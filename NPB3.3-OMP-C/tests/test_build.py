#!/usr/bin/env python3
"""Check direct SP compilation, reuse, source changes and manual-hook switches."""
import argparse
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile

from check_region_reports import check_report, check_sources

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', default='clang-18')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]

with tempfile.TemporaryDirectory(prefix='sp-build-test-') as temporary:
    directory = Path(temporary)
    source = directory / 'source'
    source.mkdir()
    shutil.copytree(root / 'SP', source / 'SP')
    for name in ('common', 'sys'):
        (source / name).symlink_to(root / name, target_is_directory=True)
    (directory / 'framework').symlink_to(root.parent / 'framework', target_is_directory=True)

    main = source / 'SP/src/sp.c'
    anchor = '  int i, niter, step, n3;'
    text = main.read_text()
    assert text.count(anchor) == 1
    main.write_text(text.replace(anchor, '''#ifdef TEST_BUILD_BRANCH
  puts("build-flag=1");
#else
  puts("build-flag=0");
#endif
''' + anchor))

    binary = directory / 'bin/SP.S'
    command = ['make', '-f', str(root / 'Makefile'), 'CLASS=S', 'TUNER=none',
               '-j2', f'CC={args.cc}', f'NPB_DIR={source}',
               f'BUILD_DIR={directory}/build', f'BIN_DIR={directory}/bin',
               'PYTHON=/missing-python', 'CLANG=/missing-clang']
    env = dict(os.environ, NPB_TIME_REPORT='1', OMP_NUM_THREADS='4',
               OMP_DYNAMIC='false', OMP_PROC_BIND='false')
    for name in ('OMP_PLACES', 'KMP_AFFINITY', 'GOMP_CPU_AFFINITY'):
        env.pop(name, None)
    env.pop('NPB_NITER', None)

    # Building must compile original SP files directly, without invoking generators.
    def make(flags='', instrument=1, j2025=0):
        process = subprocess.run(command + [f'CPPFLAGS={flags}', f'INSTRUMENT={instrument}',
                                            f'J2025_ENABLE={j2025}'],
                                 cwd=root, capture_output=True, text=True)
        assert process.returncode == 0, process.stdout + process.stderr
        assert not re.search(r'ast-dump|instrument_regions\.py|instrumented/', process.stdout)
        assert not list(directory.rglob('instrumentation.json'))

    # Every configuration must retain the standard SP.S numerical result.
    def run_sp(flag):
        process = subprocess.run([str(binary)], cwd=directory, env=env,
                                 capture_output=True, text=True, timeout=60)
        assert process.returncode == 0, process.stdout + process.stderr
        assert re.search(r'Verification\s*=\s*SUCCESSFUL', process.stdout), process.stdout
        assert f'build-flag={flag}' in process.stdout, process.stdout
        if 'time report' in process.stdout:
            log = directory / 'SP.log'
            log.write_text(process.stdout)
            check_report(log, check_sources(source / 'SP/src'))
        return process.stdout

    # Inspect calls in a real SP object: the shared runtime may retain unused APIs.
    def check_hook_calls(instrument):
        obj = directory / f'rhs-{instrument}.o'
        subprocess.run(shlex.split(args.cc) + [
            '-O0', '-fopenmp', f'-DREGION_INSTRUMENT={instrument}',
            '-I', str(directory / 'build/SP.S'), '-I', str(source / 'common'),
            '-I', str(source / 'SP/src'), '-c', str(source / 'SP/src/rhs.c'),
            '-o', str(obj)], check=True)
        symbols = subprocess.check_output(['nm', '-u', str(obj)], text=True)
        hooks = re.findall(r'\bregion_(?:parallel|for)_(?:start|end)\b', symbols)
        assert bool(hooks) == bool(instrument), symbols

    make()
    assert 'time report' in run_sp(0)
    before = binary.stat().st_mtime_ns
    make()
    assert before == binary.stat().st_mtime_ns, 'unchanged build compiled again'
    print('PASS: direct SP.S build, numerical verification and build reuse', flush=True)

    flags = '-DTEST_BUILD_BRANCH'
    make(flags)
    run_sp(1)
    assert before != binary.stat().st_mtime_ns
    print('PASS: CPPFLAGS changes rebuild the executable', flush=True)

    extra = source / 'SP/src/extra.c'
    extra.write_text('int extra_function(void) { return 1; }\n')
    make(flags)
    symbols = subprocess.check_output(['nm', str(binary)], text=True)
    assert re.search(r'\bextra_function$', symbols, re.MULTILINE)
    before = binary.stat().st_mtime_ns
    extra.unlink()
    make(flags)
    symbols = subprocess.check_output(['nm', str(binary)], text=True)
    assert not re.search(r'\bextra_function$', symbols, re.MULTILINE)
    assert before != binary.stat().st_mtime_ns
    print('PASS: source additions/removals update the executable', flush=True)

    before = binary.stat().st_mtime_ns
    make(flags, instrument=0)
    assert before != binary.stat().st_mtime_ns and 'time report' not in run_sp(1)
    check_hook_calls(0)
    before = binary.stat().st_mtime_ns
    make(flags, instrument=0)
    assert before == binary.stat().st_mtime_ns, 'unchanged plain build compiled again'
    make(flags, instrument=1)
    assert before != binary.stat().st_mtime_ns and 'time report' in run_sp(1)
    check_hook_calls(1)
    print('PASS: instrumentation switches rebuild; disabled SP code has no hook calls', flush=True)

    # Switching the tuner must relink without disabling the independent timing hooks.
    for enabled in (1, 0, 1):
        before = binary.stat().st_mtime_ns
        make(flags, j2025=enabled)
        assert before != binary.stat().st_mtime_ns
        output = run_sp(1)
        assert 'time report' in output and ('J2025 final region=' in output) == bool(enabled)
        symbols = subprocess.check_output(['nm', '-C', str(binary)], text=True)
        assert ('j2025_attach' in symbols) == bool(enabled)
        assert ('hams_binding_apply' in symbols) == bool(enabled)
        before = binary.stat().st_mtime_ns
        make(flags, j2025=enabled)
        assert before == binary.stat().st_mtime_ns, 'unchanged tuner build compiled again'
    print('PASS: J2025_ENABLE switches rebuild; disabled tuner retains timing without J2025/HAMS', flush=True)
