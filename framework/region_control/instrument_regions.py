#!/usr/bin/env python3
"""Generate OpenMP source hooks for the shared region_control runtime.

Sources are never overwritten. Pass the same -I/-D options as the real build
after --. Application-specific measurement windows and reports remain explicit.
C++ region sites must be in translation-unit free functions or function templates.
"""
import argparse
from collections import defaultdict
from dataclasses import dataclass, field
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent
GENERATOR = 'region-control-instrument-v1'
OWNER_FILE = '.region-auto-owner.json'
KINDS = {"OMPParallelDirective": "parallel", "OMPParallelForDirective": "combined",
         "OMPForDirective": "for"}
SOURCE_SUFFIXES = {'.c', '.cc', '.cpp', '.cxx', '.C'}
MANUAL_HOOKS = re.compile(r'\b(?:(?:PARALLEL|FOR)_(?:START|END)|'
                          r'region_(?:parallel|for)_(?:start|end))\s*\(')


@dataclass
class Region:
    source: str
    function: str
    kind: str
    start: int
    end: int
    line: int
    end_line: int
    nowait: bool = False
    direct: bool = False
    group: int = -1
    parent: int = -1
    id: int = -1
    loops: list = field(default_factory=list)
    timing_end: str = 'construct_end'
    members: list = field(default_factory=list)
    end_source: str = ''
    guarded: bool = False

    @property
    def label(self):
        location = f"{self.function}:{self.line}"
        if self.nowait:
            location += " (nowait)"
        return location


def offset(location):
    if "offset" not in location or "spellingLoc" in location or "expansionLoc" in location:
        raise ValueError("macro-generated OpenMP regions are unsupported; use a literal #pragma")
    return location["offset"]


def line_at(source, position):
    return source.count("\n", 0, position) + 1


def statement_end(node, source):
    loc = node["range"]["end"]
    # Literal OpenMP directives may end in an ordinary function-like macro
    # invocation (for example atomic_fetch_add). Only pragma locations forbid
    # expansion; a structured statement's endpoint follows that invocation.
    expanded = 'expansionLoc' in loc
    loc = loc.get('expansionLoc', loc)
    end = offset(loc) + loc.get("tokLen", 0)
    if expanded:
        code = blank_noncode(source)
        opening = end
        while opening < len(code) and code[opening].isspace():
            opening += 1
        if opening < len(code) and code[opening] == '(':
            depth = 1
            end = opening + 1
            while end < len(code) and depth:
                if code[end] == '(':
                    depth += 1
                elif code[end] == ')':
                    depth -= 1
                end += 1
            if depth:
                raise ValueError('cannot determine the end of a macro invocation')
    # Clang's expression/for ranges omit the terminating semicolon.
    tail = re.match(r"(?:\s|/\*.*?\*/|//[^\n]*\n)*;", source[end:], re.S)
    if tail and source[end - 1:end] != "}":
        end += tail.end()
    return end


def blank_noncode(source):
    return re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m: ''.join('\n' if char == '\n' else ' ' for char in m[0]),
                  source, flags=re.S)


def clang_ast(source, original, args, clang, dependencies):
    depfile = source.with_suffix(source.suffix + '.d')
    command = [clang, "-fopenmp", "-fsyntax-only", "-Xclang", "-ast-dump=json",
               '-MMD', '-MP', '-MF', str(depfile), '-MT', 'REGION_AUTO_TARGET',
               '-I', str(source.parent), "-iquote", str(original.parent), "-I", str(ROOT),
               *args, str(source)]
    process = subprocess.run(command, capture_output=True, text=True)
    if process.returncode:
        raise ValueError(f"Clang could not parse {original}:\n{process.stderr}")
    tree = json.loads(process.stdout)
    dependencies.append((depfile.read_text(), source, original))
    text = source.read_text()
    if not text.isascii():
        # Clang reports byte offsets; Python slices Unicode characters.
        positions = []
        for index, char in enumerate(text):
            positions.extend([index] * len(char.encode('utf-8')))
        positions.append(len(text))
        def convert(item):
            if isinstance(item, dict):
                byte = item.get('offset')
                if byte is not None and byte < len(positions):
                    length = item.get('tokLen', 0)
                    if byte + length < len(positions):
                        item['tokLen'] = positions[byte + length] - positions[byte]
                    item['offset'] = positions[byte]
                for value in item.values():
                    convert(value)
            elif isinstance(item, list):
                for value in item:
                    convert(value)
        convert(tree)
    return tree


def has_nowait(pragma):
    pragma = re.sub(r'/\*.*?\*/|//[^\n]*', '', pragma, flags=re.S)
    depth = 0
    for token in re.findall(r'\w+|[()]', pragma):
        if token == '(':
            depth += 1
        elif token == ')':
            depth -= 1
        elif token == 'nowait' and depth == 0:
            return True
    return False


def analyze(path, source, ast):
    regions, calls, bodies, definitions = [], [], {}, {}
    barriers = set()
    visited = set()
    region_sites = set()

    def has_openmp(node):
        kind = node.get("kind", "")
        return ((kind.startswith("OMP") and ("Parallel" in kind or "For" in kind)) or
                any(has_openmp(child) for child in node.get("inner", [])))

    def walk(node, function, compound=None, direct=False):
        identity = node.get("id")
        if identity and identity in visited:
            return
        if identity:
            visited.add(identity)
        kind = node.get("kind", "")
        children = node.get("inner", [])
        if kind in ("LambdaExpr", "CXXRecordDecl", "FunctionTemplateDecl") and has_openmp(node):
            raise ValueError(f"{path}: C++ OpenMP sites must be in translation-unit free functions")
        if kind == "CompoundStmt" and "offset" in node["range"]["end"]:
            compound = offset(node["range"]["end"])
            bodies[compound] = children
        if kind.startswith("OMP") and ("Parallel" in kind or "For" in kind) and kind not in KINDS:
            raise ValueError(f"{path}: unsupported construct {kind}; no files written")
        if kind in KINDS:
            start = offset(node["range"]["begin"])
            pragma_end = offset(node["range"]["end"])
            if not source[start:pragma_end].startswith("#pragma"):
                raise ValueError(f"{path}: region is not a literal source pragma")
            body = next((child for child in children if child.get("kind") == "CapturedStmt"), None)
            if body is None:
                raise ValueError(f"{path}: no structured body for {kind}")
            end = statement_end(body, source)
            nowait = KINDS[kind] == "for" and has_nowait(source[start:pragma_end])
            # A function template has one source body but Clang emits another
            # AST body for each instantiation. All specializations of this
            # source pragma share one timer. Still visit instantiated bodies
            # so resolved calls participate in parallel-parent analysis.
            site = (kind, start, end)
            if site not in region_sites:
                region_sites.add(site)
                regions.append(Region(str(path), function, KINDS[kind], start, end,
                                      line_at(source, start), line_at(source, end),
                                      nowait, direct, compound if compound is not None else -1,
                                      loops=[line_at(source, start)]))
        if kind == 'OMPBarrierDirective':
            barriers.add((offset(node['range']['begin']), statement_end(node, source)))
        if kind == "CallExpr" and children:
            def callee(item):
                decl = item.get("referencedDecl", {})
                if decl.get("kind") == "FunctionDecl":
                    return decl.get("name")
                for child in item.get("inner", []):
                    result = callee(child)
                    if result:
                        return result
                return None
            name = callee(children[0])
            if name and "offset" in node.get("range", {}).get("begin", {}):
                calls.append((function, name, offset(node["range"]["begin"])))
        for child in children:
            walk(child, function, compound, kind == "CompoundStmt")

    for node in ast.get("inner", []):
        if node.get("loc", {}).get("includedFrom"):
            if has_openmp(node):
                raise ValueError(f'{path}: OpenMP sites in included headers are unsupported; provide source-level sites')
            continue
        if node.get("kind") == "FunctionTemplateDecl":
            functions = [item for item in node.get("inner", [])
                         if item.get("kind") == "FunctionDecl"]
        elif node.get("kind") == "FunctionDecl":
            functions = [node]
        else:
            if has_openmp(node):
                raise ValueError(f"{path}: C++ OpenMP sites must be in translation-unit free functions")
            continue
        for function in functions:
            body = next((item for item in function.get("inner", [])
                         if item.get("kind") == "CompoundStmt"), None)
            if body is not None:
                name = function["name"]
                previous = definitions.get(name)
                # Template instantiations share a source body. Overloads do
                # not: a bare-name lookup must never silently select one.
                if name in definitions and (previous is None or previous.get('range') != body.get('range')):
                    definitions[name] = None
                else:
                    definitions[name] = body
                walk(body, function["name"])

    return regions, list(dict.fromkeys(calls)), bodies, definitions


def merge_nowait(regions, bodies, sources, functions):
    """Resolve each nowait group to a source-level synchronization endpoint.

    Conditional/repeated helper sites share one compiler-selected ID. Generated
    guards start that ID on its first executed member and close it at the fixed
    barrier. They never discover boundaries at runtime or add synchronization.
    """
    by_site = {(r.source, r.start): r for r in regions}
    removed, assigned = set(), set()
    pure_queries = {'omp_get_num_threads', 'omp_get_thread_num',
                    'omp_get_max_threads', 'omp_get_wtime', 'omp_in_parallel'}

    def location(node):
        loc = node.get('range', {}).get('begin', {})
        return loc.get('expansionLoc', loc).get('offset', -1)

    def target(source, name):
        if (source, name) in functions:
            return (source, name)
        candidates = [key for key in functions if key[1] == name]
        return candidates[0] if len(candidates) == 1 else None

    def callee(node):
        decl = node.get('referencedDecl', {})
        if node.get('kind') == 'DeclRefExpr' and decl.get('kind') == 'FunctionDecl':
            return decl.get('name')
        if node.get('kind') in {'ImplicitCastExpr', 'ParenExpr'}:
            children = node.get('inner', [])
            return callee(children[0]) if len(children) == 1 else None
        return None

    def unsafe_expression(item):
        kind = item.get('kind')
        if kind in {'CallExpr', 'CXXMemberCallExpr', 'CXXOperatorCallExpr',
                    'CXXConstructExpr', 'CXXTemporaryObjectExpr', 'CXXNewExpr', 'CXXDeleteExpr'}:
            children = item.get('inner', [])
            name = callee(children[0]) if children and kind == 'CallExpr' else None
            return name not in pure_queries or any(unsafe_expression(c) for c in children[1:])
        return any(unsafe_expression(c) for c in item.get('inner', []))

    def events(node, source, scopes=(), stack=(), seen=None):
        if seen is None:
            seen = set()
        identity = node.get('id')
        if identity and identity in seen:
            return
        if identity:
            seen.add(identity)
        kind, start = node.get('kind', ''), location(node)
        children = node.get('inner', [])
        region = by_site.get((source, start)) if kind in KINDS else None
        if region:
            if region.nowait and not region.direct:
                raise ValueError(f'{source}:{region.line}: unsupported unbraced nowait; use a compound body')
            yield ('work', region, scopes, source, region.end)
            return  # Calls in a worksharing loop cannot close its outer timer.
        if kind == 'OMPBarrierDirective':
            yield ('explicit_barrier', None, scopes, source,
                   statement_end(node, sources[Path(source)]))
            return
        if kind == 'OMPSingleDirective':
            pragma_end = offset(node['range']['end'])
            if not has_nowait(sources[Path(source)][start:pragma_end]):
                body = next(c for c in children if c.get('kind') == 'CapturedStmt')
                yield ('single_barrier', None, scopes, source,
                       statement_end(body, sources[Path(source)]))
            return
        if kind in {'OMPAtomicDirective', 'OMPMasterDirective', 'OMPMaskedDirective',
                    'OMPCriticalDirective', 'OMPFlushDirective'}:
            return  # No team barrier is legal inside these constructs.
        if kind in {'ReturnStmt', 'GotoStmt', 'IndirectGotoStmt', 'CXXThrowExpr',
                    'BreakStmt', 'ContinueStmt'}:
            yield ('unsafe', None, scopes, source, start)
            return
        if kind == 'CallExpr':
            if any(unsafe_expression(child) for child in children[1:]):
                yield ('unsafe', None, scopes, source, start)
            name = callee(children[0]) if children else None
            key = target(source, name)
            if key and functions[key] is not None and key not in stack:
                # A distinct visit per call preserves different call contexts.
                yield from events(functions[key], key[0], scopes,
                                  (*stack, key), set())
            elif name not in pure_queries:
                yield ('unsafe', None, scopes, source, start)
            return
        if kind in {'CXXMemberCallExpr', 'CXXOperatorCallExpr', 'CXXConstructExpr',
                    'CXXTemporaryObjectExpr', 'CXXNewExpr', 'CXXDeleteExpr'} or (
                kind.startswith('OMP') and kind.endswith('Directive')):
            yield ('unsafe', None, scopes, source, start)
            return
        if kind in {'IfStmt', 'ForStmt', 'WhileStmt', 'DoStmt', 'SwitchStmt'}:
            # The AST puts an if's then/else last, ordinary loop bodies last,
            # and a do-loop body first. Do not discard unbraced returns/calls.
            count = 2 if kind == 'IfStmt' and node.get('hasElse') else 1
            indices = {0} if kind == 'DoStmt' else set(range(len(children) - count, len(children)))

            for index, child in enumerate(children):
                scope = (*scopes, (source, start, index))
                if index in indices:
                    yield from events(child, source, scope, stack, seen)
                    if kind in {'ForStmt', 'WhileStmt', 'DoStmt'}:
                        yield ('loop_exit', None, scope, source, start)
                        if any(unsafe_expression(c) for i, c in enumerate(children) if i not in indices):
                            yield ('unsafe', None, scopes, source, start)
                elif unsafe_expression(child):
                    yield ('unsafe', None, scopes, source, location(child))
            return
        for child in children:
            yield from events(child, source, scopes, stack, seen)

    for owner in [r for r in regions if r.kind == 'parallel']:
        pending, sync_scopes = [], set()

        def close(kind, end_source, end, scopes):
            if not pending:
                return
            for member, scope in pending:
                if scope[:len(scopes)] != scopes:
                    raise ValueError(f'{member.source}:{member.line}: cannot close nowait through a conditional barrier')
            unique = list({id(member): member for member, _ in pending}.values())
            first = unique[0]
            if any(id(member) in assigned for member in unique):
                raise ValueError(f'{first.source}:{first.line}: nowait helper has multiple static completion sites')
            assigned.update(map(id, unique))
            first.members = [(r.source, r.start, r.line) for r in unique]
            first.loops = [r.line for r in unique if r.source == first.source]
            first.guarded = (any(scope for _, scope in pending) or
                             any(r.source != owner.source or
                                 not owner.start < r.start < owner.end for r in unique) or
                             end_source != owner.source or not owner.start < end <= owner.end)
            first.end, first.end_source, first.timing_end = end, end_source, kind
            first.end_line = line_at(sources[Path(end_source)], end)
            removed.update(id(r) for r in unique[1:])
            pending.clear()

        for child in bodies[Path(owner.source)].get(owner.end - 1, []):
            for kind, region, scopes, source, end in events(child, owner.source):
                if kind.endswith('_barrier') or kind == 'work' and not region.nowait:
                    sync_scopes.add(scopes)
                if kind == 'loop_exit':
                    if pending and any(s[:len(scopes)] == scopes for s in sync_scopes):
                        first = pending[0][0]
                        raise ValueError(f'{first.source}:{first.line}: cannot carry nowait across a loop backedge containing a barrier')
                elif kind == 'work':
                    if region.parent != owner.id:
                        raise ValueError(f'{source}:{region.line}: inconsistent parallel parent')
                    if region.nowait or pending:
                        pending.append((region, scopes))
                        if not region.nowait:
                            close('for_barrier', source, end, scopes)
                elif kind == 'unsafe':
                    if pending:
                        first = pending[0][0]
                        raise ValueError(f'{first.source}:{first.line}: cannot prove a static nowait boundary through control flow or call at {source}:{line_at(sources[Path(source)], end)}')
                else:
                    close(kind, source, end, scopes)
        close('parallel_join', owner.source, owner.end, ())
    for r in regions:
        if r.nowait and id(r) not in assigned:
            raise ValueError(f'{r.source}:{r.line}: unsupported nowait completion scope')
    kept = [r for r in regions if id(r) not in removed]
    remap = {r.id: index for index, r in enumerate(kept)}
    for region in kept:
        region.id = remap[region.id]
        if region.parent >= 0:
            region.parent = remap[region.parent]
    return kept


def assign_parents(regions, calls):
    """Follow direct calls for orphaned for loops; reject ambiguous parents."""
    contexts = defaultdict(set)

    def enclosing(source, position):
        candidates = [r for r in regions if r.source == source and r.kind != "for"
                      and r.start < position < r.end]
        return min(candidates, key=lambda r: r.end - r.start).id if candidates else None

    functions = {(r.source, r.function) for r in regions}
    functions.update((source, caller) for source, caller, _, _ in calls)
    for _ in range(len(functions) + 1):
        changed = False
        for source, caller, callee, position in calls:
            candidates = [key for key in functions if key[1] == callee]
            local = (source, callee)
            targets = [local] if local in candidates else candidates
            if len(targets) != 1:
                continue
            owner = enclosing(source, position)
            inherited = {owner} if owner is not None else contexts[source, caller]
            before = len(contexts[targets[0]])
            contexts[targets[0]].update(inherited)
            changed |= len(contexts[targets[0]]) != before
        if not changed:
            break
    for region in regions:
        owner = enclosing(region.source, region.start)
        candidates = {owner} if owner is not None else contexts[region.source, region.function]
        if region.kind != "for":
            if candidates:
                raise ValueError(f'{region.source}:{region.line}: nested parallel execution is unsupported by the shared timer arrays')
            continue
        if len(candidates) != 1:
            raise ValueError(f"{region.source}:{region.line}: orphaned for has {len(candidates)} possible parallel parents; "
                             "include its caller source, or retain manual instrumentation for this site")
        region.parent = next(iter(candidates))


def generate(sources, regions, control):
    files = {}
    header = ['/* Generated by instrument_regions.py; edit the original sources. */',
              '#ifndef REGION_AUTO_H', '#define REGION_AUTO_H',
              '#include "region_control.h"', 'enum {']
    header += [f'  R_AUTO_{r.id} = {r.id},' for r in regions]
    header += [f'  REGION_AUTO_COUNT = {len(regions)}', '};',
               '#ifdef __cplusplus', 'extern "C" {', '#endif',
               'extern region_info region_auto_info[];',
               'extern unsigned char region_auto_started[];',
               '#ifdef __cplusplus', '}', '#endif',
               '#if REGION_INSTRUMENT',
               '#define REGION_AUTO_BEGIN(c, id) do { \\',
               '  _Pragma("omp master") { if (!region_auto_started[id]) { \\',
               '    region_auto_started[id] = 1; \\',
               '    region_for_start((c), (id), __FILE__, __func__, __LINE__); \\',
               '  } } \\',
               '} while (0)',
               '#define REGION_AUTO_END(c, id) do { \\',
               '  _Pragma("omp master") { if (region_auto_started[id]) { \\',
               '    region_for_end((c), (id)); region_auto_started[id] = 0; \\',
               '  } } \\',
               '} while (0)',
               '#else',
               '#define REGION_AUTO_BEGIN(c, id) ((void)0)',
               '#define REGION_AUTO_END(c, id) ((void)0)',
               '#endif',
               '#endif', '']
    files['region_auto.h'] = '\n'.join(header)
    table = ['#include "region_auto.h"', 'region_info region_auto_info[] = {']
    for r in regions:
        parent = '-1' if r.parent == -1 else f'R_AUTO_{r.parent}'
        table.append(f'  [R_AUTO_{r.id}] = {{{json.dumps(r.function)}, {parent}, '
                     f'{int(r.kind == "combined")}, {json.dumps(r.source)}, {r.line}, {int(r.nowait)}}},')
    if not regions:
        table.append('  {"unused", -1, 0, "", 0, 0},')
    table += ['};', 'unsigned char region_auto_started[REGION_AUTO_COUNT ? REGION_AUTO_COUNT : 1];', '']
    files['region_auto.c'] = '\n'.join(table)
    for path, source in sources.items():
        edits = defaultdict(list)
        for r in regions:
            prefix = 'FOR' if r.kind == 'for' else 'PARALLEL'
            begin = f'{prefix}_START({control}, R_AUTO_{r.id});'
            end = f'{prefix}_END({control}, R_AUTO_{r.id});'
            if r.guarded:
                begin = f'REGION_AUTO_BEGIN({control}, R_AUTO_{r.id});'
                end = f'REGION_AUTO_END({control}, R_AUTO_{r.id});'
            # Wrappers preserve unbraced if/else. A guarded static group can
            # span multiple branches/functions, but has one fixed END site.
            if not r.nowait:
                begin, end = '{\n' + begin, end + '\n}'
            starts = r.members if r.guarded else [(r.source, r.start, r.line)]
            for file, start, _ in starts:
                if file == str(path):
                    edits[start].append((1, -r.end, begin))
            if (r.end_source or r.source) == str(path):
                edits[r.end].append((0 if r.kind == 'for' else 1, -r.start, end))
        result = source
        for position in sorted(edits, reverse=True):
            additions = '\n'.join(item[2] for item in sorted(edits[position]))
            restore = f'#line {line_at(source, position)} {json.dumps(str(path))}'
            result = result[:position] + '\n' + additions + '\n' + restore + '\n' + result[position:]
        files[path.name] = ('/* Generated by instrument_regions.py. */\n'
                            '#include "region_auto.h"\n'
                            f'#line 1 {json.dumps(str(path))}\n' + result)
    return files


def make_quote(path):
    return (str(path).replace('\\', '\\\\').replace('$', '$$').replace('#', '\\#')
            .replace(' ', '\\ ').replace('\t', '\\\t').replace(':', '\\:'))


def atomic_write(path, text):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=path.parent,
                                     prefix=path.name + '.', delete=False) as stream:
        temporary = Path(stream.name)
        try:
            stream.write(text)
            stream.close()
            os.replace(temporary, path)
        finally:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sources', type=Path, nargs='+')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--clang', default='clang-18')
    parser.add_argument('--control', required=True, help='region_control pointer expression visible at every OpenMP site')
    parser.add_argument('--depfile', type=Path)
    import sys
    argv = sys.argv[1:]
    split = argv.index('--') if '--' in argv else len(argv)
    args = parser.parse_args(argv[:split])
    flags = argv[split + 1:] if split < len(argv) else []
    if not shutil.which(args.clang):
        parser.error(f'{args.clang} not found')
    sources = {}
    try:
        for path in args.sources:
            path = path.resolve()
            if path.suffix not in SOURCE_SUFFIXES:
                raise ValueError(f'{path}: expected a C/C++ source file (.c, .cc, .cpp, .cxx, .C)')
            if path.name == 'region_auto.c':
                raise ValueError('region_auto.c is reserved for the generated table')
            if path.name in {p.name for p in sources}:
                raise ValueError('source basenames must be unique within one generated program')
            text = path.read_text()
            if text.startswith('/* Generated by instrument_regions.py.'):
                raise ValueError('use the original inputs when regenerating, not generated files')
            if MANUAL_HOOKS.search(blank_noncode(text)):
                raise ValueError(f'{path}: existing manual OpenMP hooks would be instrumented twice; '
                                 'remove PARALLEL/FOR hooks while retaining iteration/step calls')
            sources[path] = text
        if not args.control.strip() or '\n' in args.control or '\r' in args.control:
            raise ValueError('--control must be a nonempty single-line pointer expression')
        regions, calls, bodies, dependencies, functions = [], [], {}, [], {}
        with tempfile.TemporaryDirectory(prefix='region-instrument-') as temporary:
            temporary = Path(temporary)
            bootstrap = temporary / 'region_auto.h'
            bootstrap.write_text('#ifndef REGION_AUTO_H\n#define REGION_AUTO_H\n'
                                 '#include "region_control.h"\n'
                                 '#define REGION_AUTO_COUNT 256\n'
                                 '#ifdef __cplusplus\nextern "C" {\n#endif\n'
                                 'extern region_info region_auto_info[];\n'
                                 'extern unsigned char region_auto_started[];\n'
                                 '#ifdef __cplusplus\n}\n#endif\n#endif\n')
            for path, text in sources.items():
                working = temporary / path.name
                working.write_text(text)
                found, invoked, compounds, definitions = analyze(path, text, clang_ast(working, path, flags, args.clang, dependencies))
                bodies[path] = compounds
                functions.update(((str(path), name), body) for name, body in definitions.items())
                regions.extend(found)
                calls.extend((str(path), *call) for call in invoked)
            regions.sort(key=lambda r: (r.source, r.start))
            for index, region in enumerate(regions):
                region.id = index
            assign_parents(regions, calls)
            regions = merge_nowait(regions, bodies, sources, functions)
            if len(regions) > 256:
                raise ValueError(f'{len(regions)} regions exceed REGION_CONTROL_MAX_REGIONS=256')
            names = {}
            for region in regions:
                key = f'{region.function}:{region.line}'
                if key in names:
                    raise ValueError(f'ambiguous region name {key}: {names[key]} and {region.source}')
                names[key] = region.source
            files = generate(sources, regions, args.control)
            for name, text in files.items():
                (temporary / name).write_text(text)
            for path in sources:
                command = [args.clang, '-fopenmp', '-fsyntax-only', '-I', str(temporary),
                           '-iquote', str(path.parent), '-I', str(ROOT), *flags,
                           str(temporary / path.name)]
                checked = subprocess.run(command, capture_output=True, text=True)
                if checked.returncode:
                    raise ValueError(f'generated source failed validation:\n{checked.stderr}')
            manifest = {'generator': GENERATOR, 'sources': list(map(str, sources)), 'compiler_args': flags,
                        'control': args.control,
                        'source_sha256': {str(p): hashlib.sha256(p.read_text().encode()).hexdigest() for p in sources},
                        'generated_files': sorted([*files, 'instrumentation.json', OWNER_FILE]),
                        'regions': [dict(id=r.id, file=r.source, function=r.function, kind=r.kind,
                                         line=r.line, end_line=r.end_line, parent=r.parent,
                                         timing_end=r.timing_end,
                                         nowait=r.nowait, loop_lines=r.loops, label=r.label,
                                         loop_sites=[dict(file=f, line=line) for f, _, line in
                                                     (r.members or [(r.source, r.start, r.line)])],
                                         end_file=r.end_source or r.source, guarded=r.guarded) for r in regions]}
            files['instrumentation.json'] = json.dumps(manifest, indent=2) + '\n'
            files[OWNER_FILE] = json.dumps({key: manifest[key] for key in
                                           ('generator', 'sources', 'generated_files')}, indent=2) + '\n'
            output = args.output.resolve()
            for path in sources:
                if output / path.name == path:
                    raise ValueError('output must not overwrite an input source')
            if args.depfile and args.depfile.resolve() in {
                    *sources, *(output / name for name in files)}:
                raise ValueError('depfile must not overwrite an input source or another generated output')
            previous_files = set()
            if output.exists() and any(output.iterdir()):
                manifest_path = output / 'instrumentation.json'
                ownership_path = manifest_path if manifest_path.exists() else output / OWNER_FILE
                previous = json.loads(ownership_path.read_text()) if ownership_path.exists() else {}
                if previous.get('generator') != GENERATOR:
                    raise ValueError('output directory is not owned by this generation; choose an empty directory')
                previous_files = set(previous.get('generated_files', files))
                if any(Path(name).name != name for name in previous_files):
                    raise ValueError('invalid generated file name in previous manifest')
                for name in set(files) - previous_files:
                    if (output / name).exists():
                        raise ValueError(f'output would overwrite an unowned file: {name}')
            output.mkdir(parents=True, exist_ok=True)
            for name, text in files.items():
                atomic_write(output / name, text)
            for name in previous_files - files.keys():
                (output / name).unlink(missing_ok=True)
            if args.depfile:
                # Keep the caller's path spelling: GNU Make treats absolute and
                # relative target names as distinct dependency graph nodes.
                targets = ' '.join(make_quote(args.output / name) for name in sorted(files))
                targets += ' ' + make_quote(args.depfile)
                deptext = ''
                for text, working, original in dependencies:
                    text = text.replace(make_quote(working), make_quote(original))
                    quoted = make_quote(bootstrap)
                    text = text.replace(quoted + ':\n', '').replace(quoted, '')
                    text = text.replace('REGION_AUTO_TARGET:', targets + ':', 1)
                    deptext += text + make_quote(original) + ':\n'
                atomic_write(args.depfile, deptext)
            print(f'Generated {len(regions)} regions ({sum(r.nowait for r in regions)} nowait groups): {output}')
    except (ValueError, OSError, StopIteration, json.JSONDecodeError) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
