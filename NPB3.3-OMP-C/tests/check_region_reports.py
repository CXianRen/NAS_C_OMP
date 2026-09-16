#!/usr/bin/env python3
"""Check SP metadata, explicit merged for boundaries and reports."""
import argparse
from pathlib import Path
import re

from report_region_times import parse_log

ROOT = Path(__file__).resolve().parents[1]
HOOK = re.compile(r'\b(PARALLEL|FOR)_(START|END)\(\s*&sp_control\s*,\s*(SP_[PF]_\w+)\s*\)')
FUNCTION = re.compile(r'^\s*(?:void|int|double)\s+(\w+)\s*\([^;{}]*\)\s*\{', re.MULTILINE)
FOR_PRAGMA = re.compile(r'#pragma\s+omp\s+for\b[^\n]*')


def source_code(text):
    """Hide comments and string literals without changing source positions."""
    return re.sub(r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda match: re.sub(r'[^\n]', ' ', match[0]), text, flags=re.DOTALL)


def block_end(code, start):
    opening = code.index('{', start)
    depth = 1
    for position in range(opening + 1, len(code)):
        depth += (code[position] == '{') - (code[position] == '}')
        if depth == 0:
            return opening, position
    raise AssertionError('unclosed block')


# Check manual structure independently of the generated name/file/line initializers.
def check_sources(directory=ROOT / 'SP/src'):
    ids = re.findall(r'\b(SP_[PF]_\w+)\s*,', (directory / 'sp_regions.h').read_text())
    entries = re.findall(
        r'\[(SP_[PF]_\w+)\]\s*=\s*REGION_INFO\(\s*(SP_[PF]_\w+)\s*,\s*(-?\w+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)',
        (directory / 'sp_regions.c').read_text())
    assert [entry[0] for entry in entries] == ids
    table = {}
    for key, metadata_id, parent, combined, nowait in entries:
        assert key == metadata_id, (key, metadata_id)
        table[key] = {'parent': None if parent == '-1' else parent,
                      'combined': int(combined), 'nowait': int(nowait)}
    sources = {}
    for path in directory.glob('*.c'):
        source = path.read_text()
        sources[path] = source_code(source)
        functions = list(FUNCTION.finditer(source))
        for hook in HOOK.finditer(source):
            kind, boundary, key = hook.groups()
            region = table[key]
            assert boundary not in region, (path, key, 'duplicate hook')
            region[boundary] = hook.start()
            if boundary == 'END':
                continue
            region['file'] = path
            region['line'] = source.count('\n', 0, hook.start()) + 1
            region['function'] = next(f[1] for f in reversed(functions) if f.start() < hook.start())
            region['label'] = f"{region['function']}:{region['line']}" + (' (nowait)' if region['nowait'] else '')
            pragma = re.search(r'#pragma omp[^\n]*(?:\\\n[^\n]*)?', source[hook.end():])[0]
            assert ('PARALLEL' if region['parent'] is None else 'FOR') == kind
            assert bool(re.search(r'\bparallel\b', pragma)) == (kind == 'PARALLEL')
            assert bool(re.search(r'\bparallel\s+for\b', pragma)) == bool(region['combined'])
            assert bool(re.search(r'\bnowait\b', pragma)) == bool(region['nowait'])
    for key, region in table.items():
        if region['parent'] is None:
            assert region['START'] < region['END'], key
            continue
        owner = table[region['parent']]
        assert owner['parent'] is None and owner['file'] == region['file'], key
        assert owner['START'] < region['START'] < owner['END'], key
        assert region['START'] < region['END'] < owner['END'], key

    for parent, owner in table.items():
        if owner['parent'] is not None:
            continue
        children = sorted((key for key in table if table[key]['parent'] == parent),
                          key=lambda key: table[key]['START'])
        if not children:
            continue
        code = sources[owner['file']]
        opening, closing = block_end(code, owner['START'])
        owner['body_end'] = closing
        loops = list(FOR_PRAGMA.finditer(code, opening, closing))
        covered = []
        previous_end = opening
        for key in children:
            region = table[key]
            assert previous_end < region['START'] < closing, key
            previous_end = region['END']
            members = [loop for loop in loops
                       if region['START'] < loop.start() < region['END']]
            assert members, (key, 'no enclosed OpenMP for')
            covered.extend(members)
            # A merged region has one START and one END. Every loop except its
            # final loop must be nowait; an ordinary loop closes the region.
            assert all(re.search(r'\bnowait\b', loop[0]) for loop in members[:-1]), key
            last = members[-1]
            _, last_end = block_end(code, last.end())
            assert last_end < region['END'], key
            region['loop_count'] = len(members)
            if re.search(r'\bnowait\b', last[0]):
                # A terminal nowait ends explicitly after the parallel join.
                assert closing < region['END'], (key, 'nowait END before parallel join')
                assert not code[closing + 1:region['END']].strip(), key
            else:
                assert region['END'] < closing, key
                assert not code[last_end + 1:region['END']].strip(), key
        assert covered == loops, (parent, 'each OpenMP for must belong to exactly one region')
    return table


# Verify compiled labels, static parents, shared combined samples and percentages.
def check_report(path, table):
    result = parse_log(path)
    text = path.read_text()
    rows = result['rows']
    assert rows and all(row['level'] in ('parallel', 'for') for row in rows), path
    by_key = {(row['level'], row['region']): row for row in rows}
    assert len(rows) == len(by_key), path
    shares = re.findall(r'  step: ([\d.]+)%', text)
    assert len(shares) == len(rows), path
    total = result['total']
    assert total > 0, path
    expected = {}
    for region in table.values():
        name, owner = region['label'], region['parent']
        expected['parallel' if owner is None else 'for', name] = (
            None if owner is None else table[owner]['label'])
        if region['combined']:
            expected['for', name] = name
    for row, share in zip(rows, shares):
        key = row['level'], row['region']
        assert key in expected, (path, row)
        tolerance = 0.0005 + 100 * 0.5e-9 / total * (1 + row['seconds'] / total)
        assert abs(float(share) - row['percent_total']) <= tolerance, (path, row)
        if row['level'] == 'parallel':
            assert row['depth'] == 0 and row['parent'] == 'iteration total', (path, row)
            parent_seconds = total
        else:
            assert row['depth'] == 1 and row['parent'] == expected[key], (path, row)
            parent_seconds = by_key['parallel', row['parent']]['seconds']
        assert row['seconds'] <= parent_seconds + 1e-9, (path, row)
        assert abs(row['percent_parent'] - 100 * row['seconds'] / parent_seconds) < 1e-8
    for region in table.values():
        name = region['label']
        if region['combined'] and ('parallel', name) in by_key:
            p, f = by_key['parallel', name], by_key['for', name]
            assert p['seconds'] == f['seconds'] and p['percent_total'] == f['percent_total'], (path, name)
    for parent, region in table.items():
        name = region['label']
        if region['parent'] is not None or ('parallel', name) not in by_key:
            continue
        groups = [child['label'] for child in table.values()
                  if child['parent'] == parent]
        for group in groups:
            assert ('for', group) in by_key, (path, 'missing merged group', group)
        # All internal groups are disjoint; double-counting a merged loop fails.
        seconds = sum(by_key['for', group]['seconds'] for group in groups)
        tolerance = (len(groups) + 1) * 0.5e-9
        assert seconds <= by_key['parallel', name]['seconds'] + tolerance, (path, name)
    return result


# Without a log, validate metadata and explicit merged hook pairs.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('path', type=Path, nargs='?', help='SP log or a directory containing SP.log')
    parser.add_argument('--source-dir', type=Path, default=ROOT / 'SP/src')
    args = parser.parse_args()
    table = check_sources(args.source_dir)
    parallel = sum(region['parent'] is None for region in table.values())
    print(f'PASS: {len(table)} manual regions ({parallel} parallel, {len(table) - parallel} for)')
    if args.path:
        path = args.path / 'SP.log' if args.path.is_dir() else args.path
        result = check_report(path, table)
        print(f"PASS SP: {len(result['rows'])} rows; compiled labels, parents, combined equality, percentages")


if __name__ == '__main__':
    main()
