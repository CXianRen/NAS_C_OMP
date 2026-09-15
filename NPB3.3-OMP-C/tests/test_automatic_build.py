#!/usr/bin/env python3
"""Exercise SP generation, build reuse, compiler flags and source-set changes."""
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
with tempfile.TemporaryDirectory(prefix="npb-auto-make-check-") as scratch:
    directory = Path(scratch)
    source = directory / "source"
    source.mkdir()
    shutil.copytree(root / "SP", source / "SP")
    for name in ("common", "sys", "tools"):
        (source / name).symlink_to(root / name, target_is_directory=True)

    # A probe in the temporary real source checks preprocessing and codegen.
    main = source / "SP/src/sp.c"
    anchor = "  int i, niter, step, n3;"
    text = main.read_text()
    assert text.count(anchor) == 1
    main.write_text(text.replace(anchor, """#ifdef TEST_BUILD_BRANCH
#pragma omp parallel
  {
#pragma omp master
    puts("build-flag=1");
  }
#else
  puts("build-flag=0");
#endif
""" + anchor))

    binary = directory / "bin/SP.S"
    generated = directory / "build/SP.S/instrumented"
    command = ["make", "-f", str(root / "Makefile"), "BENCHMARKS=SP", "CLASS=S",
               "TUNER=none", "-j2", f"NPB_DIR={source}",
               f"BUILD_DIR={directory}/build", f"BIN_DIR={directory}/bin"]
    env = dict(os.environ, NPB_TIME_REPORT="1", OMP_NUM_THREADS="4", OMP_DYNAMIC="false")
    env.pop("NPB_NITER", None)  # Keep the standard SP.S reference workload.

    def make(flags="", instrument=1):
        process = subprocess.run(command + [f"CPPFLAGS={flags}", f"INSTRUMENT={instrument}"],
                                 cwd=root, capture_output=True, text=True)
        assert process.returncode == 0, process.stdout + process.stderr

    def run_sp(flag):
        process = subprocess.run([str(binary)], cwd=directory, env=env,
                                 capture_output=True, text=True, timeout=60)
        assert process.returncode == 0, process.stdout + process.stderr
        assert re.search(r"Verification\s*=\s*SUCCESSFUL", process.stdout), process.stdout
        assert f"build-flag={flag}" in process.stdout, process.stdout
        return process.stdout

    def manifest():
        return json.loads((generated / "instrumentation.json").read_text())

    make()
    run_sp(0)
    before = binary.stat().st_mtime_ns
    make()
    assert before == binary.stat().st_mtime_ns, "unchanged build compiled again"
    print("PASS: SP.S automatic generation, numerical verification and build reuse", flush=True)

    previous = manifest()
    flags = "-DTEST_BUILD_BRANCH"
    make(flags)
    run_sp(1)
    current = manifest()
    assert before != binary.stat().st_mtime_ns and flags in current["compiler_args"]
    assert len(current["regions"]) == len(previous["regions"]) + 1
    print("PASS: CPPFLAGS changes reach the instrumenter and compiled SP executable", flush=True)

    extra = source / "SP/src/extra.c"
    extra.write_text("int extra_function(void) { return 1; }\n")
    make(flags)
    assert (generated / "extra.c").exists()
    before = binary.stat().st_mtime_ns
    extra.unlink()
    make(flags)
    assert not (generated / "extra.c").exists() and before != binary.stat().st_mtime_ns
    print("PASS: source additions/removals regenerate and remove obsolete generated C", flush=True)

    before = binary.stat().st_mtime_ns
    make(flags, instrument=0)
    assert before != binary.stat().st_mtime_ns and "time report" not in run_sp(1)
    symbols = subprocess.check_output(["nm", str(binary)], text=True)
    assert not re.search(r"\b(?:npb_regions|npb_region_count|npb_time_(?:active|enabled|"
                         r"start|stop|nowait_start|sync|read|sample_begin|sample_end))$",
                         symbols, re.MULTILINE), symbols
    before = binary.stat().st_mtime_ns
    make(flags, instrument=0)
    assert before == binary.stat().st_mtime_ns, "unchanged plain build compiled again"
    make(flags, instrument=1)
    assert before != binary.stat().st_mtime_ns and "time report" in run_sp(1)
    print("PASS: instrumentation off/on rebuilds; plain SP has no region hooks/report", flush=True)
