#!/usr/bin/env python3
"""Check LULESH generation, numerical equivalence and incremental builds."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
NPB = ROOT.parent / 'NPB3.3-OMP-C'
sys.dont_write_bytecode = True
sys.path.insert(0, str(NPB / 'tests'))
from check_region_reports import check_report


def run(command, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, timeout=180, **kwargs)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


def numerical_result(text):
    keys = ('Iteration count', 'Final Origin Energy', 'MaxAbsDiff', 'TotalAbsDiff', 'MaxRelDiff')
    values = {key: re.search(r'^\s*' + key + r'\s*=\s*(\S+)', text, re.M)[1] for key in keys}
    assert values['Iteration count'] == '16', text
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--cxx', default='clang++')
    parser.add_argument('--clang', default='clang')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='lulesh-auto-test-') as scratch:
        directory = Path(scratch)
        source = directory / 'LULESH'
        source.mkdir()
        for path in [ROOT / 'Makefile', *ROOT.glob('*.cc'), *ROOT.glob('*.h')]:
            shutil.copy2(path, source / path.name)
        (directory / 'NPB3.3-OMP-C').symlink_to(NPB, target_is_directory=True)
        build = source / 'build'
        binary = build / 'lulesh2.0'
        manifest_path = build / 'instrumented/instrumentation.json'
        command = ['make', '-j4', f'CC={args.cc}', f'CXX={args.cxx}',
                   f'CLANG={args.clang}', f'PYTHON={sys.executable}']

        def make(*options):
            return run(command + list(options), cwd=source)

        def execute(report='1', threads=4):
            env = dict(os.environ, OMP_NUM_THREADS=str(threads), OMP_DYNAMIC='false')
            env.pop('NPB_TIME_REPORT', None)
            if report is not None:
                env['NPB_TIME_REPORT'] = report
            return run([str(binary), '-s', '8', '-i', '16'], cwd=directory, env=env)

        def stamps():
            return {str(p.relative_to(build)): p.stat().st_mtime_ns
                    for p in build.rglob('*') if p.is_file()}

        originals = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in source.glob('*.cc')}
        make('INSTRUMENT=1')
        manifest = json.loads(manifest_path.read_text())
        regions = manifest['regions']
        assert regions and any(r['nowait'] for r in regions), manifest
        for path in source.glob('*.cc'):
            assert hashlib.sha256(path.read_bytes()).hexdigest() == originals[path.name], path
            text = path.read_text()
            generated = (build / 'instrumented' / path.name).read_text()
            # All literal LULESH sites must be represented, including every
            # original loop folded into a nowait group. No pragma may change.
            pragmas = re.findall(r'^\s*(#pragma omp[^\n]*(?:\\\n[^\n]*)*)', text, re.M)
            assert pragmas == re.findall(r'^\s*(#pragma omp[^\n]*(?:\\\n[^\n]*)*)', generated, re.M)
            expected = {i for i, line in enumerate(text.splitlines(), 1)
                        if re.match(r'\s*#pragma omp (?:parallel|for)\b', line)}
            covered = {line for r in regions if r['file'] == str(path) for line in r['loop_lines']}
            assert covered == expected, (path, expected - covered, covered - expected)

        results = {}
        for threads in (1, 4):
            text = execute(threads=threads)
            results[threads] = numerical_result(text)
            log = directory / 'LULESH.log'
            log.write_text(text)
            check_report(log, manifest_path)
        for report in ('0', None):
            text = execute(report=report)
            assert 'time report' not in text and numerical_result(text) == results[4], text
        before = stamps()
        make('INSTRUMENT=1')
        assert stamps() == before, 'unchanged instrumented build rebuilt outputs'
        print('PASS: complete C++ site coverage, original pragmas, report hierarchy, nowait and build reuse', flush=True)

        make('INSTRUMENT=0', 'CLANG=false', 'PYTHON=false')
        assert binary.stat().st_mtime_ns != before['lulesh2.0']
        symbols = run(['nm', str(binary)])
        assert not re.search(r'\bnpb_(?:time_\w+|regions|region_count)$', symbols, re.M), symbols
        for threads in (1, 4):
            text = execute(threads=threads)
            assert 'time report' not in text and numerical_result(text) == results[threads], text
        before = stamps()
        make('INSTRUMENT=0', 'CLANG=false', 'PYTHON=false')
        assert stamps() == before, 'unchanged plain build rebuilt outputs'
        make('INSTRUMENT=1')
        assert 'time report' in execute()
        print('PASS: 1 -> 0 -> 1 rebuilds; plain mode skips Python/Clang parsing; 1/4-thread numerical equivalence', flush=True)

        # Add a conditional region in a temporary source to check source/header
        # dependencies and parser/compiler flags against observable metadata.
        probe = source / 'lulesh-util.cc'
        with probe.open('a') as stream:
            stream.write('\n#if LULESH_INSTRUMENT_TEST\nvoid instrumentation_probe() {\n'
                         '#pragma omp parallel for\nfor (int i = 0; i < 4; ++i) {}\n}\n#endif\n')
        old_stamp = manifest_path.stat().st_mtime_ns
        make('INSTRUMENT=1')
        assert manifest_path.stat().st_mtime_ns != old_stamp
        assert len(json.loads(manifest_path.read_text())['regions']) == len(regions)
        header = source / 'lulesh.h'
        with header.open('a') as stream:
            stream.write('\n#ifndef LULESH_INSTRUMENT_TEST\n#define LULESH_INSTRUMENT_TEST 1\n#endif\n')
        make('INSTRUMENT=1')
        assert len(json.loads(manifest_path.read_text())['regions']) == len(regions) + 1
        make('INSTRUMENT=1', 'CPPFLAGS=-DLULESH_INSTRUMENT_TEST=0')
        changed = json.loads(manifest_path.read_text())
        assert '-DLULESH_INSTRUMENT_TEST=0' in changed['compiler_args']
        assert len(changed['regions']) == len(regions)
        print('PASS: source/header changes regenerate; CPPFLAGS selects the same branch in parser and compiler', flush=True)
        make('clean')
        assert not build.exists(), 'clean left generated files behind'
        print('PASS: clean removes objects, binary, configuration and generated copies', flush=True)


if __name__ == '__main__':
    main()
