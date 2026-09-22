#!/usr/bin/env python3
"""Check generated SP metadata against source pragmas and compiled reports."""
import argparse
import json
from pathlib import Path
import re

from report_region_times import parse_log

ROOT = Path(__file__).resolve().parents[1]
HOOK = re.compile(r'\b(?:PARALLEL|FOR)_(?:START|END)\s*\(')
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


# The manifest maps IDs; independent source checks define expected grouping.
def check_sources(directory=ROOT / 'SP/src', manifest=None):
    directory = Path(directory).resolve()
    manifest = Path(manifest) if manifest else ROOT / '.build/SP.S/generated/instrumentation.json'
    data = json.loads(manifest.read_text())
    regions = data['regions']
    assert [region['id'] for region in regions] == list(range(len(regions)))
    table, sources = {}, {}
    for path in directory.glob('*.c'):
        source = path.read_text()
        assert not HOOK.search(source_code(source)), (path, 'manual OpenMP hooks remain')
        sources[path.resolve()] = source_code(source)
    for region in regions:
        path = Path(region['file']).resolve()
        assert path in sources, (path, 'unexpected generated region source')
        code = sources[path]
        lines = code.splitlines()
        line = region['line']
        pragma = lines[line - 1].strip()
        kind = region['kind']
        assert pragma.startswith('#pragma omp '), (path, line, pragma)
        assert bool(re.search(r'\bparallel\b', pragma)) == (kind != 'for')
        assert bool(re.search(r'\bparallel\s+for\b', pragma)) == (kind == 'combined')
        assert bool(re.search(r'\bnowait\b', pragma)) == bool(region['nowait'])
        position = sum(len(text) + 1 for text in lines[:line - 1])
        functions = list(FUNCTION.finditer(code, 0, position))
        assert functions and functions[-1][1] == region['function'], region
        table[region['id']] = dict(region, file=path,
            parent=None if region['parent'] == -1 else region['parent'],
            combined=int(kind == 'combined'), START=position,
            label=f"{region['function']}:{line}" + (' (nowait)' if region['nowait'] else ''),
            loop_count=len(region['loop_lines']))
    source_parallel_sites = set()
    for path, code in sources.items():
        source_parallel_sites.update((path, code.count('\n', 0, match.start()) + 1)
            for match in re.finditer(r'#pragma\s+omp\s+parallel\b', code))
    assert {(region['file'], region['line']) for region in table.values()
            if region['parent'] is None} == source_parallel_sites
    for region in table.values():
        if region['parent'] is not None:
            assert region['parent'] in table, region
            owner = table[region['parent']]
            assert owner['parent'] is None and not owner['combined'], region
    for parent, owner in table.items():
        if owner['parent'] is not None or owner['combined']:
            continue
        code = sources[owner['file']]
        opening, closing = block_end(code, owner['START'])
        owner['body_end'] = closing
        loops = list(FOR_PRAGMA.finditer(code, opening, closing))
        expected, group = [], []
        for loop in loops:
            group.append(code.count('\n', 0, loop.start()) + 1)
            if not re.search(r'\bnowait\b', loop[0]):
                expected.append(group)
                group = []
        if group:
            expected.append(group)
        children = sorted((region for region in table.values() if region['parent'] == parent),
                          key=lambda region: region['line'])
        assert [child['loop_lines'] for child in children] == expected, (owner, children, expected)
        for child in children:
            assert child['file'] == owner['file'] and opening < child['START'] < closing, child
            assert child['line'] == child['loop_lines'][0], child
            last = next(loop for loop in loops
                        if code.count('\n', 0, loop.start()) + 1 == child['loop_lines'][-1])
            if re.search(r'\bnowait\b', last[0]):
                assert child['timing_end'] == 'parallel_join', child
            else:
                assert child['timing_end'] in ('construct_end', 'for_barrier'), child
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


# Without a log, validate generated metadata and source-defined merged groups.
def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('path', type=Path, nargs='?', help='SP log or a directory containing SP.log')
    parser.add_argument('--source-dir', type=Path, default=ROOT / 'SP/src')
    parser.add_argument('--manifest', type=Path, default=ROOT / '.build/SP.S/generated/instrumentation.json')
    args = parser.parse_args()
    table = check_sources(args.source_dir, args.manifest)
    parallel = sum(region['parent'] is None for region in table.values())
    print(f'PASS: {len(table)} automatic regions ({parallel} parallel, {len(table) - parallel} for)')
    if args.path:
        path = args.path / 'SP.log' if args.path.is_dir() else args.path
        result = check_report(path, table)
        print(f"PASS SP: {len(result['rows'])} rows; compiled labels, parents, combined equality, percentages")


if __name__ == '__main__':
    main()
