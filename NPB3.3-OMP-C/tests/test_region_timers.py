#!/usr/bin/env python3
"""Standalone timing API regression, using only Class-independent code."""
import argparse
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

from report_region_times import parse_log

parser = argparse.ArgumentParser()
parser.add_argument("--cc", default="gcc")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
source = root / "tests/region_timers_test.c"

with tempfile.TemporaryDirectory(prefix="npb-time-test-") as directory:
    directory = Path(directory)
    binary = directory / "region_timers_test"
    subprocess.run(shlex.split(args.cc) + [
        "-O2", "-std=c11", "-fopenmp", "-Wall", "-Wextra", "-Wpedantic",
        "-Werror", "-I", str(root / "common"), str(source),
        str(root / "common/region_timers.c"),
        str(root.parent / "framework/timer/region_timer.c"),
        "-Wl,--wrap=omp_get_wtime",
        "-o", str(binary),
    ], check=True)
    env = os.environ.copy()
    env.pop("NPB_TIME_REPORT", None)
    env["OMP_THREAD_LIMIT"] = "3"
    env["OMP_NUM_THREADS"] = "2"
    # An old flag file must have no effect, including when the env is zero.
    (directory / "timer.flag").touch()
    for setting in (None, "0", "", "false"):
        run_env = dict(env)
        if setting is not None:
            run_env["NPB_TIME_REPORT"] = setting
        out = subprocess.check_output([str(binary)], cwd=directory,
                                      env=run_env, text=True, timeout=30)
        assert "time report" not in out, (setting, out)
    env["NPB_TIME_REPORT"] = "1"
    out = subprocess.check_output([str(binary)], cwd=directory, env=env,
                                  text=True, timeout=30)
    empty = subprocess.check_output([str(binary), "empty"], cwd=directory,
                                    env=env, text=True, timeout=30)
    assert "iteration total: 0.000000000 s" in empty, empty
    assert not re.search(r"^\s*(iteration|kernel|parallel|for) region ", empty, re.MULTILINE), empty
    assert not re.search(r"\b(nan|inf)\b", empty, re.IGNORECASE), empty

    log = directory / "BT.log"
    log.write_text(out)
    current = parse_log(log)
    assert len(current["rows"]) == 8, current
    assert {row["level"] for row in current["rows"]} == {"parallel", "for"}
    for row in current["rows"]:
        assert row["parent_level"] == ("total" if row["level"] == "parallel" else "parallel")
    # Old saved logs remain readable, while new output has only parallel/for.
    log.write_text("time report\nkernel region total  0.100000000 s\n"
                   "    parallel region old:1  0.050000000 s\n"
                   "        for region old:2  0.025000000 s\n")
    old = parse_log(log)
    assert old["total"] == 0.1
    assert old["rows"][-1]["percent_parent"] == 50

rows = re.findall(r"^( *)(iteration|kernel|parallel|for) region (.*?)  ([\d.]+) s"
                  r"  step: ([\d.]+)%$",
                  out, re.MULTILINE)
assert len(rows) == 8, out
total = float(re.search(r"^iteration total: ([\d.]+) s$", out, re.MULTILINE)[1])
# Three windows, each with 26 clock reads between its begin/end timestamps.
assert total == 0.081, out
assert "excluded" not in out, out
assert "calls=" not in out, out
assert "kernel region" not in out and "iteration region" not in out
parent = None
for indent, kind, name, seconds, share in rows:
    seconds = float(seconds)
    assert seconds >= 0, out
    assert abs(float(share) - 100 * seconds / total) <= 0.000500001, out
    assert kind in {"parallel", "for"}, out
    assert len(indent) == (0 if kind == "parallel" else 4), out
    location = name.split()[0]
    function, lines = location.rsplit(":", 1)
    lineno = int(lines.split("-")[0])
    pragma = source.read_text().splitlines()[lineno - 1].strip()
    expected = "parallel for" if function == "combined" else kind
    assert pragma.startswith("#pragma omp " + expected), (location, pragma)
    if "nowait" in pragma:
        assert name.endswith(" (nowait)") and "-" in lines, name
        end = int(lines.split("-")[1])
        assert source.read_text().splitlines()[end - 1].strip() == "NPB_FOR_END()"
    if kind == "parallel":
        parent = (location, seconds)
    elif function == "combined":
        assert (location, seconds) == parent, out
    elif function == "nowait_chain":
        assert seconds == 0.015, out
    elif function in {"grouped", "work"}:
        assert seconds == 0.006, out
    else:
        assert seconds == 0.003, out

print("PASS: enable/disable, timer.flag ignored, iteration gating, team changes,")
print("      combined equality, explicit nowait groups BEFORE ordinary loops,")
print("      orphaned for, repeated calls, source labels, master-only clock reads,")
print("      all-level time-step percentages, empty report, old/new log parsing,")
print("      parallel/for only, nowait line ranges, static parallel parents,")
print("      no extra clock reads during warmup, verification, and only total timestamps during disabled runs")
