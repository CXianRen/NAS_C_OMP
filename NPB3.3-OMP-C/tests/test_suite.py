#!/usr/bin/env python3
"""Build and numerically verify all ten Class S applications with the framework."""
import argparse
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile


BENCHMARKS = ('BT', 'CG', 'DC', 'EP', 'FT', 'IS', 'LU', 'MG', 'SP', 'UA')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='clang-18')
    parser.add_argument('--clang', default='clang-18')
    parser.add_argument('--existing-bins', type=Path,
                        help='reuse an instrumented Class S bin directory')
    parser.add_argument('--existing-build', type=Path,
                        help='matching build directory containing manifests')
    parser.add_argument('--skip-plain', action='store_true',
                        help='skip the separate INSTRUMENT=0 build')
    args = parser.parse_args()
    assert bool(args.existing_bins) == bool(args.existing_build), 'provide both existing paths'
    root = Path(__file__).resolve().parents[1]
    env = dict(os.environ)
    for key in tuple(env):
        if key.startswith('OTTER_') or key.startswith('PHAMS_AUX_') or key in (
            'OMP_PLACES', 'KMP_AFFINITY', 'GOMP_CPU_AFFINITY', 'OMP_THREAD_LIMIT',
            'TUNER', 'OFFLINE_CONFIG', 'REGION_TIME_REPORT', 'NPB_NITER',
        ):
            env.pop(key, None)
    env.update(OMP_NUM_THREADS='4', OMP_PROC_BIND='false', OMP_DYNAMIC='false',
               OTTER_VERBOSE='0')

    with tempfile.TemporaryDirectory(prefix='nas-suite-test-') as temporary:
        directory = Path(temporary)

        def build(instrument):
            build_dir = directory / f'build-{instrument}'
            bins = directory / f'bin-{instrument}'
            command = ['make', '-j4', 'CLASS=S', f'CC={args.cc}', f'CLANG={args.clang}',
                       f'INSTRUMENT={instrument}', f'BUILD_DIR={build_dir}', f'BIN_DIR={bins}']
            process = subprocess.run(command, cwd=root, env=env, text=True,
                                     capture_output=True, timeout=600)
            assert process.returncode == 0, process.stdout + process.stderr
            return build_dir, bins

        if args.existing_bins:
            build_dir, bins = args.existing_build.resolve(), args.existing_bins.resolve()
        else:
            build_dir, bins = build(1)

        def run(bench, binaries=bins, report='1', tuner='none', config=None, verbose='0'):
            work = directory / f'{bench}-{tuner}-{report}-{len(list(directory.iterdir()))}'
            work.mkdir()
            run_env = dict(env, TUNER=tuner, REGION_TIME_REPORT=report, OTTER_VERBOSE=verbose)
            if config:
                run_env['OFFLINE_CONFIG'] = str(config)
            process = subprocess.run([str(binaries / f'{bench}.S')], cwd=work,
                                     env=run_env, text=True, capture_output=True, timeout=120)
            output = process.stdout + process.stderr
            (work / 'stdout.log').write_text(output)
            assert process.returncode == 0, f'{bench}/{tuner}: {output}'
            assert re.search(r'Verification\s*=\s*SUCCESSFUL', output), f'{bench}/{tuner}: {output}'
            return output

        for bench in BENCHMARKS:
            manifest = json.loads((build_dir / f'{bench}.S/generated/instrumentation.json').read_text())
            assert manifest['regions'], bench
            output = run(bench)
            assert 'time report' in output and '\nparallel region ' in output, output
            output = run(bench, report='0')
            assert 'time report' not in output, output
            print(f'PASS: {bench}.S numerical verification, generated metadata and report switch', flush=True)

        # Dynamic selection must retain the numerical result and receive samples.
        for bench in ('BT', 'CG', 'FT', 'IS', 'LU', 'MG', 'SP', 'UA'):
            run(bench, report='0', tuner='dummy')
            output = run(bench, report='0', tuner='otter', verbose='1')
            assert re.search(r'^Otter step=\d+ sample state=', output, re.MULTILINE), output
            assert 'time report' not in output, output
        print('PASS: dummy/Otter selection preserves eight iterative applications and sampling', flush=True)

        # DC partitions by the actual team after tuner selection, not max_threads.
        dc = json.loads((build_dir / 'DC.S/generated/instrumentation.json').read_text())
        region, = [item for item in dc['regions'] if item['parent'] == -1]
        config = directory / 'dc-offline.conf'
        config.write_text(f"{region['function']}:{region['line']};2;3\n")
        output = run('DC', tuner='offline', config=config)
        assert re.search(r'Number of Tasks\s*=\s*2\b', output), output
        print('PASS: DC partitions correctly when Offline changes four available threads to two', flush=True)

        if not args.skip_plain:
            plain_build, plain_bins = build(0)
            for bench in BENCHMARKS:
                manifest = json.loads((plain_build / f'{bench}.S/generated/instrumentation.json').read_text())
                assert manifest['regions'], bench
                output = run(bench, binaries=plain_bins)
                assert 'time report' not in output, output
            print('PASS: all ten INSTRUMENT=0 binaries verify, retain metadata and suppress reports', flush=True)
    print('PASS: NAS Class S suite')


if __name__ == '__main__':
    main()
