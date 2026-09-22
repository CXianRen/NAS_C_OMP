#!/usr/bin/env python3
"""Exercise C++ automatic instrumentation and runtime tuner selection."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='clang-18')
    parser.add_argument('--cxx', default='clang++-18')
    parser.add_argument('--clang', default='clang-18')
    args = parser.parse_args()
    root = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix='automatic-example-') as temporary:
        directory = Path(temporary)
        source = directory / 'example'
        source.mkdir()
        for name in ('main.cpp', 'Makefile'):
            shutil.copy2(root / name, source / name)
        (directory / 'framework').symlink_to(root.parent / 'framework', target_is_directory=True)
        binary = source / 'build/example'
        env = dict(os.environ)
        for key in tuple(env):
            if key.startswith('OTTER_') or key in (
                'OMP_PLACES', 'OMP_PROC_BIND', 'OMP_DYNAMIC', 'OMP_THREAD_LIMIT',
                'KMP_AFFINITY', 'GOMP_CPU_AFFINITY', 'OFFLINE_CONFIG', 'TUNER',
                'REGION_TIME_REPORT',
            ):
                env.pop(key, None)
        env.update(OMP_NUM_THREADS='4', OMP_PROC_BIND='false', OMP_DYNAMIC='false',
                   EXAMPLE_SIZE='32768', EXAMPLE_STEPS='12', OTTER_VERBOSE='0')

        def build(instrument=1):
            result = subprocess.run(
                ['make', '-j2', f'CC={args.cc}', f'CXX={args.cxx}',
                 f'CLANG={args.clang}', f'INSTRUMENT={instrument}'],
                cwd=source, env=env, text=True, capture_output=True, timeout=120)
            assert result.returncode == 0, result.stdout + result.stderr

        def run(tuner, instrument=True, report='1', verbose=None):
            run_env = dict(env, TUNER=tuner)
            if report is not None:
                run_env['REGION_TIME_REPORT'] = report
            if verbose is not None:
                run_env['OTTER_VERBOSE'] = str(verbose)
            result = subprocess.run([str(binary)], cwd=source,
                                    env=run_env, text=True,
                                    capture_output=True, timeout=30)
            assert result.returncode == 0, result.stdout + result.stderr
            assert 'example=PASS' in result.stdout, result.stdout
            enabled = instrument and report in ('1', 'true', 'yes', 'on')
            assert ('time report' in result.stdout) == enabled, result.stdout
            if enabled:
                assert result.stdout.count('\nparallel region ') == 3, result.stdout
                assert result.stdout.count('\n    for region ') == 3, result.stdout
            if tuner == 'otter' and verbose == 1:
                assert re.search(r'^Otter step=\d+ sample state=',
                                 result.stdout, re.MULTILINE), result.stdout
                assert result.stdout.count('Otter final ') == 1, result.stdout
            return result.stdout

        original = (source / 'main.cpp').read_bytes()
        build()
        manifest_path, = source.glob('build/**/instrumentation.json')
        manifest = json.loads(manifest_path.read_text())
        regions = manifest['regions']
        assert len(regions) == 4 and sum(r['nowait'] for r in regions) == 1, regions
        config = directory / 'offline.conf'
        config.write_text(''.join(f"{r['function']}:{r['line']};2;3\n"
                                  for r in regions if r['parent'] == -1))
        env['OFFLINE_CONFIG'] = str(config)
        before = binary.stat().st_mtime_ns
        build()
        assert binary.stat().st_mtime_ns == before, 'unchanged build rebuilt C++ example'
        for tuner in ('none', 'dummy', 'offline', 'j2025', 'j2025_b', 'otter'):
            run(tuner)
        # Framework reporting changes output only; tuner samples still arrive.
        for report in (None, '0', 'false', 'invalid', 'TRUE', '1', 'true', 'yes', 'on'):
            run('otter', report=report, verbose=1)
        assert binary.stat().st_mtime_ns == before, 'runtime selection changed binary'

        # A missing generated source must be recreated, not ignored by Make.
        generated = manifest_path.parent / 'main.cpp'
        generated.unlink()
        build()
        assert generated.is_file()
        run('none')

        build(0)
        for report in ('1', 'true', 'yes', 'on'):
            run('none', instrument=False, report=report)
        disabled = subprocess.run([str(binary)], cwd=source, env=dict(env, TUNER='dummy'),
                                  text=True, capture_output=True, timeout=30)
        assert disabled.returncode != 0 and 'INSTRUMENT=1' in disabled.stderr, disabled
        build(1)
        run('otter')
        assert (source / 'main.cpp').read_bytes() == original, 'build rewrote original source'
    print('PASS: C++ automatic regions, all six tuner modes, Offline pragma keys,')
    print('      framework report environment switch with continued tuner samples,')
    print('      generated-source recovery, build reuse and INSTRUMENT=1 -> 0 -> 1')


if __name__ == '__main__':
    main()
