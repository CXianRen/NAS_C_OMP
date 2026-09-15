#!/usr/bin/env python3
"""Compile and execute generated instrumentation, including difficult syntax."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--cc', default='gcc')
parser.add_argument('--clang', default='clang-18')
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
tool = root / 'tools/instrument_regions.py'

APP = r'''#include <assert.h>
#include "region_timers.h"
/* Use deterministic timestamps so tiny loops always appear in the report. */
double __wrap_omp_get_wtime(void) {
  static int ticks;
  assert(omp_get_thread_num() == 0);
  return ++ticks;
}
void helper(int *, int);
/* 中文注释：Clang 的字节偏移必须正确转换。 */
static void run(int *a, int n) {
  const char *ignored = "#pragma omp for nowait { }";
  (void)ignored;
  #pragma omp parallel shared(a, n)
  {
    #pragma omp for schedule(static) nowait
    for (int i = 0; i < n; ++i) { a[i] += 1; }
    int delta = 2; /* Must remain visible after the nowait group. */
    #pragma omp for schedule(static) nowait
    for (int i = 0; i < n; ++i) a[i] += delta;
    #pragma omp for /* nowait in this comment is not a clause */
    for (int i = 0; i < n; ++i) a[i] += delta;
    helper(a, n);
    #pragma omp barrier
  }
  if (n > 0)
    #pragma omp parallel for \
      schedule(static)
    for (int i = 0; i < n; ++i)
      if (a[i] > 0) a[i] += 1; else a[i] = 1;
#if 0
  #pragma omp parallel for simd
  for (int i = 0; i < n; ++i) a[i] += 999;
#endif
}
int main(void) {
  int a[64] = {0};
  run(a, 64); /* Warmup remains outside the measurement window. */
  for (int i = 0; i < 64; ++i) a[i] = 0;
  int original_line = __LINE__; assert(original_line == EXPECTED_LINE);
  npb_time_begin();
  run(a, 64);
  npb_time_end();
  for (int i = 0; i < 64; ++i) assert(a[i] == 10);
  npb_time_report();
  return 0;
}
'''
HELPER = '''void helper(int *a, int n) {
  #pragma omp for schedule(static) nowait
  for (int i = 0; i < n; ++i) a[i] += 4;
}
'''
APP = APP.replace('EXPECTED_LINE', str(next(i for i, line in enumerate(APP.splitlines(), 1)
                                          if 'original_line =' in line)))

with tempfile.TemporaryDirectory(prefix='npb-instrument-test-') as directory:
    directory = Path(directory)
    app, helper = directory / 'app.c', directory / 'helper.c'
    app.write_text(APP)
    helper.write_text(HELPER)
    output = directory / 'generated'
    command = [sys.executable, str(tool), str(app), str(helper), '--output', str(output),
               '--clang', args.clang]
    generated = subprocess.run(command, capture_output=True, text=True)
    assert generated.returncode == 0, generated.stderr
    manifest = json.loads((output / 'instrumentation.json').read_text())
    regions = manifest['regions']
    assert len(regions) == 5, regions
    grouped = next(r for r in regions if r['function'] == 'run' and r['nowait'])
    assert len(grouped['loop_lines']) == 2, grouped
    assert APP.splitlines()[grouped['end_line'] - 1].strip().startswith('#pragma omp for /*'), grouped
    orphan = next(r for r in regions if r['function'] == 'helper')
    assert regions[orphan['parent']]['kind'] == 'parallel', regions
    assert sum(r['kind'] == 'combined' for r in regions) == 1
    for path in (app, helper):
        before = re.findall(r'^\s*#pragma omp[^\n]*(?:\\\n[^\n]*)?', path.read_text(), re.MULTILINE)
        after = re.findall(r'^\s*#pragma omp[^\n]*(?:\\\n[^\n]*)?', (output / path.name).read_text(), re.MULTILINE)
        assert [s.strip() for s in before] == [s.strip() for s in after], path
    def hashes():
        return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in output.iterdir()}
    first = hashes()
    subprocess.run(command, check=True, capture_output=True, text=True)
    assert first == hashes(), 'generation must be deterministic and idempotent'
    assert app.read_text() == APP and helper.read_text() == HELPER
    # Make must also handle source files being added and removed. Removed
    # generated .c files must not survive into the next wildcard compilation.
    extra = directory / 'extra.c'
    extra.write_text('int unused_helper(void) { return 1; }\n')
    expanded = command.copy()
    expanded.insert(expanded.index('--output'), str(extra))
    subprocess.run(expanded, check=True, capture_output=True, text=True)
    assert (output / extra.name).exists()
    subprocess.run(command, check=True, capture_output=True, text=True)
    assert not (output / extra.name).exists()
    assert first == hashes()
    binary = directory / 'test'
    subprocess.run(shlex.split(args.cc) + [
        '-O2', '-fopenmp', '-Wall', '-Wextra', '-Werror', '-I', str(root / 'common'),
        str(output / 'app.c'), str(output / 'helper.c'), str(output / 'npb_generated_regions.c'),
        str(root / 'common/region_timers.c'), str(root.parent / 'framework/timer/region_timer.c'),
        '-Wl,--wrap=omp_get_wtime', '-o', str(binary),
    ], check=True)
    env = dict(os.environ, OMP_NUM_THREADS='3', OMP_DYNAMIC='false', NPB_TIME_REPORT='1')
    out = subprocess.check_output([str(binary)], env=env, text=True, timeout=30)
    rows = re.findall(r'^( *)(parallel|for) region (.*?)  ([\d.]+) s  step: ([\d.]+)%$', out, re.MULTILINE)
    assert len(rows) == 6, out
    assert 'kernel region' not in out and 'iteration region' not in out
    combined = next(r for r in regions if r['kind'] == 'combined')
    pair = [r for r in rows if r[2] == combined['label']]
    assert len(pair) == 2 and pair[0][3:] == pair[1][3:], out
    assert sum('(nowait)' in row[2] for row in rows) == 2, out
    env['NPB_TIME_REPORT'] = '0'
    assert 'time report' not in subprocess.check_output([str(binary)], env=env, text=True, timeout=30)

    # Ambiguities and unsupported input must fail before creating output.
    bad = directory / 'bad.c'
    cases = [
        ('void f(void) {\n#pragma omp for\nfor(int i=0;i<4;i++) {}\n}', 'orphaned for'),
        ('void f(void) {\n#pragma omp parallel\n{\n#pragma omp parallel\n{}\n}\n}', 'nested parallel'),
        ('void f(void) {\n#pragma omp parallel for simd\nfor(int i=0;i<4;i++) {}\n}', 'unsupported construct'),
        ('void f(void) { npb_time_start(3); }', 'custom timing'),
    ]
    for source, message in cases:
        bad.write_text(source)
        target = directory / 'bad-output'
        result = subprocess.run([sys.executable, str(tool), str(bad), '--output', str(target),
                                 '--clang', args.clang], capture_output=True, text=True)
        assert result.returncode and message in result.stderr, result.stderr
        assert not target.exists(), target

print('PASS: generated code executes with correct results; UTF-8, multiline pragmas,')
print('      unbraced control flow, source line preservation, orphaned for parents,')
print('      nowait grouping, declaration scope, combined equality, deterministic output,')
print('      disabled timing and conservative rejection before writing output')
