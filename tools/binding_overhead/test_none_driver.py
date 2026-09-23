#!/usr/bin/env python3
"""TUNER=none driver checks; no compiler, runtime, or affinity probes are executed."""
import contextlib
import csv
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock


def load(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


driver = load("none_test_driver", "run.py")
none = load("none_test_module", "none_mode.py")
INHERITED = {"PATH": "/bin", "OMP_NUM_THREADS": "32", "OMP_PLACES": "cores", "OMP_PROC_BIND": "spread",
             "OMP_DYNAMIC": "true", "OMP_WAIT_POLICY": "active", "OMP_THREAD_LIMIT": "64", "OMP_SCHEDULE": "guided,4",
             "KMP_AFFINITY": "verbose,none", "KMP_BLOCKTIME": "13", "GOMP_CPU_AFFINITY": "0-63",
             "TUNER": "NoNe", "REGION_TIME_REPORT": "1", "OFFLINE_CONFIG": "/unused/offline.conf",
             "LD_LIBRARY_PATH": "/mock/lib", "UNRELATED_SECRET": "do-not-record"}


def sample_log(mode="native", failed=False):
    first, steady = (1e-6, 2e-6) if mode == "native" else (2e-6, 3e-6)
    return (f"SAMPLE phase=first batch=0 operations=1 seconds={first:.9f}\n"
            + "".join(f"SAMPLE phase=steady batch={batch} operations=3 seconds={steady * 3:.9f}\n" for batch in (1, 2))
            + f"CHECKSUM operations=8 main={7 if failed else 8} status=pass\n")


class NoneDriverTests(unittest.TestCase):
    def test_full_environment_preserved_except_explicit_overrides(self):
        for report in (None, "0", "1"):
            args = driver.parser().parse_args([] if report is None else ["--report", report])
            expected = INHERITED | {"TUNER": "none"}
            if report is not None:
                expected["REGION_TIME_REPORT"] = report
            self.assertEqual(none.inherited_environment(args, INHERITED), expected)
            self.assertNotIn("UNRELATED_SECRET", none.recorded_environment(expected))
        self.assertEqual(INHERITED["TUNER"], "NoNe")

    def test_c_compiler_inference_and_real_framework_sources(self):
        for cxx, expected in (("clang++-18", "clang-18"), ("/opt/clang++", "/opt/clang"),
                              ("g++", "gcc"), ("g++-13", "gcc-13"), ("/opt/c++", "/opt/cc")):
            args = driver.parser().parse_args(["--cxx", cxx])
            self.assertEqual(none.c_compiler(args), expected)
        args = driver.parser().parse_args(["--cxx", "custom++", "--cc", "custom-cc"])
        commands = none.build_commands(args, driver.ROOT, Path("/tmp/none-mock"), ["-lhwloc"])
        self.assertEqual(commands[0][0], "custom-cc")
        self.assertIn("-DREGION_INSTRUMENT=1", commands[0])
        self.assertIn("-DREGION_INSTRUMENT=1", commands[1])
        for source in ("tuner/tuner.cpp", "hams/hams_binding.cpp", "otter/otter.cpp"):
            self.assertTrue(any(arg.endswith(source) for arg in commands[1]))
        self.assertNotIn("binding_bench.cpp", " ".join(commands[1]))

    def test_dry_run_routes_before_all_binding_validation_and_probes(self):
        for selection in ([], ["--tuner", "none"], ["--tuner=none"]):
            env = INHERITED if not selection else INHERITED | {"TUNER": "offline"}
            with self.subTest(selection=selection), tempfile.TemporaryDirectory() as tmp, \
                 mock.patch.dict(driver.os.environ, env, clear=True), \
                 mock.patch.object(driver, "parse_cpus", side_effect=AssertionError("CPU parsing forbidden")), \
                 mock.patch.object(driver, "clean_environment", side_effect=AssertionError("environment cleaning forbidden")), \
                 mock.patch.object(driver.os, "sched_getaffinity", side_effect=AssertionError("affinity query forbidden")), \
                 mock.patch.object(driver.subprocess, "Popen", side_effect=AssertionError("subprocess forbidden")), \
                 contextlib.redirect_stdout(io.StringIO()) as output:
                out = Path(tmp) / "not-created"
                code = driver.main([*selection, "--cpus", "invalid", "--threads", "ignored", "--other-threads", "ignored",
                                    "--scenario", "ignored", "--proc-bind", "ignored", "--iterations", "3", "--warmup", "1",
                                    "--dry-run", "--out", str(out)])
                self.assertEqual(code, 0)
                self.assertFalse(out.exists())
                plan = json.loads(output.getvalue())
                self.assertNotIn("cpu_pool", plan)
                self.assertNotIn("mapping", plan)
                self.assertEqual(plan["cases"]["native"]["environment"], plan["cases"]["framework"]["environment"])
                self.assertEqual(plan["cases"]["native"]["environment"]["OMP_PROC_BIND"], "spread")
                self.assertNotIn("UNRELATED_SECRET", output.getvalue())
                self.assertNotIn("--map", output.getvalue())
                self.assertNotIn("--probe", output.getvalue())

    def test_control_checksum_is_required_without_mapping_validation(self):
        args = driver.parser().parse_args(["--iterations", "3", "--batches", "2", "--warmup", "1"])
        raw, values = none.parse_samples(sample_log(), args, driver.parse_timing_samples)
        self.assertEqual(len(raw), 3)
        self.assertAlmostEqual(values["steady"], 2)
        for text in (sample_log(failed=True), sample_log().replace("operations=8", "operations=7"),
                     sample_log().replace("status=pass", "status=fail"),
                     sample_log() + "CHECKSUM operations=8 main=8 status=pass\n",
                     sample_log().replace("CHECKSUM operations=8 main=8 status=pass", "VALIDATION mapping=pass")):
            with self.subTest(text=text), self.assertRaises(ValueError):
                none.parse_samples(text, args, driver.parse_timing_samples)

    def fake_run(self, command, cwd, env, log, timeout):
        self.calls.append((list(command), dict(env)))
        self.assertEqual(env, self.expected_env)
        log = Path(log)
        self.assertNotIn("--probe", command)
        self.assertNotIn("--map", command)
        if command[0] == "pkg-config":
            log.write_text("-I/mock/hwloc -lhwloc\n")
        elif "--version" in command or command[0] == "ldd":
            log.write_text("mock metadata\n")
        elif "-o" in command:
            Path(command[command.index("-o") + 1]).write_bytes(b"mock compiler output")
            log.write_text("mock build\n")
        else:
            mode = command[command.index("--mode") + 1]
            self.assertIn(mode, ("native", "control"))
            self.assertNotIn("--threads", command)
            log.write_text(sample_log(mode, failed=self.fail_control and mode == "control"))
        return 0

    def mock_end_to_end(self, report=None, fail_control=False):
        self.calls, self.fail_control = [], fail_control
        self.expected_env = INHERITED | {"TUNER": "none"}
        if report is not None:
            self.expected_env["REGION_TIME_REPORT"] = report
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "results"
            with mock.patch.dict(driver.os.environ, INHERITED, clear=True), \
                 mock.patch.object(driver, "run_logged", side_effect=self.fake_run), \
                 mock.patch.object(driver, "capture_metadata", side_effect=AssertionError("binding metadata forbidden")), \
                 mock.patch.object(driver, "clean_environment", side_effect=AssertionError("environment cleaning forbidden")), \
                 mock.patch.object(driver.os, "sched_getaffinity", side_effect=AssertionError("affinity query forbidden")), \
                 mock.patch.object(driver.subprocess, "Popen", side_effect=AssertionError("real processes forbidden")), \
                 contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                arguments = ["--repeats", "2", "--iterations", "3", "--batches", "2", "--warmup", "1",
                             "--cxx", "mock++", "--cc", "mock-cc", "--out", str(out)]
                if report is not None:
                    arguments += ["--report", report]
                code = driver.main(arguments)
            self.assertEqual(code, int(fail_control))
            with (out / "samples.csv").open() as stream:
                samples = list(csv.DictReader(stream))
            with (out / "summary.csv").open() as stream:
                summaries = list(csv.DictReader(stream))
            self.assertEqual(len(samples), 4)
            self.assertEqual(len(summaries), 2 if fail_control else 4)
            self.assertTrue(all(row["samples"] == "2" for row in summaries))
            if fail_control:
                self.assertTrue(all(row["mode"] == "native" for row in summaries))
                self.assertEqual(sum(row["status"] == "failed" for row in samples), 2)
            report_text = (out / "summary.md").read_text()
            self.assertIn("TUNER=none control-layer overhead", report_text)
            self.assertNotIn("# Binding overhead", report_text)
            self.assertNotIn("replayed", report_text)
            self.assertFalse((out / "probes").exists())
            self.assertFalse(list(out.glob("map_*")))
            metadata = (out / "metadata/environment_and_sources.json").read_text()
            self.assertNotIn("UNRELATED_SECRET", metadata)
            self.assertNotIn("allowed_cpus", metadata)
            self.assertTrue((out / "metadata/binary.sha256").is_file())
            self.assertTrue((out / "metadata/sources/framework/tuner/tuner.cpp").is_file())
            commands = [command for command, _ in self.calls]
            self.assertEqual(sum("--mode" in command for command in commands), 4)
            self.assertEqual(sum("-o" in command for command in commands), 2)
            self.assertIn("-I/mock/hwloc", next(command for command in commands if "-std=c++17" in command))

    def test_mock_end_to_end_report_inherited_zero_and_one(self):
        for report in (None, "0", "1"):
            with self.subTest(report=report):
                self.mock_end_to_end(report=report)

    def test_failed_checksums_are_excluded_from_statistics(self):
        self.mock_end_to_end(fail_control=True)


if __name__ == "__main__":
    unittest.main()
