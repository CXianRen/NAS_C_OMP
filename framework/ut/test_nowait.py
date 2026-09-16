#!/usr/bin/env python3
"""Verify statically paired regions preserve existing nowait synchronization."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc', default='gcc')
parser.add_argument('--clang', default='clang-18')
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
control = root / 'framework/region_control'
with tempfile.TemporaryDirectory(prefix='manual-nowait-test-') as temporary:
    binary = Path(temporary) / 'test'
    source = Path(__file__).with_name('region_nowait_test.c')
    flags = ['-O2', '-std=c11', '-fopenmp', '-Wall', '-Wextra', '-Werror',
             '-I', str(control), '-I', temporary]
    subprocess.run([sys.executable, str(control / 'generate_regions.py'),
                    '--clang', args.clang, '--output', str(Path(temporary) / 'region_metadata.h'),
                    '--sources', str(source), '--', *flags], check=True)
    metadata = (Path(temporary) / 'region_metadata.h').read_text()
    descriptors = set(re.findall(r'^#define REGION_INFO_([PF]_[A-Z_]+)\(', metadata,
                                 flags=re.MULTILINE))
    assert descriptors == {'P_TAIL', 'F_TAIL', 'P_NEXT', 'F_NEXT', 'F_AFTER',
                           'P_SYNC', 'F_HELPER', 'P_CONDITIONAL', 'F_CONDITIONAL'}, metadata
    subprocess.run(shlex.split(args.cc) + [
        *flags, str(source),
        str(control / 'region_control.c'),
        '-Wl,--wrap=omp_get_wtime', '-o', str(binary),
    ], check=True)
    env = dict(os.environ, OMP_THREAD_LIMIT='2')
    for arguments in ([], ['report=0']):
        text = subprocess.check_output([str(binary), *arguments], env=env,
                                       text=True, timeout=15)
        assert 'manual_nowait=PASS' in text, text
        assert ('time report' in text) == (not arguments), text

print('PASS: one static hook pair per merged interval, including an explicit')
print('      post-join end; only merged IDs appear in compile-time metadata;')
print('      repeated/skipped pairs, helper start/caller end, no added barriers,')
print('      master-only clock reads and no region timing when reporting is off')
