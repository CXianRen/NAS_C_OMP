#!/usr/bin/env python3
"""Prove generated nowait timing includes existing synchronization, without adding it."""
import argparse
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = r'''#define _POSIX_C_SOURCE 200809L
#include "region_timers.h"
#include <assert.h>
#include <sched.h>
#include <stdatomic.h>
#include <time.h>

static atomic_int released, phase;
static int clock_calls;

/* Only the primary thread reads timestamps. A worker advances the logical
 * clock by one second when it finishes. Wall-clock scheduling cannot shorten
 * a correctly measured interval below that second.
 */
double __wrap_omp_get_wtime(void) {
  assert(omp_get_thread_num() == 0);
  return atomic_load(&phase) + 0.001 * ++clock_calls;
}
static void worker(int delay) {
  while (!atomic_load(&released)) sched_yield();
  if (delay) {
    struct timespec wait = {0, 20000000};
    nanosleep(&wait, 0);
  }
  atomic_fetch_add(&phase, 1);
}
static void tail(void) {
  atomic_store(&released, 0);
  #pragma omp parallel
  {
    #pragma omp for schedule(static, 1) nowait
    for (int i = 0; i < 2; ++i) if (i == 1) worker(1);
    #pragma omp master
    atomic_store(&released, 1);
  }
}
static void next_for(void) {
  atomic_store(&released, 0);
  #pragma omp parallel
  {
    #pragma omp for schedule(static, 1) nowait
    for (int i = 0; i < 2; ++i) if (i == 1) worker(0);
    #pragma omp for schedule(static, 1)
    for (int i = 0; i < 2; ++i) { if (i == 0) atomic_store(&released, 1); }
  }
}
static void helper(void) {
  #pragma omp for schedule(static, 1) nowait
  for (int i = 0; i < 2; ++i) if (i == 1) worker(1);
}
static void explicit_sync(void) {
  atomic_store(&released, 0);
  #pragma omp parallel
  {
    helper();
    #pragma omp master
    atomic_store(&released, 1);
    if (1) {
      #pragma omp barrier
    }
    #pragma omp master
    atomic_fetch_add(&phase, 10); /* Must not be charged to the completed group. */
  }
}
static void conditional(int execute) {
  atomic_store(&released, 0);
  #pragma omp parallel
  {
    if (execute)
      #pragma omp for schedule(static, 1) nowait
      for (int i = 0; i < 2; ++i) if (i == 1) worker(1);
    #pragma omp master
    atomic_store(&released, 1);
  }
}
int main(void) {
  omp_set_dynamic(0);
  omp_set_num_threads(2);
  npb_time_begin();
  tail();
  tail();
  next_for();
  explicit_sync();
  conditional(1);
  conditional(0); /* Must not reuse the previous call's pending timer. */
  npb_time_end();
  assert(atomic_load(&phase) == 15);
  npb_time_report();
}
'''

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', default='gcc')
parser.add_argument('--clang', default='clang-18')
args = parser.parse_args()
with tempfile.TemporaryDirectory(prefix='npb-nowait-barrier-test-') as directory:
    directory = Path(directory)
    source = directory / 'test.c'
    source.write_text(SOURCE)
    output = directory / 'generated'
    generated = subprocess.run([
        sys.executable, str(ROOT / 'tools/instrument_regions.py'), str(source),
        '--output', str(output), '--clang', args.clang,
        '--', '-std=c11', '-D_POSIX_C_SOURCE=200809L',
    ], capture_output=True, text=True)
    assert generated.returncode == 0, generated.stderr
    manifest = json.loads((output / 'instrumentation.json').read_text())
    # No extra synchronization is allowed. In the test a worker waits for an
    # action performed after the nowait loop; inserting a loop-end barrier
    # would deadlock and hit the subprocess timeout.
    generated_text = (output / source.name).read_text()
    assert generated_text.count('#pragma omp barrier') == SOURCE.count('#pragma omp barrier')
    binary = directory / 'test'
    subprocess.run(shlex.split(args.cc) + [
        '-O2', '-std=c11', '-fopenmp', '-Wall', '-Wextra', '-Werror',
        '-D_POSIX_C_SOURCE=200809L',
        '-I', str(ROOT / 'common'), str(output / source.name),
        str(output / 'npb_generated_regions.c'), str(ROOT / 'common/region_timers.c'),
        '-Wl,--wrap=omp_get_wtime', '-o', str(binary),
    ], check=True)
    env = dict(os.environ, OMP_THREAD_LIMIT='2', NPB_TIME_REPORT='1')
    text = subprocess.check_output([str(binary)], env=env, text=True, timeout=15)
    rows = re.findall(r'^    for region (.*?)  ([\d.]+) s', text, re.MULTILINE)
    times = {name: float(seconds) for name, seconds in rows}
    assert len(times) == 5, text
    for region in manifest['regions']:
        if region['kind'] != 'for':
            continue
        value = times[region['label']]
        if region['function'] == 'tail':
            assert 2 <= value < 3, text
        elif region['function'] == 'next_for' and region['nowait']:
            assert 0 < value < 0.5, text
        else:
            assert 1 <= value < 2, text
    env['NPB_TIME_REPORT'] = '0'
    assert 'time report' not in subprocess.check_output([str(binary)], env=env, text=True, timeout=15)

print('PASS: trailing nowait includes parallel join; ordinary for closes prior group;')
print('      explicit barrier closes helper group; skipped groups do not reuse timers;')
print('      repeated calls, unbraced control flow, no added barriers, master-only clock')
