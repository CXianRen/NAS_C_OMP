#!/usr/bin/env python3
"""Verify region_control hooks preserve existing nowait synchronization."""
import argparse
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', default='gcc')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
control = root / 'framework/region_control'
with tempfile.TemporaryDirectory(prefix='manual-nowait-test-') as temporary:
    binary = Path(temporary) / 'test'
    subprocess.run(shlex.split(args.cc) + [
        '-O2', '-std=c11', '-fopenmp', '-Wall', '-Wextra', '-Werror',
        '-I', str(control), str(Path(__file__).with_name('region_nowait_test.c')),
        str(control / 'region_control.c'),
        '-Wl,--wrap=omp_get_wtime', '-o', str(binary),
    ], check=True)
    env = dict(os.environ, OMP_THREAD_LIMIT='2')
    for arguments in ([], ['report=0']):
        text = subprocess.check_output([str(binary), *arguments], env=env,
                                       text=True, timeout=15)
        assert 'manual_nowait=PASS' in text, text
        assert ('time report' in text) == (not arguments), text

print('PASS: manual nowait hooks preserve joins, following loops, explicit barriers,')
print('      helper returns, repeated/skipped groups and master-only clock reads')
