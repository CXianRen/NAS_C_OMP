#!/usr/bin/env python3
"""Check LULESH generation, numerical equivalence and incremental builds."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FRAMEWORK = ROOT.parent / 'framework'


def check_report(text, manifest):
    total = float(re.search(r'^iteration total: ([\d.eE+-]+) s$', text, re.M)[1])
    assert total > 0, text
    regions = manifest['regions']
    labels = {r['id']: f"{r['function']}:{r['line']}" + (' (nowait)' if r['nowait'] else '')
              for r in regions}
    expected = {}
    for region in regions:
        label = labels[region['id']]
        parent = region['parent']
        expected['parallel' if parent == -1 else 'for', label] = parent
        if region['kind'] == 'combined':
            expected['for', label] = region['id']
    rows = re.findall(r'^( *)(parallel|for) region (.+?)  ([\d.eE+-]+) s  step: ([\d.]+)%$', text, re.M)
    assert rows, text
    observed, current = {}, None
    for indent, kind, label, seconds, share in rows:
        key = kind, label
        assert key in expected and key not in observed, key
        value = float(seconds)
        assert abs(float(share) - 100 * value / total) < 0.01, (key, share)
        if kind == 'parallel':
            assert indent == '' and value <= total + 1e-9, key
            current = label
        else:
            assert indent == '    ' and current == labels[expected[key]], key
            assert value <= observed['parallel', current] + 1e-9, key
        observed[key] = value
    for region in regions:
        label = labels[region['id']]
        if region['kind'] == 'combined' and ('parallel', label) in observed:
            assert observed['parallel', label] == observed['for', label], label
    return observed


def run(command, **kwargs):
    result = subprocess.run(command, capture_output=True, text=True, timeout=240, **kwargs)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout


def numerical_result(text):
    keys = ('Iteration count', 'Final Origin Energy', 'MaxAbsDiff', 'TotalAbsDiff', 'MaxRelDiff')
    values = {key: re.search(r'^\s*' + key + r'\s*=\s*(\S+)', text, re.M)[1] for key in keys}
    assert values['Iteration count'] == '16', text
    for boundary in ('start', 'end'):
        assert [int(value) for value in re.findall(r'^test step_' + boundary + r'=(\d+)$', text, re.M)] == list(range(1, 17)), text
    # The uninstrumented ompt_v source gives this 8^3, 16-step reference.
    assert math.isclose(float(values['Final Origin Energy']), 9.693707e4, rel_tol=5e-7), text
    assert float(values['MaxRelDiff']) < 1e-10, text
    return values


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='clang')
    parser.add_argument('--cxx', default='clang++')
    parser.add_argument('--clang', default='clang')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='lulesh-auto-test-') as scratch:
        directory = Path(scratch)
        source = directory / 'lulesh'
        source.mkdir()
        for path in [ROOT / 'Makefile', *ROOT.glob('*.cc'), *ROOT.glob('*.h')]:
            shutil.copy2(path, source / path.name)
        # Observe manual application boundaries even after Otter stops logging
        # its completed search. These link wrappers are confined to this test.
        (source / 'test_steps.cc').write_text('''#include "benchmark.h"
#include <cstdio>
extern "C" void __real_step_start(region_control *, int);
extern "C" void __real_step_end(region_control *, int);
extern "C" void __wrap_step_start(region_control *control, int step) {
  std::printf("test step_start=%d\\n", step);
  __real_step_start(control, step);
}
extern "C" void __wrap_step_end(region_control *control, int step) {
  std::printf("test step_end=%d\\n", step);
  __real_step_end(control, step);
}
''')
        # Freeze shared sources for this test so concurrent development cannot
        # invalidate an otherwise unchanged-build assertion halfway through.
        shutil.copytree(FRAMEWORK, directory / 'framework',
                        ignore=shutil.ignore_patterns('build', '.build', 'ut', '__pycache__'))
        build = source / 'build'
        binary = build / 'lulesh2.0'
        manifest_path = build / 'generated/instrumentation.json'
        command = ['make', '-j4', '--trace', f'CC={args.cc}', f'CXX={args.cxx}',
                   f'CLANG={args.clang}', f'PYTHON={sys.executable}',
                   'LDFLAGS=-fopenmp -Wl,--wrap=step_start -Wl,--wrap=step_end']
        last_make_output = ''

        def make(*options):
            nonlocal last_make_output
            last_make_output = run(command + list(options), cwd=source)
            return last_make_output

        def execute(report='1', threads=4, tuner='none', config=None):
            env = dict(os.environ, OMP_NUM_THREADS=str(threads), OMP_DYNAMIC='false',
                       OMP_PROC_BIND='false', TUNER=tuner, OTTER_VERBOSE='1')
            for name in ('REGION_TIME_REPORT', 'OMP_PLACES', 'KMP_AFFINITY',
                         'GOMP_CPU_AFFINITY', 'OFFLINE_CONFIG', 'OMP_THREAD_LIMIT'):
                env.pop(name, None)
            for name in tuple(env):
                if name.startswith('OTTER_') and name != 'OTTER_VERBOSE':
                    env.pop(name)
            if report is not None:
                env['REGION_TIME_REPORT'] = report
            if config is not None:
                env['OFFLINE_CONFIG'] = str(config)
            return run([str(binary), '-s', '8', '-i', '16'], cwd=directory, env=env)

        def equivalent(actual, reference):
            assert actual['Iteration count'] == reference['Iteration count']
            for key in ('Final Origin Energy', 'MaxAbsDiff', 'TotalAbsDiff', 'MaxRelDiff'):
                assert math.isclose(float(actual[key]), float(reference[key]),
                                    rel_tol=1e-10, abs_tol=1e-8), (key, actual, reference)

        def stamps():
            return {str(p.relative_to(build)): p.stat().st_mtime_ns
                    for p in build.rglob('*') if p.is_file()}

        def unchanged(before, message):
            after = stamps()
            changed = sorted(name for name in before.keys() | after.keys()
                             if before.get(name) != after.get(name))
            assert not changed, (message,
                [(name, before.get(name), after.get(name)) for name in changed],
                last_make_output)

        originals = {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in source.glob('*.cc')}
        make('INSTRUMENT=1')
        manifest = json.loads(manifest_path.read_text())
        regions = manifest['regions']
        assert regions and any(r['nowait'] for r in regions), manifest
        for path in source.glob('*.cc'):
            assert hashlib.sha256(path.read_bytes()).hexdigest() == originals[path.name], path
            text = path.read_text()
            generated = (build / 'generated' / path.name).read_text()
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
            check_report(text, manifest)
        equivalent(results[1], results[4])
        for report in ('0', None):
            text = execute(report=report)
            assert 'time report' not in text and numerical_result(text) == results[4], text
        before = stamps()
        make('INSTRUMENT=1')
        unchanged(before, 'unchanged instrumented build rebuilt outputs')
        print('PASS: standalone LULESH + framework build, C++ coverage, pragmas, hierarchy, nowait and build reuse', flush=True)

        # All policies use one shared runtime; the environment never rebuilds.
        config = directory / 'offline.conf'
        config.write_text(''.join(f"{r['function']}:{r['line']};2;0x3\n"
                                  for r in regions if r['parent'] == -1))
        for tuner in ('dummy', 'j2025', 'j2025_b', 'otter', 'offline'):
            text = execute(tuner=tuner, config=config if tuner == 'offline' else None)
            equivalent(numerical_result(text), results[4])
            check_report(text, manifest)
            if tuner == 'j2025':
                assert 'J2025 final region=' in text, text
            elif tuner == 'j2025_b':
                assert 'J2025_B final region=' in text, text
            elif tuner == 'otter':
                assert 'Otter step=1 select ' in text and 'Otter step=1 sample ' in text, text
                assert text.count('Otter final ') == 1, text
            elif tuner == 'offline':
                assert 'Offline loaded ' in text and 'Offline missing ' not in text, text
            text = execute(report='0', tuner=tuner, config=config if tuner == 'offline' else None)
            assert 'time report' not in text, text
            equivalent(numerical_result(text), results[4])
        # Force 1 -> 4 transitions at the two force-assembly entry loops.
        # Their scratch/indexing path must follow initialization capacity,
        # rather than the team size left by the preceding region.
        force_functions = {'IntegrateStressForElems', 'CalcFBHourglassForceForElems'}
        first_force = {name: min(r['line'] for r in regions
                                if r['parent'] == -1 and r['function'] == name)
                       for name in force_functions}
        constraint_functions = {'CalcCourantConstraintForElems', 'CalcHydroConstraintForElems'}
        for wide_constraint in sorted(constraint_functions):
            switching = []
            for region in regions:
                if region['parent'] != -1:
                    continue
                full = (first_force.get(region['function']) == region['line'] or
                        region['function'] == wide_constraint)
                switching.append(f"{region['function']}:{region['line']};"
                                 + ('4;0xf\n' if full else '1;0x1\n'))
            config.write_text(''.join(switching))
            text = execute(tuner='offline', config=config)
            equivalent(numerical_result(text), results[4])
            for name, line in first_force.items():
                assert f'Offline config region={name}:{line} threads=4 mask=0xf' in text, text
        print('PASS: Offline 1 <-> 4 force/constraint transitions preserve scratch and reduction capacity', flush=True)
        unchanged(before, 'runtime tuner/report selection modified the build')
        print('PASS: all shared tuner choices, physical Otter steps and independent reporting', flush=True)

        make('INSTRUMENT=0')
        assert binary.stat().st_mtime_ns != before['lulesh2.0']
        assert json.loads(manifest_path.read_text())['regions'] == regions
        symbols = run(['nm', '-u', str(build / 'app/lulesh.o')])
        assert not re.search(r'\bregion_(?:parallel|for)_(?:start|end)\b', symbols), symbols
        for threads in (1, 4):
            text = execute(threads=threads)
            assert 'time report' not in text, text
            equivalent(numerical_result(text), results[threads])
        rejected = subprocess.run([str(binary), '-s', '8', '-i', '16'], cwd=directory,
            env=dict(os.environ, TUNER='dummy', OMP_NUM_THREADS='4', OMP_PROC_BIND='false'),
            capture_output=True, text=True, timeout=30)
        assert rejected.returncode != 0 and 'INSTRUMENT' in rejected.stderr, rejected.stderr
        before = stamps()
        make('INSTRUMENT=0')
        unchanged(before, 'unchanged plain build rebuilt outputs')
        make('INSTRUMENT=1')
        assert 'time report' in execute()
        print('PASS: 1 -> 0 -> 1 rebuilds; disabled hooks retain metadata; 1/4-thread numerical equivalence', flush=True)

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
        extra = source / 'added.cc'
        extra.write_text('void added_region() {\n#pragma omp parallel\n{ }\n}\n')
        make('INSTRUMENT=1', 'CPPFLAGS=-DLULESH_INSTRUMENT_TEST=0')
        assert len(json.loads(manifest_path.read_text())['regions']) == len(regions) + 1
        extra.unlink()
        make('INSTRUMENT=1', 'CPPFLAGS=-DLULESH_INSTRUMENT_TEST=0')
        assert not (build / 'generated/added.cc').exists()
        assert len(json.loads(manifest_path.read_text())['regions']) == len(regions)
        (build / 'generated/lulesh.cc').unlink()
        make('INSTRUMENT=1', 'CPPFLAGS=-DLULESH_INSTRUMENT_TEST=0')
        assert (build / 'generated/lulesh.cc').exists()
        equivalent(numerical_result(execute()), results[4])
        print('PASS: source-list changes and missing generated copies recover correctly', flush=True)
        make('clean')
        assert not build.exists(), 'clean left generated files behind'
        print('PASS: clean removes objects, binary, configuration and generated copies', flush=True)


if __name__ == '__main__':
    main()
