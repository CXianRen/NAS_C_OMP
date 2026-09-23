#!/usr/bin/env python3
"""Compare framework binding, or TUNER=none control hooks, with native OpenMP."""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import statistics
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
MODES = ("native", "framework")
ENV_PREFIXES = ("OMP_", "KMP_", "GOMP_", "OTTER_", "NPB_", "OVERHEAD_")
ENV_KEYS = {"TUNER", "OFFLINE_CONFIG", "REGION_TIME_REPORT"}


def parse_cpus(value):
    """Expand Linux-style CPU lists, rejecting duplicates or descending input."""
    cpus = []
    for part in value.split(","):
        match = re.fullmatch(r"\s*(\d+)(?:-(\d+))?\s*", part)
        if not match:
            raise ValueError(f"Invalid CPU list item: {part!r}")
        first = int(match.group(1))
        last = int(match.group(2)) if match.group(2) else first
        if last < first or last >= 128:
            raise ValueError("CPU ranges must ascend and CPU IDs must be below 128")
        cpus.extend(range(first, last + 1))
    if not cpus or any(a >= b for a, b in zip(cpus, cpus[1:])):
        raise ValueError("CPU list must be strictly ascending without duplicates")
    return cpus


def clean_environment(source=None):
    return {key: value for key, value in (os.environ if source is None else source).items()
            if key not in ENV_KEYS and not key.startswith(ENV_PREFIXES)}


def controlled_environment(args, mode, scenario, cpus):
    maximum = max(args.threads, args.other_threads) if scenario == "switch" else args.threads
    result = {"OMP_NUM_THREADS": str(maximum), "OMP_THREAD_LIMIT": str(maximum),
              "OMP_DYNAMIC": "false", "OMP_PROC_BIND": args.proc_bind if mode == "native" else "false"}
    if mode == "native":
        result["OMP_PLACES"] = ",".join("{" + str(cpu) + "}" for cpu in cpus)
    return result


def benchmark_command(args, out, mode, scenario, probe=False):
    result = [str(out / "binding_bench"), "--mode", mode, "--scenario", scenario,
              "--threads", str(args.threads), "--other-threads", str(args.other_threads),
              "--iterations", str(args.iterations), "--batches", str(args.batches), "--warmup", str(args.warmup)]
    if mode == "framework" or not probe:
        result += ["--map", str(out / f"map_{scenario}.txt")]
    if probe:
        result.append("--probe")
    return result


def build_command(args, out):
    return [args.cxx, "-O3", "-std=c++17", "-fopenmp", "-Wall", "-Wextra", "-Werror",
            "-I" + str(ROOT / "framework"), str(ROOT / "tools/binding_overhead/binding_bench.cpp"),
            str(ROOT / "framework/hams/hams_binding.cpp"), "-o", str(out / "binding_bench")]


def run_logged(command, cwd, env, log, timeout):
    """Keep raw/partial logs, and kill descendants if a process times out."""
    import signal
    with Path(log).open("w") as stream:
        process = subprocess.Popen(command, cwd=cwd, env=env, stdout=stream,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            return process.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            stream.write(f"\nBINDING_RUNNER timeout_seconds={timeout}\n")
            return 124


def parse_mapping(text, cpus, expected_counts):
    records = re.findall(r"^MAP threads=(\d+) cpus=([\d,]+)\s*$", text, re.M)
    result = {}
    for raw_threads, raw_cpus in records:
        threads = int(raw_threads)
        mapping = parse_cpus(raw_cpus)
        if threads in result or len(mapping) != threads or not set(mapping) <= set(cpus):
            raise ValueError("Probe has duplicate/invalid mapping or CPUs outside the requested pool")
        result[threads] = mapping
    if set(result) != set(expected_counts):
        raise ValueError(f"Expected probe mappings for {sorted(set(expected_counts))}, found {sorted(result)}")
    return result


def parse_samples(text, expected_batches, expected_operations=None):
    if re.findall(r"^VALIDATION mapping=(\S+)\s*$", text, re.M) != ["pass"]:
        raise ValueError("Missing or failed post-measurement mapping validation")
    checksums = re.findall(r"^CHECKSUM actual=(\d+) expected=(\d+) status=(\S+)\s*$", text, re.M)
    if len(checksums) != 1 or checksums[0][0] != checksums[0][1] or checksums[0][2] != "pass":
        raise ValueError("Missing or failed kernel checksum")
    return parse_timing_samples(text, expected_batches, expected_operations)


def parse_timing_samples(text, expected_batches, expected_operations=None):
    """Common timing format; each mode validates its own work before calling this."""
    records = re.findall(r"^SAMPLE phase=(first|steady) batch=(\d+) operations=(\d+) seconds=([\d.eE+-]+)\s*$", text, re.M)
    rows = [{"phase": phase, "batch": int(batch), "operations": int(operations), "seconds": float(seconds)}
            for phase, batch, operations, seconds in records]
    if any(row["operations"] < 1 or not math.isfinite(row["seconds"]) or row["seconds"] < 0 for row in rows):
        raise ValueError("Invalid sample duration or operation count")
    first = [row for row in rows if row["phase"] == "first"]
    steady = [row for row in rows if row["phase"] == "steady"]
    if len(first) != 1 or first[0]["operations"] != 1 or first[0]["batch"] != 0:
        raise ValueError("Expected one first-use sample with one operation")
    if len(steady) != expected_batches or {row["batch"] for row in steady} != set(range(1, expected_batches + 1)):
        raise ValueError(f"Expected {expected_batches} unique steady batches")
    if expected_operations is not None and any(row["operations"] != expected_operations for row in steady):
        raise ValueError("Steady batch operation count differs from requested iterations")
    values = {"first": first[0]["seconds"] * 1e6,
              "steady": sum(row["seconds"] for row in steady) / sum(row["operations"] for row in steady) * 1e6}
    return rows, values


def summarize(values):
    """Statistics use independent processes, never individual batches."""
    values = sorted(values)
    position = .95 * (len(values) - 1)
    lo, hi = math.floor(position), math.ceil(position)
    p95 = values[lo] + (values[hi] - values[lo]) * (position - lo)
    avg = statistics.mean(values)
    return {"samples": len(values), "mean_us": avg, "median_us": statistics.median(values),
            "cv_percent": statistics.stdev(values) / avg * 100 if len(values) >= 2 and avg > 0 else "",
            "p95_us": p95}


def write_csv(path, rows, fields):
    with Path(path).open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def write_results(out, samples, batches, scenarios, kind="binding"):
    write_csv(out / "samples.csv", samples, ["round", "order", "scenario", "mode", "status", "first_us", "steady_us", "exit_code", "log", "error"])
    write_csv(out / "batches.csv", batches, ["round", "scenario", "mode", "phase", "batch", "operations", "seconds"])
    summaries, comparisons, grouped = [], [], {}
    for scenario in scenarios:
        for phase in ("first", "steady"):
            for mode in MODES:
                values = {row["round"]: row[phase + "_us"] for row in samples
                          if row["scenario"] == scenario and row["mode"] == mode and row["status"] == "ok"}
                grouped[scenario, phase, mode] = values
                if values:
                    summaries.append({"scenario": scenario, "phase": phase, "mode": mode, **summarize(list(values.values()))})
            native = grouped[scenario, phase, "native"]
            framework = grouped[scenario, phase, "framework"]
            common = sorted(set(native) & set(framework))
            if common:
                base = statistics.mean(native[r] for r in common)
                test = statistics.mean(framework[r] for r in common)
                comparisons.append({"scenario": scenario, "phase": phase, "paired_processes": len(common),
                                    "native_mean_us": base, "framework_mean_us": test,
                                    "mean_paired_delta_us": statistics.mean(framework[r] - native[r] for r in common),
                                    "delta_percent": (test / base - 1) * 100 if base > 0 else ""})
    write_csv(out / "summary.csv", summaries, ["scenario", "phase", "mode", "samples", "mean_us", "median_us", "cv_percent", "p95_us"])
    write_csv(out / "comparisons.csv", comparisons, ["scenario", "phase", "paired_processes", "native_mean_us", "framework_mean_us", "mean_paired_delta_us", "delta_percent"])
    control = kind == "control"
    lines = ["# TUNER=none control-layer overhead comparison" if control else "# Binding overhead comparison", "",
             "Unit: us per operation. Positive delta means framework is slower.",
             ("Native runs the OpenMP kernel; framework runs the same kernel with production region_control hooks and TUNER=none."
              if control else "An operation applies the configuration, then runs a parallel team whose threads update separate cacheline counters."),
             "Steady batches are combined into one sample per independent process. CV uses sample standard deviation; P95 is linearly interpolated.",
             "Steady P95 describes independent process means (us per region), not per-call tail latency; first P95 describes independent first-use samples.", "",
             "| Scenario | Phase | Mode | Processes | Mean us | Median us | CV % | P95 us |",
             "|---|---|---|---:|---:|---:|---:|---:|"]
    for row in summaries:
        cv = f"{row['cv_percent']:.3f}" if row["cv_percent"] != "" else "n/a"
        lines.append(f"| {row['scenario']} | {row['phase']} | {row['mode']} | {row['samples']} | {row['mean_us']:.6f} | {row['median_us']:.6f} | {cv} | {row['p95_us']:.6f} |")
    lines += ["", "| Scenario | Phase | Paired processes | Framework − native us | Delta % |", "|---|---|---:|---:|---:|"]
    for row in comparisons:
        pct = f"{row['delta_percent']:.3f}" if row["delta_percent"] != "" else "n/a"
        lines.append(f"| {row['scenario']} | {row['phase']} | {row['paired_processes']} | {row['mean_paired_delta_us']:.6f} | {pct} |")
    if control:
        lines += ["", "Both modes inherit the same OpenMP environment unchanged; only TUNER is forced to none and --report may override REGION_TIME_REPORT.",
                  "Each operation runs one parallel region with per-thread counter updates. The framework case adds production step, parallel and for hooks.",
                  "Both modes use the same iteration-window timing boundaries per batch. First-use includes first team creation; process launch, initialization and output are outside timing.",
                  "REGION_TIME_REPORT=0 measures disabled-report control hooks; REGION_TIME_REPORT=1 includes per-region timing and accounting.",
                  "No CPU mapping is probed or validated. The checksum validates executed operations only. This comparison measures control-layer cost, not affinity cost.",
                  "This tool does not measure SP/application performance or NUMA page placement."]
    else:
        lines += ["", "The fixed case measures reuse of one configuration; switch alternates team sizes and binding configurations.",
              "First-use includes first configuration and team creation; process launch and setup queries are outside timing. Steady time includes the tiny per-thread kernel and synchronization, not just a binding syscall.",
              "Native mapping is probed first and replayed through the production framework. Each measured process also validates its mapping after all timing has finished.",
              "This tool does not measure SP/application performance, NUMA page placement, or pure offline callback overhead."]
    lines += ["Small mean differences from a few processes are not evidence of a robust win; inspect CV and repeat under controlled load.",
              f"Failed processes: {sum(row['status'] != 'ok' for row in samples)}. Only successful samples enter statistics; raw logs retain failures.", ""]
    (out / "summary.md").write_text("\n".join(lines))


def capture_metadata(out, args, env, cpus):
    directory = out / "metadata"
    directory.mkdir()
    for name, command in {"compiler_version": [args.cxx, "--version"], "lscpu": ["lscpu", "-e"]}.items():
        try:
            code = run_logged(command, ROOT, env, directory / (name + ".txt"), 30)
            if code:
                with (directory / (name + ".txt")).open("a") as stream:
                    stream.write(f"\nmetadata command exit={code}\n")
        except OSError as error:
            (directory / (name + ".txt")).write_text(str(error) + "\n")
    relevant = ["PATH", "LD_LIBRARY_PATH", "LD_PRELOAD", "LIBRARY_PATH", "CPATH", "CPLUS_INCLUDE_PATH"]
    metadata = {"allowed_cpus": sorted(os.sched_getaffinity(0)), "requested_cpu_pool": cpus,
                "relevant_inherited_environment": {key: env[key] for key in relevant if key in env},
                "source_sha256": {}}
    for relative in ["tools/binding_overhead/binding_bench.cpp", "framework/hams/hams_binding.cpp", "framework/hams/hams_binding.h", "tools/binding_overhead/run.py"]:
        data = (ROOT / relative).read_bytes()
        metadata["source_sha256"][relative] = hashlib.sha256(data).hexdigest()
        snapshot = directory / "sources" / relative
        snapshot.parent.mkdir(parents=True, exist_ok=True)
        snapshot.write_bytes(data)
    (directory / "host_and_sources.json").write_text(json.dumps(metadata, indent=2) + "\n")


def parser(tuner_none=False):
    result = argparse.ArgumentParser(description=__doc__)
    result.add_argument("--cpus", default="0-63", help="Ascending CPU ranges/list; these are logical CPU IDs, not inferred core IDs")
    result.add_argument("--threads", type=None if tuner_none else int, default=32)
    result.add_argument("--other-threads", type=None if tuner_none else int, default=48)
    result.add_argument("--iterations", type=int, default=1000)
    result.add_argument("--batches", type=int, default=10)
    result.add_argument("--warmup", type=int, default=100)
    result.add_argument("--repeats", type=int, default=5)
    result.add_argument("--scenario", choices=None if tuner_none else ["all", "fixed", "switch"], default="all")
    result.add_argument("--proc-bind", choices=None if tuner_none else ["spread", "close"], default="spread")
    result.add_argument("--tuner", choices=["none"], help="Compare control hooks with native OpenMP, inheriting environment binding (also selected by TUNER=none)")
    result.add_argument("--report", choices=["0", "1"], help="Override REGION_TIME_REPORT in both none-mode cases; otherwise inherit it")
    result.add_argument("--cc", help="C compiler for none-mode region_control (default: infer from --cxx)")
    result.add_argument("--cxx", default="clang++-18")
    prefix = "control_overhead" if tuner_none else "binding_overhead"
    result.add_argument("--out", type=Path, default=ROOT / "exp_log" / time.strftime(prefix + "_%Y%m%d_%H%M%S"))
    result.add_argument("--timeout", type=float, default=1800)
    result.add_argument("--seed", type=int, default=20260923)
    result.add_argument("--dry-run", action="store_true", help="Print plan only; do not probe the host, compile, or run benchmarks")
    return result


def main(argv=None):
    selector = argparse.ArgumentParser(add_help=False)
    selector.add_argument("--tuner", choices=["none"])
    selected, _ = selector.parse_known_args(argv)
    tuner_none = selected.tuner == "none" or os.environ.get("TUNER", "").lower() == "none"
    args = parser(tuner_none=tuner_none).parse_args(argv)
    try:
        if tuner_none:
            # Resolve locally even when this runner was imported by a test harness.
            import importlib.util
            spec = importlib.util.spec_from_file_location("binding_none_mode", Path(__file__).with_name("none_mode.py"))
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            return module.run(args, ROOT, run_logged, parse_timing_samples, write_results)
        cpus = parse_cpus(args.cpus)
        if not 1 <= args.threads <= len(cpus):
            raise ValueError("Thread count must be positive and fit in the CPU pool")
        if args.iterations < 1 or args.batches < 1 or args.warmup < 0 or args.repeats < 2 or not math.isfinite(args.timeout) or args.timeout <= 0:
            raise ValueError("Require iterations>=1, batches>=1, warmup>=0, repeats>=2, timeout>0")
        scenarios = ["fixed", "switch"] if args.scenario == "all" else [args.scenario]
        if "switch" in scenarios and not 1 <= args.other_threads <= len(cpus):
            raise ValueError("Other thread count must fit in the CPU pool for switch")
        if "switch" in scenarios and (args.iterations % 2 or args.warmup % 2):
            raise ValueError("Switch scenario requires even iterations and warmup")
        if "switch" in scenarios and args.threads == args.other_threads:
            raise ValueError("Switch scenario requires different thread counts")
        out = args.out.resolve()
        rng, order = random.Random(args.seed), []
        for round_number in range(1, args.repeats + 1):
            pairs = [(scenario, mode) for scenario in scenarios for mode in MODES]
            rng.shuffle(pairs)
            order.extend({"round": round_number, "order": index, "scenario": scenario, "mode": mode}
                         for index, (scenario, mode) in enumerate(pairs, 1))
        cases = {f"{scenario}/{mode}": {"command": benchmark_command(args, out, mode, scenario),
                                       "probe_command": benchmark_command(args, out, mode, scenario, probe=True),
                                       "environment": controlled_environment(args, mode, scenario, cpus)}
                 for scenario in scenarios for mode in MODES}
        plan = {"root": str(ROOT), "out": str(out), "cpu_pool": cpus, "seed": args.seed,
                "build_command": build_command(args, out), "cases": cases, "order": order,
                "mapping": "Probe native mapping per scenario, reject nonascending mappings, then replay exactly through framework."}
        if args.dry_run:
            print(json.dumps(plan, indent=2))
            return 0
        unavailable = set(cpus) - os.sched_getaffinity(0)
        if unavailable:
            raise ValueError(f"Requested CPUs are unavailable in this process affinity: {sorted(unavailable)}")
        if out.exists():
            raise ValueError(f"Output directory must be new: {out}")
        out.mkdir(parents=True)
        (out / "plan.json").write_text(json.dumps(plan, indent=2) + "\n")
        env = clean_environment()
        capture_metadata(out, args, env, cpus)
        print("Building binding microbenchmark", flush=True)
        if run_logged(plan["build_command"], ROOT, env, out / "build.log", args.timeout):
            raise ValueError(f"Build failed; see {out / 'build.log'}")
        binary = out / "binding_bench"
        (out / "metadata/binary.sha256").write_text(hashlib.sha256(binary.read_bytes()).hexdigest() + "  binding_bench\n")
        run_logged(["ldd", str(binary)], ROOT, env, out / "metadata/ldd.txt", 30)
        for scenario in scenarios:
            counts = [args.threads, args.other_threads] if scenario == "switch" else [args.threads]
            native_mapping = None
            for mode in MODES:
                directory = out / "probes" / scenario / mode
                directory.mkdir(parents=True)
                case = cases[f"{scenario}/{mode}"]
                (directory / "command.json").write_text(json.dumps({"argv": case["probe_command"], "environment": case["environment"], "cwd": str(directory)}, indent=2) + "\n")
                log = directory / "probe.log"
                print(f"Probe: {scenario}/{mode}", flush=True)
                code = run_logged(case["probe_command"], directory, env | case["environment"], log, args.timeout)
                if code:
                    raise ValueError(f"Probe failed with exit {code}: {log}")
                mapping = parse_mapping(log.read_text(), cpus, counts)
                if mode == "native":
                    native_mapping = mapping
                    (out / f"map_{scenario}.txt").write_text("".join(f"{threads};{','.join(map(str, mapping[threads]))}\n" for threads in sorted(mapping)))
                elif mapping != native_mapping:
                    raise ValueError(f"Framework probe mapping differs from native: {log}")
        samples, batches = [], []
        for item in order:
            scenario, mode = item["scenario"], item["mode"]
            directory = out / "runs" / f"round_{item['round']:03d}" / scenario / mode
            directory.mkdir(parents=True)
            case = cases[f"{scenario}/{mode}"]
            (directory / "command.json").write_text(json.dumps({"argv": case["command"], "environment": case["environment"], "cwd": str(directory)}, indent=2) + "\n")
            log = directory / "run.log"
            sample = dict(item, status="failed", first_us="", steady_us="", exit_code="", log=str(log.relative_to(out)), error="")
            print(f"Round {item['round']}/{args.repeats}: {scenario}/{mode}", flush=True)
            try:
                sample["exit_code"] = run_logged(case["command"], directory, env | case["environment"], log, args.timeout)
                if sample["exit_code"]:
                    raise ValueError(f"Process exit {sample['exit_code']}")
                raw, values = parse_samples(log.read_text(), args.batches, args.iterations)
                sample.update(status="ok", first_us=values["first"], steady_us=values["steady"])
                batches.extend({"round": item["round"], "scenario": scenario, "mode": mode, **row} for row in raw)
            except (OSError, ValueError) as error:
                sample["error"] = str(error)
                print(f"  FAILED: {error}", file=sys.stderr, flush=True)
            samples.append(sample)
            write_results(out, samples, batches, scenarios)
        print(f"Results: {out / 'summary.md'}")
        return int(any(sample["status"] != "ok" for sample in samples))
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
