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
parser.add_argument('--clang', default='clang-18', help='Clang used to generate common region metadata')
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
    # All tuners, including none, see the complete control table before attach
    # and before any initialization/warmup region has executed.
    anchor = '  tuner *runtime = tuner_attach(&sp_control);'
    text = main.read_text()
    assert text.count(anchor) == 1
    main.write_text(text.replace(anchor, r'''  for (int metadata_id = 0; metadata_id < SP_REGION_COUNT; ++metadata_id) {
    const region_info *info = &sp_control.regions[metadata_id];
    if (!info->name || !info->file || info->line <= 0) abort();
    printf("metadata id=%d name=%s file=%s line=%d parent=%d combined=%d nowait=%d\n",
           metadata_id, info->name, info->file, info->line,
           info->parent, info->combined, info->nowait);
  }
''' + anchor))

    binary = directory / 'bin/SP.S'
    command = ['make', '-f', str(root / 'Makefile'), 'CLASS=S',
               '-j2', f'CC={args.cc}', f'NPB_DIR={source}',
               f'BUILD_DIR={directory}/build', f'BIN_DIR={directory}/bin',
               f'CLANG={args.clang}']
    env = dict(os.environ, NPB_TIME_REPORT='1', OMP_NUM_THREADS='4',
               OMP_DYNAMIC='false', OMP_PROC_BIND='false')
    for name in ('OMP_PLACES', 'KMP_AFFINITY', 'GOMP_CPU_AFFINITY'):
        env.pop(name, None)
    env.pop('NPB_NITER', None)
    for name in tuple(env):
        if name.startswith('OTTER_') or name in ('TUNER', 'OFFLINE_CONFIG'):
            env.pop(name)

    # The AST generator reads existing hooks; SP files are compiled without rewriting.
    def make(flags='', instrument=1, selection=None, config=None):
        build_env = dict(env)
        if selection is not None:
            build_env['TUNER'] = selection
        if config is not None:
            build_env['OFFLINE_CONFIG'] = str(config)
        process = subprocess.run(command + [f'CPPFLAGS={flags}', f'INSTRUMENT={instrument}'],
                                 cwd=root, env=build_env,
                                 capture_output=True, text=True)
        assert process.returncode == 0, process.stdout + process.stderr
        assert not re.search(r'instrument_regions\.py|instrumented/', process.stdout)
        assert not list(directory.rglob('instrumentation.json'))

    metadata = directory / 'build/SP.S/region_metadata.h'

    def metadata_names():
        entries = re.findall(
            r'^#define REGION_INFO_(SP_[PF]_\w+)\(parent, combined, nowait\) '
            r'\{ "([^"]+)", \(parent\), \(combined\), "([^"]+)", (\d+), \(nowait\) \}',
            metadata.read_text(), re.MULTILINE)
        table = check_sources(source / 'SP/src')
        assert {entry[0] for entry in entries} == set(table), metadata.read_text()
        names = []
        for key, name, file, line in entries:
            expected = table[key]
            assert name == expected['function'] and int(line) == expected['line'], (key, entries)
            assert Path(file).resolve() == expected['file'].resolve(), (key, file)
            names.append(f'{name}:{line}')
        return names

    def check_startup_metadata(output):
        entries = re.findall(
            r'^metadata id=(\d+) name=(\w+) file=(.+) line=(\d+) '
            r'parent=(-?\d+) combined=(\d+) nowait=(\d+)$', output, re.MULTILINE)
        table = check_sources(source / 'SP/src')
        assert len(entries) == len(table), output
        ids = {key: index for index, key in enumerate(table)}
        for index, (entry, (key, expected)) in enumerate(zip(entries, table.items())):
            region_id, name, file, line, parent, combined, nowait = entry
            assert int(region_id) == index and name == expected['function'], entry
            assert int(line) == expected['line'], entry
            assert Path(file).resolve() == expected['file'].resolve(), entry
            assert int(parent) == (-1 if expected['parent'] is None else ids[expected['parent']]), entry
            assert int(combined) == expected['combined'] and int(nowait) == expected['nowait'], entry

    def parallel_names():
        return sorted(region['label'] for region in check_sources(source / 'SP/src').values()
                      if region['parent'] is None)

    # Every configuration must retain the standard SP.S numerical result.
    def run_sp(flag, selection=None, report=True, verbose=None, config=None):
        run_env = dict(env, NPB_TIME_REPORT='1' if report else '0')
        if selection is not None:
            run_env['TUNER'] = selection
        if verbose is not None:
            run_env['OTTER_VERBOSE'] = str(verbose)
        if config is not None:
            run_env['OFFLINE_CONFIG'] = str(config)
        process = subprocess.run([str(binary)], cwd=directory, env=run_env,
                                 capture_output=True, text=True, timeout=60)
        assert process.returncode == 0, process.stdout + process.stderr
        assert re.search(r'Verification\s*=\s*SUCCESSFUL', process.stdout), process.stdout
        assert f'build-flag={flag}' in process.stdout, process.stdout
        check_startup_metadata(process.stdout)
        mode = (selection or 'none').lower()
        assert ('Offline ' in process.stdout) == (mode == 'offline'), process.stdout
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

    def reject_tuner(selection, message, config=None):
        run_env = dict(env, TUNER=selection)
        if config is not None:
            run_env['OFFLINE_CONFIG'] = str(config)
        process = subprocess.run([str(binary)], cwd=directory,
                                 env=run_env,
                                 capture_output=True, text=True, timeout=60)
        output = process.stdout + process.stderr
        assert process.returncode != 0 and message in output, output
        assert not re.search(r'Time step\s+1\b', output), output
        assert 'Offline loaded ' not in output, output

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
    assert len(metadata_names()) == 32, metadata.read_text()
    assert 'time report' in run_sp(0)
    before = binary.stat().st_mtime_ns
    make()
    assert before == binary.stat().st_mtime_ns, 'unchanged build compiled again'
    print('PASS: direct SP.S build, exact shared compile-time metadata, numerical verification and build reuse', flush=True)

    flags = '-DTEST_BUILD_BRANCH'
    make(flags)
    run_sp(1)
    assert before != binary.stat().st_mtime_ns
    print('PASS: CPPFLAGS changes rebuild the executable', flush=True)

    extra = source / 'SP/src/extra.c'
    region_ids = source / 'SP/src/sp_regions.h'
    region_table = source / 'SP/src/sp_regions.c'
    original_ids, original_table = region_ids.read_text(), region_table.read_text()
    region_ids.write_text(original_ids.replace('  SP_REGION_COUNT', '  SP_P_UNUSED,\n  SP_REGION_COUNT'))
    region_table.write_text(original_table.replace('\n};',
        '\n  [SP_P_UNUSED] = REGION_INFO(SP_P_UNUSED, -1, 0, 0),\n};'))
    extra.write_text('#include "sp_regions.h"\n'
                     'int extra_function(void) { return 1; }\n'
                     'void unused_region(void) {\n'
                     '  PARALLEL_START(&sp_control, SP_P_UNUSED);\n'
                     '  #pragma omp parallel\n'
                     '  { }\n'
                     '  PARALLEL_END(&sp_control, SP_P_UNUSED);\n'
                     '}\n')
    make(flags)
    symbols = subprocess.check_output(['nm', str(binary)], text=True)
    assert re.search(r'\bextra_function$', symbols, re.MULTILINE)
    assert 'unused_region:4' in metadata_names(), metadata.read_text()
    config = directory / 'offline.conf'
    config.write_text('unused_region:4;2;0x3\n')
    assert 'Offline config region=unused_region:4' in run_sp(1, 'offline', config=config)
    before = binary.stat().st_mtime_ns
    extra.unlink()
    region_ids.write_text(original_ids)
    region_table.write_text(original_table)
    make(flags)
    symbols = subprocess.check_output(['nm', str(binary)], text=True)
    assert not re.search(r'\bextra_function$', symbols, re.MULTILINE)
    assert 'unused_region:4' not in metadata_names(), metadata.read_text()
    reject_tuner('offline', 'unknown region=unused_region:4', config=config)
    assert before != binary.stat().st_mtime_ns
    print('PASS: source additions/removals refresh shared metadata; compiled unexecuted regions are valid', flush=True)

    before = binary.stat().st_mtime_ns
    make(flags, instrument=0)
    assert len(metadata_names()) == 32, metadata.read_text()
    assert before != binary.stat().st_mtime_ns and 'time report' not in run_sp(1)
    for selection in ('dummy', 'j2025', 'j2025_b', 'otter', 'offline'):
        reject_tuner(selection, 'INSTRUMENT')
    check_hook_calls(0)
    before = binary.stat().st_mtime_ns
    make(flags, instrument=0)
    assert before == binary.stat().st_mtime_ns, 'unchanged plain build compiled again'
    make(flags, instrument=1)
    assert len(metadata_names()) == 32, metadata.read_text()
    assert before != binary.stat().st_mtime_ns and 'time report' in run_sp(1)
    check_hook_calls(1)
    print('PASS: all tuners see metadata before attach; disabled hooks retain the same complete metadata', flush=True)

    # One binary contains all policies; the runtime environment does not rebuild it.
    before = binary.stat().st_mtime_ns
    for selection in (None, '', 'none', 'dummy', 'j2025', 'j2025_b', 'otter', 'offline',
                      'DuMmY', 'J2025', 'J2025_B', 'J2025_b', 'OtTeR', 'OffLiNe'):
        make(flags, selection=selection)
        assert before == binary.stat().st_mtime_ns, 'TUNER selection rebuilt the executable'
        assert 'time report' in run_sp(1, selection)
    symbols = subprocess.check_output(['nm', '-C', str(binary)], text=True)
    for symbol in ('tuner_attach', 'hams_binding_apply', 'dummy_select_cfg',
                   'j2025_create', 'j2025_select_cfg', 'j2025_observe', 'j2025_destroy',
                   'j2025_b_create', 'j2025_b_select_cfg', 'j2025_b_observe',
                   'j2025_b_destroy', 'otter_select_cfg', 'offline_select_cfg',
                   'sp_regions'):
        assert symbol in symbols, symbol
    assert 'offline_region_names' not in symbols and 'offline_region_count' not in symbols
    reject_tuner('ottre', 'TUNER')
    print('PASS: one SP.S binary supports all tuner choices; invalid TUNER is rejected', flush=True)

    for selection in ('none', 'dummy', 'j2025', 'j2025_b', 'otter', 'offline'):
        assert 'time report' not in run_sp(1, selection, report=False,
                                          verbose=1 if selection == 'otter' else None)
    assert 'time report' in run_sp(1, 'otter', verbose=0)
    assert before == binary.stat().st_mtime_ns
    print('PASS: Otter search logs and time reports switch independently; SP verifies', flush=True)

    # Offline files are loaded at attach time and never become build inputs.
    names = parallel_names()
    partial = f'{names[0]};2;0x5\n'
    config.write_text(partial)
    make(flags, selection='offline', config=config)
    assert before == binary.stat().st_mtime_ns, 'OFFLINE_CONFIG rebuilt the executable'
    output = run_sp(1, 'offline', config=config)
    assert f'Offline loaded path={config} entries=1' in output, output
    assert f'Offline config region={names[0]} threads=2 mask=0x5' in output, output
    assert f'Offline missing region={names[0]} ' not in output, output
    executed = set(re.findall(r'^parallel region (\S+)', output, re.MULTILINE))
    assert names[0] in executed, output
    for name in executed - {names[0]}:
        assert output.count(f'Offline missing region={name} threads=4 mask=0xf') == 1, output
    assert output.count('Offline missing ') == len(executed) - 1, output

    config.write_text(''.join(f'{name};2;3\n' for name in names))
    make(flags, selection='offline', config=config)
    assert before == binary.stat().st_mtime_ns, 'config contents rebuilt the executable'
    for report in (True, False):
        output = run_sp(1, 'OffLiNe', config=config, report=report)
        assert output.count('Offline config region=') == len(names), output
        assert 'Offline missing ' not in output, output
        assert ('time report' in output) == report, output
    for path in ('', directory / 'not-found.conf'):
        output = run_sp(1, 'offline', config=path)
        assert 'Offline fallback ' in output and 'threads=4 mask=0xf' in output, output
        assert 'Offline missing ' not in output, output
    print('PASS: Offline partial/full files, report switch, fallback and config changes; SP verifies', flush=True)

    # Reject the whole file before any formal iteration or loaded/config success log.
    invalid_configs = (
        ('no_such_region:1;2;3\n', 'unknown region='),
        (f'{names[0]};2\n', 'expected region_name:line;thread_number;hex_mask'),
        (partial + partial, 'duplicate region='),
        (f'{names[0]};2;1\n', 'mask bit count'),
        (f'{names[0]};0;0\n', 'positive decimal'),
        (f'{names[0]};5;1f\n', 'exceeds max_threads=4'),
        (f'{names[0]};1;0x100000000000000000000000000000000\n', 'exceeds HAMS_CPU_COUNT='),
    )
    for content, message in invalid_configs:
        config.write_text(content)
        reject_tuner('offline', message, config=config)
    assert before == binary.stat().st_mtime_ns
    print('PASS: invalid Offline names, format, duplicates, masks and thread limits fail at attach', flush=True)

    # Source line changes must invalidate old keys, even without changing hook spelling.
    rhs = source / 'SP/src/rhs.c'
    old_source = rhs.read_text()
    old_name = next(name for name in names if name.startswith('compute_rhs:'))
    rhs.write_text('\n' + old_source)
    make(flags)
    new_name = next(name for name in parallel_names() if name.startswith('compute_rhs:'))
    assert old_name != new_name and old_name not in metadata_names(), metadata.read_text()
    assert new_name in metadata_names(), metadata.read_text()
    config.write_text(f'{old_name};2;3\n')
    reject_tuner('offline', f'unknown region={old_name}', config=config)
    config.write_text(f'{new_name};2;3\n')
    run_sp(1, 'offline', config=config)
    print('PASS: source line changes regenerate names and reject stale Offline keys before execution', flush=True)
