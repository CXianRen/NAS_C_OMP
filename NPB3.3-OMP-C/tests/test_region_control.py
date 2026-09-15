#!/usr/bin/env python3
"""Check the actual timer/adapter with deterministic callback and clock stubs."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument("--cc", default="gcc")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
env = os.environ.copy()
for key in ("OMP_PLACES", "KMP_AFFINITY", "GOMP_CPU_AFFINITY"):
    env.pop(key, None)
env.update(OMP_NUM_THREADS="2", OMP_THREAD_LIMIT="2", OMP_DYNAMIC="false",
           OMP_PROC_BIND="false")

with tempfile.TemporaryDirectory(prefix="npb-control-test-") as directory:
    for control in (0, 1):
        binary = Path(directory) / f"adapter-{control}"
        subprocess.run(shlex.split(args.cc) + [
            "-O2", "-std=c11", "-fopenmp", "-Wall", "-Wextra", "-Wpedantic",
            "-Werror", f"-DNPB_REGION_CONTROL={control}", "-I", str(root / "common"),
            str(root / "tests/region_control_adapter_test.c"),
            str(root / "common/region_timers.c"), str(root.parent / "framework/timer/region_timer.c"),
            "-Wl,--wrap=omp_get_wtime",
            "-o", str(binary),
        ], check=True)
        for report in (0, 1):
            env["NPB_TIME_REPORT"] = str(report)
            out = subprocess.check_output([str(binary)], env=env, text=True,
                                          timeout=30)
            assert "region_control_adapter=PASS" in out, out
            assert ("time report" in out) == bool(report), out
            if report:
                rows = re.findall(r"^\s*(parallel|for) region (.*?)  ([\d.]+) s",
                                  out, re.MULTILINE)
                assert rows == [("parallel", "combined:1", "2.000000000"),
                                ("for", "combined:1", "2.000000000"),
                                ("parallel", "tail:2", "2.000000000"),
                                ("for", "tail:3 (nowait)", "1.000000000")], out
            print(f"PASS: adapter control={control} report={report}")

print("PASS: step callbacks, one execution, shared timer samples, callback overhead")
print("      combined equality, nowait tail, report independence, master-only clock")
