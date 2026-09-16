#!/usr/bin/env python3
"""Check direct SP builds, runtime tuner selection and manual-hook switches."""
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
    command = ['make', '-f', str(root / 'Makefile'), 'CLASS=S',
               '-j2', f'CC={args.cc}', f'NPB_DIR={source}',
               f'BUILD_DIR={directory}/build', f'BIN_DIR={directory}/bin',
               'PYTHON=/missing-python', 'CLANG=/missing-clang']
    env = dict(os.environ, NPB_TIME_REPORT='1', OMP_NUM_THREADS='4',
               OMP_DYNAMIC='false', OMP_PROC_BIND='false')
    for name in ('OMP_PLACES', 'KMP_AFFINITY', 'GOMP_CPU_AFFINITY'):
        env.pop(name, None)
    env.pop('NPB_NITER', None)
    for name in tuple(env):
        if name.startswith('OTTER_') or name == 'TUNER':
            env.pop(name)

    # Building must compile original SP files directly, without invoking generators.
    def make(flags='', instrument=1, selection=None):
        build_env = dict(env)
        if selection is not None:
            build_env['TUNER'] = selection
        process = subprocess.run(command + [f'CPPFLAGS={flags}', f'INSTRUMENT={instrument}'],
                                 cwd=root, env=build_env,
                                 capture_output=True, text=True)
        assert process.returncode == 0, process.stdout + process.stderr
        assert not re.search(r'ast-dump|instrument_regions\.py|instrumented/', process.stdout)
        assert not list(directory.rglob('instrumentation.json'))

    # Every configuration must retain the standard SP.S numerical result.
    def run_sp(flag, selection=None, report=True, verbose=None):
        run_env = dict(env, NPB_TIME_REPORT='1' if report else '0')
        if selection is not None:
            run_env['TUNER'] = selection
        if verbose is not None:
            run_env['OTTER_VERBOSE'] = str(verbose)
        process = subprocess.run([str(binary)], cwd=directory, env=run_env,
                                 capture_output=True, text=True, timeout=60)
        assert process.returncode == 0, process.stdout + process.stderr
        assert re.search(r'Verification\s*=\s*SUCCESSFUL', process.stdout), process.stdout
        assert f'build-flag={flag}' in process.stdout, process.stdout
        mode = (selection or 'none').lower()
        assert ('J2025 final region=' in process.stdout) == (mode == 'j2025'), process.stdout
        assert ('J2025_B final region=' in process.stdout) == (mode == 'j2025_b'), process.stdout
        otter_final = re.findall(r'^Otter final threads=\d+ placement=\S+ state=\S+',
                                 process.stdout, re.MULTILINE)
        assert len(otter_final) == int(mode == 'otter'), process.stdout
        assert process.stdout.count('Otter final ') == int(mode == 'otter'), process.stdout
        assert 'Otter final region=' not in process.stdout, process.stdout
        trace = mode == 'otter' and verbose != 0
        assert ('Otter search ' in process.stdout) == trace, process.stdout
        assert ('Otter step=' in process.stdout) == trace, process.stdout
        if trace:
            assert re.search(r'^Otter search .*unit: us$', process.stdout, re.MULTILINE), process.stdout
            assert 'Otter step=1 select state=' in process.stdout, process.stdout
            assert 'Otter step=1 sample state=' in process.stdout, process.stdout
        if 'time report' in process.stdout:
            log = directory / 'SP.log'
            log.write_text(process.stdout)
            check_report(log, check_sources(source / 'SP/src'))
        return process.stdout

    def reject_tuner(selection, message):
        process = subprocess.run([str(binary)], cwd=directory,
                                 env=dict(env, TUNER=selection),
                                 capture_output=True, text=True, timeout=60)
        output = process.stdout + process.stderr
        assert process.returncode != 0 and message in output, output

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
    for selection in ('dummy', 'j2025', 'j2025_b', 'otter'):
        reject_tuner(selection, 'INSTRUMENT')
    check_hook_calls(0)
    before = binary.stat().st_mtime_ns
    make(flags, instrument=0)
    assert before == binary.stat().st_mtime_ns, 'unchanged plain build compiled again'
    make(flags, instrument=1)
    assert before != binary.stat().st_mtime_ns and 'time report' in run_sp(1)
    check_hook_calls(1)
    print('PASS: instrumentation switches rebuild; disabled SP code has no hook calls', flush=True)

    # One binary contains all policies; the runtime environment does not rebuild it.
    before = binary.stat().st_mtime_ns
    for selection in (None, '', 'none', 'dummy', 'j2025', 'j2025_b', 'otter',
                      'DuMmY', 'J2025', 'J2025_B', 'J2025_b', 'OtTeR'):
        make(flags, selection=selection)
        assert before == binary.stat().st_mtime_ns, 'TUNER selection rebuilt the executable'
        assert 'time report' in run_sp(1, selection)
    symbols = subprocess.check_output(['nm', '-C', str(binary)], text=True)
    for symbol in ('tuner_attach', 'hams_binding_apply', 'dummy_select_cfg',
                   'j2025_create', 'j2025_select_cfg', 'j2025_observe', 'j2025_destroy',
                   'j2025_b_create', 'j2025_b_select_cfg', 'j2025_b_observe',
                   'j2025_b_destroy', 'otter_select_cfg'):
        assert symbol in symbols, symbol
    reject_tuner('ottre', 'TUNER')
    print('PASS: one SP.S binary supports default/none/dummy/J2025/J2025_B/Otter; invalid TUNER is rejected', flush=True)

    for selection in ('none', 'dummy', 'j2025', 'j2025_b', 'otter'):
        assert 'time report' not in run_sp(1, selection, report=False,
                                          verbose=1 if selection == 'otter' else None)
    assert 'time report' in run_sp(1, 'otter', verbose=0)
    assert before == binary.stat().st_mtime_ns
    print('PASS: Otter search logs and time reports switch independently; SP verifies', flush=True)
