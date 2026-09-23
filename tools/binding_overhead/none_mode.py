"""TUNER=none comparison: preserve OpenMP environment and measure control hooks."""
from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import shlex
import sys

CPP_SOURCES = ["tools/binding_overhead/control_bench.cpp"] + [
    f"framework/{name}/{name}.cpp"
    for name in ("tuner", "dummy", "offline", "j2025", "j2025_b", "otter")
] + ["framework/hams/hams_binding.cpp"]
C_SOURCE = "framework/region_control/region_control.c"


def inherited_environment(args, source=None):
    env = dict(os.environ if source is None else source)
    env["TUNER"] = "none"
    if args.report is not None:
        env["REGION_TIME_REPORT"] = args.report
    return env


def recorded_environment(env):
    return {key: value for key, value in env.items()
            if key.startswith(("OMP_", "KMP_", "GOMP_"))
            or key in ("TUNER", "REGION_TIME_REPORT", "OFFLINE_CONFIG")}


def c_compiler(args):
    if args.cc:
        return args.cc
    inferred = args.cxx.replace("clang++", "clang").replace("g++", "gcc")
    if inferred != args.cxx:
        return inferred
    if Path(args.cxx).name == "c++":
        return str(Path(args.cxx).with_name("cc"))
    return os.environ.get("CC", "cc")


def build_commands(args, root, out, hwloc_flags):
    common = ["-O3", "-fopenmp", "-DREGION_INSTRUMENT=1"]
    return [
        [c_compiler(args), *common, "-std=c11", "-I" + str(root / "framework/region_control"),
         "-c", str(root / C_SOURCE), "-o", str(out / "region_control.o")],
        [args.cxx, *common, "-std=c++17", "-I" + str(root / "framework"),
         *[str(root / source) for source in CPP_SOURCES], str(out / "region_control.o"),
         *hwloc_flags, "-o", str(out / "control_bench")],
    ]


def capture_metadata(out, args, root, env, run_logged):
    directory = out / "metadata"
    directory.mkdir()
    for name, compiler in (("c_compiler_version", c_compiler(args)), ("cxx_compiler_version", args.cxx)):
        try:
            run_logged([compiler, "--version"], root, env, directory / (name + ".txt"), 30)
        except OSError as error:
            (directory / (name + ".txt")).write_text(str(error) + "\n")
    sources = CPP_SOURCES + [C_SOURCE, "tools/binding_overhead/run.py", "tools/binding_overhead/none_mode.py"]
    sources += [str(Path(source).with_suffix(".h")) for source in CPP_SOURCES + [C_SOURCE]
                if (root / Path(source).with_suffix(".h")).is_file()]
    hashes = {}
    for source in sources:
        data = (root / source).read_bytes()
        hashes[source] = hashlib.sha256(data).hexdigest()
        snapshot = directory / "sources" / source
        snapshot.parent.mkdir(parents=True, exist_ok=True)
        snapshot.write_bytes(data)
    (directory / "environment_and_sources.json").write_text(json.dumps(
        {"environment": recorded_environment(env), "source_sha256": hashes}, indent=2) + "\n")


def parse_samples(text, args, parse_timing_samples):
    checksums = re.findall(r"^CHECKSUM operations=(\d+) main=(\d+) status=(\S+)\s*$", text, re.M)
    expected = 1 + args.warmup + args.iterations * args.batches
    if (len(checksums) != 1 or tuple(map(int, checksums[0][:2])) != (expected, expected)
            or checksums[0][2] != "pass"):
        raise ValueError("Missing or failed control-kernel operation checksum")
    return parse_timing_samples(text, args.batches, args.iterations)


def run(args, root, run_logged, parse_timing_samples, write_results):
    if (args.iterations < 1 or args.batches < 1 or args.warmup < 0 or args.repeats < 2
            or not math.isfinite(args.timeout) or args.timeout <= 0):
        raise ValueError("Require iterations>=1, batches>=1, warmup>=0, repeats>=2, timeout>0")
    out, env = args.out.resolve(), inherited_environment(args)
    recorded = recorded_environment(env)
    cases = {mode: {"command": [str(out / "control_bench"), "--mode", cpp_mode,
                               "--iterations", str(args.iterations), "--batches", str(args.batches),
                               "--warmup", str(args.warmup)], "environment": recorded}
             for mode, cpp_mode in (("native", "native"), ("framework", "control"))}
    rng, order = random.Random(args.seed), []
    for repeat in range(1, args.repeats + 1):
        modes = list(cases)
        rng.shuffle(modes)
        order.extend({"round": repeat, "order": index, "scenario": "control", "mode": mode}
                     for index, mode in enumerate(modes, 1))
    plan = {"comparison": "TUNER=none control-layer overhead", "root": str(root), "out": str(out),
            "seed": args.seed, "build_commands": build_commands(args, root, out, ["-lhwloc"]),
            "hwloc_flags": "Resolve pkg-config --cflags --libs hwloc on execution; fallback -lhwloc",
            "cases": cases, "order": order,
            "binding": "Both cases inherit all environment settings. No affinity probe or mapping validation.",
            "ignored_binding_options": ["--cpus", "--threads", "--other-threads", "--scenario", "--proc-bind"]}
    if args.dry_run:
        print(json.dumps(plan, indent=2))
        return 0
    if out.exists():
        raise ValueError(f"Output directory must be new: {out}")
    out.mkdir(parents=True)
    flags = ["-lhwloc"]
    try:
        if run_logged(["pkg-config", "--cflags", "--libs", "hwloc"], root, env, out / "hwloc.log", 30) == 0:
            flags = shlex.split((out / "hwloc.log").read_text())
    except OSError as error:
        (out / "hwloc.log").write_text(str(error) + "\nUsing fallback -lhwloc\n")
    plan["hwloc_flags"] = flags
    plan["build_commands"] = build_commands(args, root, out, flags)
    (out / "plan.json").write_text(json.dumps(plan, indent=2) + "\n")
    capture_metadata(out, args, root, env, run_logged)
    print("Building TUNER=none control-layer microbenchmark", flush=True)
    for index, command in enumerate(plan["build_commands"], 1):
        log = out / f"build_{index}.log"
        if run_logged(command, root, env, log, args.timeout):
            raise ValueError(f"Build failed; see {log}")
    binary = out / "control_bench"
    (out / "metadata/binary.sha256").write_text(hashlib.sha256(binary.read_bytes()).hexdigest() + "  control_bench\n")
    run_logged(["ldd", str(binary)], root, env, out / "metadata/ldd.txt", 30)
    samples, batches = [], []
    for item in order:
        mode = item["mode"]
        directory = out / "runs" / f"round_{item['round']:03d}" / "control" / mode
        directory.mkdir(parents=True)
        case = cases[mode]
        (directory / "command.json").write_text(json.dumps(
            {"argv": case["command"], "environment": recorded, "cwd": str(directory)}, indent=2) + "\n")
        log = directory / "run.log"
        sample = dict(item, status="failed", first_us="", steady_us="", exit_code="", log=str(log.relative_to(out)), error="")
        print(f"Round {item['round']}/{args.repeats}: control/{mode}", flush=True)
        try:
            sample["exit_code"] = run_logged(case["command"], directory, env, log, args.timeout)
            if sample["exit_code"]:
                raise ValueError(f"Process exit {sample['exit_code']}")
            raw, values = parse_samples(log.read_text(), args, parse_timing_samples)
            sample.update(status="ok", first_us=values["first"], steady_us=values["steady"])
            batches.extend({"round": item["round"], "scenario": "control", "mode": mode, **row} for row in raw)
        except (OSError, ValueError) as error:
            sample["error"] = str(error)
            print(f"  FAILED: {error}", file=sys.stderr, flush=True)
        samples.append(sample)
        write_results(out, samples, batches, ["control"], kind="control")
    print(f"Results: {out / 'summary.md'}")
    return int(any(sample["status"] != "ok" for sample in samples))
