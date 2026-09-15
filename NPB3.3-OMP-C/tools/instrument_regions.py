#!/usr/bin/env python3
"""NPB compatibility entrypoint for the shared region instrumenter."""
from pathlib import Path
import runpy
import sys


# Keep the existing CLI and implicit NPB common-header include path.
if __name__ == '__main__':
    root = Path(__file__).resolve().parents[2]
    sys.argv[1:1] = ['--include-dir', str(root / 'NPB3.3-OMP-C/common')]
    runpy.run_path(str(root / 'framework/region_control/instrument_regions.py'), run_name='__main__')
