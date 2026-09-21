#!/usr/bin/env python3
"""Instrument C/C++ OpenMP parallel/for regions using Clang's JSON AST.

Sources are never overwritten. Pass the same -I/-D options as the real build
after --. Application-specific npb_time_begin/end/report calls remain explicit.
C++ region sites must be in translation-unit free functions.
"""
import argparse
from collections import defaultdict
from dataclasses import dataclass, field
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
GENERATOR = 'npb-instrument-regions-v1'
KINDS = {"OMPParallelDirective": "parallel", "OMPParallelForDirective": "combined",
         "OMPForDirective": "for"}
SOURCE_SUFFIXES = {'.c', '.cc', '.cpp', '.cxx', '.C'}
PAIRS = re.compile(r"^[ \t]*NPB_(?:PARALLEL_FOR|PARALLEL|FOR)_(?:BEGIN\([^\n]*\)|END\(\))[ \t]*$",
                   re.MULTILINE)


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

    @property
    def label(self):
        location = f"{self.function}:{self.line}"
        if self.nowait:
            location += f"-{self.end_line} (nowait)"
        return location


def offset(location):
    if "offset" not in location or "spellingLoc" in location or "expansionLoc" in location:
        raise ValueError("macro-generated OpenMP regions are unsupported; use a literal #pragma")
    return location["offset"]


def line_at(source, position):
    return source.count("\n", 0, position) + 1


def statement_end(node, source):
    loc = node["range"]["end"]
    end = offset(loc) + loc.get("tokLen", 0)
    # Clang's expression/for ranges omit the terminating semicolon.
    tail = re.match(r"(?:\s|/\*.*?\*/|//[^\n]*\n)*;", source[end:], re.S)
    if tail and source[end - 1:end] != "}":
        end += tail.end()
    return end


def clang_ast(source, original, args, clang):
    command = [clang, "-fopenmp", "-fsyntax-only", "-Xclang", "-ast-dump=json",
               "-iquote", str(original.parent), "-I", str(ROOT / "common"), *args, str(source)]
    process = subprocess.run(command, capture_output=True, text=True)
    if process.returncode:
        raise ValueError(f"Clang could not parse {original}:\n{process.stderr}")
    tree = json.loads(process.stdout)
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
    regions, calls, bodies = [], [], {}
    barriers = []
    visited = set()

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
            regions.append(Region(str(path), function, KINDS[kind], start, end,
                                  line_at(source, start), line_at(source, end),
                                  nowait, direct, compound if compound is not None else -1,
                                  loops=[line_at(source, start)]))
        if kind == 'OMPBarrierDirective':
            barriers.append((offset(node['range']['begin']), statement_end(node, source)))
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
            continue
        if node.get("kind") != "FunctionDecl":
            if has_openmp(node):
                raise ValueError(f"{path}: C++ OpenMP sites must be in translation-unit free functions")
            continue
        body = next((item for item in node.get("inner", []) if item.get("kind") == "CompoundStmt"), None)
        if body is not None:
            walk(body, node["name"])

    # Build source ranges for sibling nowait groups. The runtime keeps the
    # last group pending past its lexical end until the next measured for or
    # existing synchronization, including an enclosing parallel join. Repeated
    # invocations close/restart the interval at the next begin timestamp.
    removed = set()
    for region in sorted(regions, key=lambda item: item.start):
        if not region.nowait or id(region) in removed or not region.direct:
            continue
        end = region.group
        for sibling in bodies.get(region.group, []):
            if "range" not in sibling or "offset" not in sibling["range"].get("begin", {}):
                continue
            start = offset(sibling["range"]["begin"])
            if start <= region.start:
                continue
            other = next((item for item in regions if item.start == start), None)
            if other and other.kind == "for" and other.nowait:
                region.loops.extend(other.loops)
                removed.add(id(other))
            elif (sibling.get("kind", "").startswith("OMP") and
                  sibling.get("kind") not in ("OMPMasterDirective", "OMPMaskedDirective")) or sibling.get("kind") in (
                      'ForStmt', 'WhileStmt', 'DoStmt', 'IfStmt', 'SwitchStmt',
                      'ReturnStmt', 'BreakStmt', 'ContinueStmt', 'GotoStmt'):
                end = start
                break
        region.end = end
        region.end_line = line_at(source, end)
    return [item for item in regions if id(item) not in removed], calls, barriers


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


def generate(sources, regions, barriers):
    files = {}
    header = ['/* Generated by instrument_regions.py; edit the original sources. */',
              '#ifndef NPB_GENERATED_REGIONS_H', '#define NPB_GENERATED_REGIONS_H',
              '#include "region_timers.h"', 'enum {']
    header += [f'  R_AUTO_{r.id} = {r.id},' for r in regions]
    header += ['  R_AUTO_COUNT', '};',
               '#define NPB_AUTO_FOR_START(id) \\',
               '  if (npb_time_active) { NPB_TIME_MASTER npb_time_nowait_start(id); }',
               '#define NPB_AUTO_SYNC() \\',
               '  if (npb_time_active) { NPB_TIME_MASTER npb_time_sync(); }',
               '#endif', '']
    files['npb_generated_regions.h'] = '\n'.join(header)
    table = ['#include "npb_generated_regions.h"', 'const npb_region_info npb_regions[] = {']
    for r in regions:
        parent = '-1' if r.parent == -1 else f'R_AUTO_{r.parent}'
        table.append(f'  [R_AUTO_{r.id}] = {{{json.dumps(r.label)}, {parent}, {int(r.kind == "combined")}}},')
    if not regions:
        table.append('  {"unused", -1, 0},')
    table += ['};', f'const int npb_region_count = {len(regions)};', '']
    files['npb_generated_regions.c'] = '\n'.join(table)
    for path, source in sources.items():
        edits = defaultdict(list)
        for r in regions:
            if r.source != str(path):
                continue
            if r.nowait:
                # Keep a pending interval across lexical scopes and helper
                # returns. Its next for, explicit barrier or parallel join
                # closes it. No new barrier is introduced.
                begin = f'NPB_AUTO_FOR_START(R_AUTO_{r.id})'
                end = ''
                if not r.direct:
                    begin, end = '{\n' + begin, '}'
            else:
                prefix = {'parallel': 'NPB_PARALLEL', 'combined': 'NPB_PARALLEL_FOR', 'for': 'NPB_FOR'}[r.kind]
                begin, end = f'{prefix}_BEGIN(R_AUTO_{r.id})', f'{prefix}_END()'
            edits[r.start].append((1, -r.end, begin))
            if end:
                edits[r.end].append((0, -r.start, end))
        for start, end in barriers.get(path, []):
            # Keep the barrier and its post-barrier timing hook together.
            edits[start].append((1, -end, '{'))
            edits[end].append((0, -start, 'NPB_AUTO_SYNC()\n}'))
        result = source
        for position in sorted(edits, reverse=True):
            additions = '\n'.join(item[2] for item in sorted(edits[position]))
            restore = f'#line {line_at(source, position)} {json.dumps(str(path))}'
            result = result[:position] + '\n' + additions + '\n' + restore + '\n' + result[position:]
        files[path.name] = ('/* Generated by instrument_regions.py. */\n'
                            '#include "npb_generated_regions.h"\n'
                            f'#line 1 {json.dumps(str(path))}\n' + result)
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sources', type=Path, nargs='+')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--clang', default='clang-18')
    parser.add_argument('--strip-existing', action='store_true',
                        help='remove standalone NPB BEGIN/END lines from generated copies, retaining application window markers')
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
            if path.name == 'npb_generated_regions.c':
                raise ValueError('npb_generated_regions.c is reserved for the generated table')
            if path.name in {p.name for p in sources}:
                raise ValueError('source basenames must be unique within one generated program')
            text = path.read_text()
            if text.startswith('/* Generated by instrument_regions.py.'):
                raise ValueError('use the original inputs when regenerating, not generated files')
            if PAIRS.search(text):
                if not args.strip_existing:
                    raise ValueError(f'{path}: already instrumented; use --strip-existing to migrate a generated copy')
                text = PAIRS.sub(lambda m: ' ' * len(m[0]), text)
            if re.search(r'npb_time_(?:start|stop)\s*\(', text):
                raise ValueError(f'{path}: custom timing calls require a manual migration; no files written')
            sources[path] = text
        regions, calls, barriers = [], [], {}
        with tempfile.TemporaryDirectory(prefix='npb-instrument-') as temporary:
            temporary = Path(temporary)
            for path, text in sources.items():
                working = temporary / path.name
                working.write_text(text)
                found, invoked, synchronized = analyze(path, text, clang_ast(working, path, flags, args.clang))
                barriers[path] = synchronized
                regions.extend(found)
                calls.extend((str(path), *call) for call in invoked)
            regions.sort(key=lambda r: (r.source, r.start))
            for index, region in enumerate(regions):
                region.id = index
            if len(regions) > 256:
                raise ValueError(f'{len(regions)} regions exceed NPB_MAX_REGIONS=256')
            assign_parents(regions, calls)
            files = generate(sources, regions, barriers)
            for name, text in files.items():
                (temporary / name).write_text(text)
            for path in sources:
                command = [args.clang, '-fopenmp', '-fsyntax-only', '-I', str(temporary),
                           '-iquote', str(path.parent), '-I', str(ROOT / 'common'), *flags,
                           str(temporary / path.name)]
                checked = subprocess.run(command, capture_output=True, text=True)
                if checked.returncode:
                    raise ValueError(f'generated source failed validation:\n{checked.stderr}')
            manifest = {'generator': GENERATOR, 'sources': list(map(str, sources)), 'compiler_args': flags,
                        'source_sha256': {str(p): hashlib.sha256(p.read_text().encode()).hexdigest() for p in sources},
                        'generated_files': sorted([*files, 'instrumentation.json']),
                        'regions': [dict(id=r.id, file=r.source, function=r.function, kind=r.kind,
                                         line=r.line, end_line=r.end_line, parent=r.parent,
                                         timing_end='next_for_or_barrier' if r.nowait else 'construct_end',
                                         nowait=r.nowait, loop_lines=r.loops, label=r.label) for r in regions]}
            files['instrumentation.json'] = json.dumps(manifest, indent=2) + '\n'
            output = args.output.resolve()
            for path in sources:
                if output / path.name == path:
                    raise ValueError('output must not overwrite an input source')
            previous_files = set()
            if output.exists() and any(output.iterdir()):
                manifest_path = output / 'instrumentation.json'
                previous = json.loads(manifest_path.read_text()) if manifest_path.exists() else {}
                if previous.get('generator') != GENERATOR and previous.get('sources') != manifest['sources']:
                    raise ValueError('output directory is not owned by this generation; choose an empty directory')
                previous_files = set(previous.get('generated_files', files))
                if any(Path(name).name != name for name in previous_files):
                    raise ValueError('invalid generated file name in previous manifest')
                for name in set(files) - previous_files:
                    if (output / name).exists():
                        raise ValueError(f'output would overwrite an unowned file: {name}')
            output.mkdir(parents=True, exist_ok=True)
            for name, text in files.items():
                (output / name).write_text(text)
            for name in previous_files - files.keys():
                (output / name).unlink(missing_ok=True)
            print(f'Generated {len(regions)} regions ({sum(r.nowait for r in regions)} nowait groups): {output}')
            print(f'Compile generated sources with {output}/npb_generated_regions.c and common/region_timers.c;')
            print('retain the original include/define flags and add -I' + str(ROOT / 'common'))
    except (ValueError, OSError, StopIteration) as error:
        parser.error(str(error))


if __name__ == '__main__':
    main()
