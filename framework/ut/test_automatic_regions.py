#!/usr/bin/env python3
"""Validate source-generated regions against synchronization and callback contracts."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile
import unittest

CONTROL = Path(__file__).resolve().parents[1] / 'region_control'
GENERATOR = CONTROL / 'instrument_regions.py'

# A worker waits for an action after a nowait loop. An added profiling barrier
# deadlocks; a prematurely closed interval misses the worker's logical time.
SYNCHRONIZATION = r'''#define _POSIX_C_SOURCE 200809L
#include "region_control.h"
#include "region_auto.h"
#include <assert.h>
#include <omp.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static region_control control;
static atomic_int released, phase;
static int clock_calls, starts, ends, current_id;
double __wrap_omp_get_wtime(void) {
  assert(omp_get_thread_num() == 0);
  return atomic_load(&phase) + 0.001 * ++clock_calls;
}
static void worker(void) {
  while (!atomic_load(&released)) sched_yield();
  struct timespec delay = {0, 1000000};
  nanosleep(&delay, 0);
  atomic_fetch_add(&phase, 1);
}
static void select_region(void *context, int id) {
  assert(context == &control && !omp_in_parallel());
  assert(starts == ends);
  current_id = id;
  ++starts;
  atomic_fetch_add(&phase, 1000); /* Selection must precede the sample. */
}
static void observe_region(void *context, int id, double seconds) {
  assert(context == &control && !omp_in_parallel());
  assert(starts == ends + 1 && id == current_id);
  assert(seconds > 0 && seconds < 50);
  ++ends;
  atomic_fetch_add(&phase, 1000); /* Observation must follow the sample. */
}
static void tail(void) {
  atomic_store(&released, 0);
  #pragma omp parallel
  {
    #pragma omp for schedule(static, 1) nowait
    for (int i = 0; i < 2; ++i) if (i == 1) worker();
    #pragma omp for schedule(static, 1) nowait
    for (int i = 0; i < 2; ++i) if (i == 1) worker();
    #pragma omp master
    atomic_store(&released, 1);
  }
}
static void next_for(void) {
  atomic_store(&released, 0);
  #pragma omp parallel
  {
    #pragma omp for schedule(static, 1) nowait
    for (int i = 0; i < 2; ++i) if (i == 1) worker();
    #pragma omp for schedule(static, 1) nowait
    for (int i = 0; i < 2; ++i) if (i == 1) worker();
    #pragma omp for schedule(static, 1)
    for (int i = 0; i < 2; ++i) {
      if (i == 0) atomic_store(&released, 1);
      else worker();
    }
    #pragma omp for schedule(static, 1)
    for (int i = 0; i < 2; ++i) if (i == 0) atomic_fetch_add(&phase, 1);
  }
}
static void explicit_sync(void) {
  atomic_store(&released, 0);
  #pragma omp parallel
  {
    #pragma omp for schedule(static, 1) nowait
    for (int i = 0; i < 2; ++i) if (i == 1) worker();
    #pragma omp master
    atomic_store(&released, 1);
    #pragma omp barrier
    #pragma omp master
    atomic_fetch_add(&phase, 10);
  }
}
static void conditional(int execute) {
  if (execute)
    #pragma omp parallel
    { }
}
static void combined(void) {
  int sum = 0;
  #pragma omp parallel for reduction(+:sum)
  for (int i = 0; i < 8; ++i) sum += i;
  assert(sum == 28);
}
int main(int argc, char **argv) {
  (void)argv;
  int report = argc == 1;
  omp_set_dynamic(0);
  omp_set_num_threads(2);
  region_control_init(&control, region_auto_info, REGION_AUTO_COUNT);
  assert(control.enabled == (REGION_INSTRUMENT && report));
  for (int id = 0; id < REGION_AUTO_COUNT; ++id) {
    assert(control.regions[id].name && control.regions[id].file);
    assert(control.regions[id].line > 0);
  }
  const region_control_callbacks callbacks = {
    0, select_region, observe_region, 0, 0
  };
  region_control_register(&control, &callbacks, &control);
  combined(); /* Warmup must neither sample nor tune. */
  assert(starts == 0 && ends == 0 && clock_calls == 0);
  iteration_start(&control);
  step_start(&control, 1);
  tail();
  tail();
  next_for();
  next_for();
  explicit_sync();
  conditional(1);
  conditional(0);
  combined();
  iteration_end(&control);
  assert(starts == (REGION_INSTRUMENT ? 7 : 0) && ends == starts);
  assert(atomic_load(&phase) == 23 + 2000 * starts);
  if (!REGION_INSTRUMENT) assert(clock_calls == 2);
  int found = 0;
  for (int id = 0; id < REGION_AUTO_COUNT; ++id) {
    const region_info *info = &control.regions[id];
    if (info->parent == -1) continue;
    ++found;
    double seconds = control.elapsed[id];
    if (!report || !REGION_INSTRUMENT) {
      assert(seconds == 0);
    } else if (!strcmp(info->name, "tail")) {
      assert(seconds >= 4 && seconds < 5);
    } else if (!strcmp(info->name, "next_for")) {
      assert(seconds >= (info->nowait ? 6 : 2));
      assert(seconds < (info->nowait ? 7 : 3));
    } else {
      assert(!strcmp(info->name, "explicit_sync"));
      assert(seconds >= 1 && seconds < 2); /* Excludes post-barrier +10. */
    }
  }
  assert(found == 4);
  region_report(&control);
  puts("automatic_regions=PASS");
}
'''


class AutomaticRegions(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='automatic region test ')
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.output = self.directory / 'generated'
        self.depfile = self.directory / 'instrumentation.d'

    def source(self, name, text):
        path = self.directory / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)
        return path

    def generate(self, sources, success=True, flags=()):
        process = subprocess.run([sys.executable, str(GENERATOR), *map(str, sources),
            '--clang', OPTIONS.clang, '--output', str(self.output),
            '--control', '&control', '--depfile', str(self.depfile),
            '--', '-I', str(CONTROL), *flags], capture_output=True, text=True)
        if success:
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
            return json.loads((self.output / 'instrumentation.json').read_text())
        self.assertNotEqual(process.returncode, 0, process.stdout + process.stderr)
        return process.stderr

    def compile(self, sources, instrument=1, extra=()):
        binary = self.directory / f'program-{instrument}'
        process = subprocess.run(shlex.split(OPTIONS.cc) + [
            '-O2', '-std=c11', '-fopenmp', '-Wall', '-Wextra', '-Werror', '-UNDEBUG',
            f'-DREGION_INSTRUMENT={instrument}', '-I', str(CONTROL), '-I', str(self.output),
            '-I', str(self.directory), *map(str, sources), str(self.output / 'region_auto.c'),
            str(CONTROL / 'region_control.c'), *extra, '-o', str(binary)],
            capture_output=True, text=True)
        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
        return binary

    def test_synchronization_callbacks_and_disabled_hooks(self):
        source = self.source('main.c', SYNCHRONIZATION)
        manifest = self.generate([source], flags=['-std=c11'])
        self.assertEqual(source.read_text(), SYNCHRONIZATION)
        generated = (self.output / source.name).read_text()
        pragmas = lambda text: re.findall(r'^\s*(#pragma omp[^\n]*(?:\\\n[^\n]*)*)', text, re.M)
        self.assertEqual(pragmas(generated), pragmas(SYNCHRONIZATION))
        self.assertNotIn('pending_nowait', generated)
        children = [r for r in manifest['regions'] if r['kind'] == 'for']
        self.assertEqual(len(children), 4)
        expected = {'tail': [(2, 'parallel_join')],
                    'next_for': [(3, 'for_barrier'), (1, 'construct_end')],
                    'explicit_sync': [(1, 'explicit_barrier')]}
        for function, groups in expected.items():
            self.assertEqual([(len(r['loop_lines']), r['timing_end']) for r in children
                              if r['function'] == function], groups)
        for instrument in (1, 0):
            binary = self.compile([self.output / source.name], instrument,
                                  ['-Wl,--wrap=omp_get_wtime'])
            for report in (True, False):
                environment = dict(os.environ, OMP_THREAD_LIMIT='2', OMP_PROC_BIND='false',
                                   REGION_TIME_REPORT='1' if report else '0')
                process = subprocess.run([str(binary), *([] if report else ['report=0'])],
                    capture_output=True, text=True, env=environment, timeout=15)
                self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
                self.assertIn('automatic_regions=PASS', process.stdout)
                self.assertEqual('time report' in process.stdout, bool(instrument and report))
        self.assertIn(str(CONTROL / 'region_control.h'), self.depfile.read_text())

    def test_source_list_metadata_and_output_ownership(self):
        def region(name):
            return '#include "region_control.h"\nstatic region_control control;\n' + \
                f'void {name}(void) {{\n#pragma omp parallel\n{{ }}\n}}\n'
        one = self.source('one.c', region('one'))
        two = self.source('two.c', region('two'))
        first = self.generate([one, two])
        self.assertEqual([r['function'] for r in first['regions']], ['one', 'two'])
        self.assertEqual([r['line'] for r in first['regions']], [4, 4])
        self.assertTrue((self.output / 'two.c').exists())
        second = self.generate([one])
        self.assertEqual(len(second['regions']), 1)
        self.assertFalse((self.output / 'two.c').exists())
        snapshots = {path.name: path.read_bytes() for path in self.output.iterdir()}
        self.generate([one])
        self.assertEqual(snapshots, {path.name: path.read_bytes() for path in self.output.iterdir()})
        (self.output / 'notes.txt').write_text('user content\n')
        self.generate([one])
        self.assertEqual((self.output / 'notes.txt').read_text(), 'user content\n')
        dependency_text = self.depfile.read_text()
        self.assertNotIn('region-instrument-', dependency_text)
        one.write_text('invalid C program\n')
        self.generate([one], success=False)
        self.assertEqual(self.depfile.read_text(), dependency_text)
        for name, data in snapshots.items():
            self.assertEqual((self.output / name).read_bytes(), data)

    def test_duplicate_external_names_rejected(self):
        body = '#include "region_control.h"\nstatic region_control control;\n' \
               'static void same(void) {\n#pragma omp parallel\n{ }\n}\n'
        one, two = self.source('a.c', body), self.source('b.c', body)
        message = self.generate([one, two], success=False)
        self.assertRegex(message, r'(?i)(ambiguous|duplicate|conflict).*(same:4|region)')
        self.assertFalse(self.output.exists())

    def test_active_preprocessor_branches_and_header_dependencies(self):
        header = self.source('config.h', '#define ENABLE_REGION 0\n')
        source = self.source('conditional.c', '''#include "region_control.h"
#include "config.h"
static region_control control;
void first(void) {
#pragma omp parallel
{ }
}
#if ENABLE_REGION
void second(void) {
#pragma omp parallel
{ }
}
#endif
''')
        manifest = self.generate([source])
        self.assertEqual([r['function'] for r in manifest['regions']], ['first'])
        self.assertIn(str(header).replace(' ', '\\ '), self.depfile.read_text())
        header.write_text('#define ENABLE_REGION 1\n')
        manifest = self.generate([source])
        self.assertEqual([r['function'] for r in manifest['regions']], ['first', 'second'])

    def test_unsupported_constructs_fail_without_partial_outputs(self):
        cases = {
            'conditional-nowait': '''void work(int run) {
#pragma omp parallel
{
if (run) {
#pragma omp for nowait
for (int i=0;i<8;++i) { }
}
}
}''',
            'helper-nowait': '''void helper(void) {
#pragma omp for nowait
for (int i=0;i<8;++i) { }
}
void work(void) {
#pragma omp parallel
{ helper(); }
}''',
            'nested-parallel': '''void work(void) {
#pragma omp parallel
{
#pragma omp parallel
{ }
}
}''',
            'macro-pragma': '''#define PARALLEL _Pragma("omp parallel")
void work(void) { PARALLEL { } }''',
        }
        for name, body in cases.items():
            with self.subTest(name=name):
                source = self.source('unsupported.c',
                    '#include "region_control.h"\nstatic region_control control;\n' + body)
                message = self.generate([source], success=False)
                self.assertRegex(message, r'(?i)(unsupported|cannot|nowait|macro|nested)')
                self.assertFalse(self.output.exists())

    def test_cpp_combined_and_template_metadata(self):
        source = self.source('template.cpp', r'''#include "region_control.h"
#include "region_auto.h"
#include <cassert>
static region_control control;
template<typename T> T sum(int n) {
  T result = 0;
  #pragma omp parallel for reduction(+:result)
  for (int i=0;i<n;++i) result += T(i);
  return result;
}
int main() {
  region_control_init(&control, region_auto_info, REGION_AUTO_COUNT);
  iteration_start(&control);
  assert(sum<int>(8) == 28 && sum<long>(9) == 36);
  iteration_end(&control);
  assert(REGION_AUTO_COUNT == 1 && region_auto_info[0].combined);
  assert(control.elapsed[0] > 0);
}''')
        manifest = self.generate([source], flags=['-std=c++17'])
        self.assertEqual(len(manifest['regions']), 1)
        region = manifest['regions'][0]
        self.assertEqual((region['function'], region['kind'], region['parent']), ('sum', 'combined', -1))
        cxx = shlex.split(OPTIONS.cxx)
        for name, path in [('table', self.output / 'region_auto.c'),
                           ('runtime', CONTROL / 'region_control.c')]:
            process = subprocess.run(shlex.split(OPTIONS.cc) + ['-std=c11', '-fopenmp',
                '-I', str(CONTROL), '-I', str(self.output), '-c', str(path),
                '-o', str(self.directory / f'{name}.o')], capture_output=True, text=True)
            self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
        binary = self.directory / 'cpp-program'
        process = subprocess.run(cxx + ['-std=c++17', '-fopenmp', '-I', str(CONTROL),
            '-I', str(self.output), str(self.output / source.name),
            str(self.directory / 'table.o'), str(self.directory / 'runtime.o'),
            '-o', str(binary)], capture_output=True, text=True)
        self.assertEqual(process.returncode, 0, process.stdout + process.stderr)
        subprocess.run([str(binary)], env=dict(os.environ, OMP_NUM_THREADS='2', REGION_TIME_REPORT='1'),
                       check=True, timeout=15)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='gcc')
    parser.add_argument('--cxx', default='g++')
    parser.add_argument('--clang', default='clang-18')
    OPTIONS, remaining = parser.parse_known_args()
    unittest.main(argv=[sys.argv[0], *remaining])
