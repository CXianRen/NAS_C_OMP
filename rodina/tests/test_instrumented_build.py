#!/usr/bin/env python3
"""Exercise all seven Rodinia CPU programs with the shared region/tuner framework."""
import argparse
import json
import math
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
PROGRAMS = {
    'hotspot': {'hotspot': (1, 1)},
    'streamcluster': {'sc_omp': (2, 2)},
    'particlefilter': {'particle_filter': (10, 8)},
    'cfd': {'euler3d_cpu': (5, 4), 'euler3d_cpu_double': (5, 4),
            'pre_euler3d_cpu': (6, 5), 'pre_euler3d_cpu_double': (6, 5)},
}


def command(args, cwd, **kwargs):
    process = subprocess.run(args, cwd=cwd, text=True, stdout=subprocess.PIPE,
                             stderr=subprocess.STDOUT, timeout=180, **kwargs)
    assert process.returncode == 0, f'{args} failed:\n{process.stdout}'
    return process.stdout


def check_report(text, metadata, count):
    total = re.search(r'^iteration total: ([\d.]+) s$', text, re.M)
    assert total and math.isfinite(float(total[1])) and float(total[1]) > 0, text
    rows = re.findall(r'^( *)(parallel|for) region (.*?)  ([\d.]+) s  step: ([\d.]+)%$', text, re.M)
    assert len(rows) == 2 * count, text
    sites = {r['label']: r for r in metadata['regions']}
    for index in range(0, len(rows), 2):
        parent, child = rows[index:index + 2]
        assert parent[0:2] == ('', 'parallel') and child[0:2] == ('    ', 'for'), rows
        assert parent[2:] == child[2:], rows
        assert sites[parent[2]]['kind'] == 'combined', rows
        assert math.isfinite(float(parent[3])) and float(parent[3]) >= 0, rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='gcc')
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--clang', default='clang-18')
    options = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='rodinia-framework-test-') as name:
        checkout = Path(name)
        ignore = shutil.ignore_patterns('__pycache__', 'build', '.build', '*.o')
        shutil.copytree(ROOT / 'framework', checkout / 'framework', ignore=ignore)
        rodina = checkout / 'rodina'
        rodina.mkdir()
        shutil.copy(ROOT / 'rodina/Makefile', rodina)
        shutil.copytree(ROOT / 'rodina/mk', rodina / 'mk')
        for benchmark in PROGRAMS:
            destination = rodina / benchmark
            destination.mkdir()
            for source in (ROOT / 'rodina' / benchmark).iterdir():
                if source.suffix in ('.c', '.cpp') or source.name in ('Makefile', 'makefile'):
                    shutil.copy(source, destination)

        # A uniform far-field cell has balanced normals and a known steady state.
        (checkout / 'mesh').write_text('1\n1.0\n-1 1 0 0\n-1 -1 0 0\n-1 0 1 0\n-1 0 -1 0\n')
        (checkout / 'temp').write_text(''.join(f'{80 + (i % 16)/16 + (i // 16)/32}\n' for i in range(256)))
        (checkout / 'power').write_text('500000\n' * 256)
        # Keep ParticleFilter's upstream time-based RNG reproducible without
        # changing its source or introducing an application-only seed API.
        fixed_time = checkout / 'fixed_time.c'
        fixed_time.write_text('#include <time.h>\ntime_t __wrap_time(time_t *out) {'
                              'time_t value = 1721000000; if (out) *out = value; return value;}\n')
        fixed_object = checkout / 'fixed_time.o'
        command([*shlex.split(options.cc), '-c', str(fixed_time), '-o', str(fixed_object)], checkout)
        build_command = ['make', '-j4', f'CC={options.cc}', f'CXX={options.cxx}', f'CLANG={options.clang}',
                         'CFLAGS=-O3 -ffast-math -fopenmp -std=c11 -DRODINIA_BUILD_TEST=1',
                         'CXXFLAGS=-O3 -fopenmp -std=c++11 -DRODINIA_BUILD_TEST=1',
                         f'LDFLAGS=-fopenmp -Wl,--wrap=time {fixed_object}']
        reference = {}
        metadata = {}

        def execute(benchmark, program, threads, instrument, tuner='none', report=True):
            directory = rodina / benchmark
            if benchmark == 'hotspot':
                args = ['16', '16', '3', str(threads), str(checkout / 'temp'), str(checkout / 'power'), 'temperature.out']
                files = ['temperature.out']
            elif benchmark == 'streamcluster':
                args = ['2', '4', '3', '128', '64', '128', 'unused', 'centers.out', str(threads)]
                files = ['centers.out']
            elif benchmark == 'particlefilter':
                args = ['-x', '16', '-y', '16', '-z', '3', '-np', '100']
                files = []
            else:
                args, files = [str(checkout / 'mesh')], ['density', 'momentum', 'density_energy']
            env = dict(os.environ, REGION_TIME_REPORT=str(int(report)), OMP_NUM_THREADS=str(threads),
                       OMP_DYNAMIC='false', OMP_PROC_BIND='false', TUNER=tuner,
                       OTTER_MAX_THREADS='4', OTTER_VERBOSE='1')
            env.pop('OFFLINE_CONFIG', None)
            text = command([str(directory / program), *args], directory, env=env)
            if instrument and report:
                check_report(text, metadata[program], PROGRAMS[benchmark][program][1])
            else:
                assert 'time report' not in text, text
            outputs = [tuple(float(token) for token in (directory / filename).read_text().split()) for filename in files]
            assert all(math.isfinite(value) for row in outputs for value in row), outputs
            if benchmark == 'hotspot':
                assert len(outputs[0]) == 512 and all(80 < x < 200 for x in outputs[0][1::2]), outputs
            elif benchmark == 'streamcluster':
                assert outputs and outputs[0] and len(outputs[0]) % 5 == 0, outputs
                assert 2 <= len(outputs[0]) // 5 <= 4, outputs
                rows = [outputs[0][i:i + 5] for i in range(0, len(outputs[0]), 5)]
                assert all(0 <= row[0] < 128 and row[1] > 0 for row in rows), outputs
                assert sum(row[1] for row in rows) == 128, outputs
                assert all(0 <= value <= 1 for row in rows for value in row[2:]), outputs
            elif benchmark == 'cfd':
                # The first two values are nel/nelr, followed by cell data.
                assert abs(outputs[0][2] - 1.4) < 1e-5, outputs
                assert all(abs(value) < 1e-6 for value in outputs[1][3:]), outputs
                assert outputs[2][2] > 2.5, outputs
            else:
                triples = re.findall(r'^XE:\s*(\S+)\nYE:\s*(\S+)\n(\S+)\n', text, re.M)
                estimates = [value for triple in triples for value in triple]
                assert len(estimates) == 6 and all(math.isfinite(float(v)) for v in estimates), text
                outputs = [tuple(map(float, estimates))]
            # Clustering can choose different centers after parallel floating
            # reductions change tie decisions. Compare its deterministic fixed
            # team across build modes; varying teams retain the invariants above.
            key = (program, threads) if benchmark == 'streamcluster' else program
            if benchmark == 'streamcluster' and tuner != 'none':
                return text
            if key not in reference:
                reference[key] = outputs
            else:
                # Floating reductions can change their last digits with team size.
                assert len(outputs) == len(reference[key]), program
                for actual, expected in zip(outputs, reference[key]):
                    assert len(actual) == len(expected), program
                    assert all(math.isclose(a, b, rel_tol=1e-5, abs_tol=1e-5) for a, b in zip(actual, expected)), (program, actual, expected)
            return text

        for stage, instrument in enumerate((1, 0, 1)):
            command(build_command + [f'INSTRUMENT={instrument}'], rodina)
            for benchmark, programs in PROGRAMS.items():
                for program, (static_count, _) in programs.items():
                    directory = rodina / benchmark
                    manifest = directory / 'build' / program / 'generated/instrumentation.json'
                    metadata[program] = json.loads(manifest.read_text())
                    assert len(metadata[program]['regions']) == static_count, program
                    standard = '-std=c11' if benchmark == 'particlefilter' else '-std=c++11'
                    assert standard in metadata[program]['compiler_args'], program
                    assert '-DRODINIA_BUILD_TEST=1' in metadata[program]['compiler_args'], program
                    assert metadata[program]['control'] == '&benchmark_control', program
                    for original in metadata[program]['sources']:
                        original = Path(original)
                        generated = manifest.parent / original.name
                        pragmas = lambda text: re.findall(r'^\s*(#pragma omp[^\n]*(?:\\\n[^\n]*)*)', text, re.M)
                        assert pragmas(original.read_text()) == pragmas(generated.read_text()), original
                    if not instrument:
                        symbols = command(['nm', '-u', str(directory / 'build' / program / 'program.o')], directory)
                        assert not re.search(r'\bregion_(?:parallel|for)_(?:start|end)\b', symbols), symbols
                    for threads in ((1, 4) if stage == 0 else (1,)):
                        execute(benchmark, program, threads, instrument)
            print(f'PASS: seven CPU programs INSTRUMENT={instrument}, stage={stage + 1}, numerical checks', flush=True)

        # Exercise region-level selection on every program, then the step-level
        # policies on Hotspot. All use the same initialized runtime and metadata.
        for benchmark, programs in PROGRAMS.items():
            for program in programs:
                execute(benchmark, program, 4, 1, tuner='dummy')
        for tuner in ('offline', 'j2025', 'j2025_b', 'otter'):
            text = execute('hotspot', 'hotspot', 4, 1, tuner=tuner, report=False)
            if tuner == 'otter':
                assert len(re.findall(r'^Otter step=\d+ sample ', text, re.M)) == 3, text
        # A missing generated manifest must be restored without clean.
        manifest = rodina / 'hotspot/build/hotspot/generated/instrumentation.json'
        manifest.unlink()
        command(build_command + ['INSTRUMENT=1'], rodina)
        assert manifest.is_file()
        print('PASS: shared tuner integration, reporting disabled while tuning, step samples and missing-manifest recovery', flush=True)


if __name__ == '__main__':
    main()
