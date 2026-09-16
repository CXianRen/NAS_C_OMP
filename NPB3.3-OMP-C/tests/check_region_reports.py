#!/usr/bin/env python3
"""Check SP's static region table, manual hooks and parallel/for reports."""
import argparse
from pathlib import Path
import re

from report_region_times import parse_log

ROOT = Path(__file__).resolve().parents[1]
HOOK = re.compile(r'\b(PARALLEL|FOR)_(START|END)\(\s*&sp_control\s*,\s*(SP_[PF]_\w+)\s*\)')
FUNCTION = re.compile(r'^\s*(?:void|int|double)\s+(\w+)\s*\([^;{}]*\)\s*\{', re.MULTILINE)


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
    for path in directory.glob('*.c'):
        source = path.read_text()
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
        assert region['START'] < region['END'], key
        if region['parent'] is not None:
            owner = table[region['parent']]
            assert owner['parent'] is None and owner['file'] == region['file'], key
            assert owner['START'] < region['START'] < region['END'] < owner['END'], key
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
    return result


# Without a log, validate just the checked-in metadata and manual hook pairs.
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
