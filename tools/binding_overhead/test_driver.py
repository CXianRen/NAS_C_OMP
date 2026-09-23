#!/usr/bin/env python3
"""Driver-only tests: subprocesses, including every benchmark call, are mocked."""
import contextlib
import csv
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location("binding_driver", Path(__file__).with_name("run.py"))
driver = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(driver)


def sample_log(first=1e-6, steady=2e-6, batches=2, operations=10):
    lines = [f"SAMPLE phase=first batch=0 operations=1 seconds={first:.9f}"]
    lines += [f"SAMPLE phase=steady batch={batch} operations={operations} seconds={steady * operations:.9f}"
              for batch in range(1, batches + 1)]
    lines.append("CHECKSUM actual=42 expected=42 status=pass")
    lines.append("VALIDATION mapping=pass")
    return "\n".join(lines) + "\n"


class DriverTests(unittest.TestCase):
    def test_cpu_pool_validation(self):
        self.assertEqual(driver.parse_cpus("0-2,4,8-9"), [0, 1, 2, 4, 8, 9])
        for value in ("", "0,0", "2,1", "3-1", "128", "0-128", "0,,1", "-1"):
            with self.subTest(value=value), self.assertRaises(ValueError):
                driver.parse_cpus(value)

    def test_environment_controls_and_cleaning(self):
        inherited = {"PATH": "/bin", "OMP_PLACES": "cores", "KMP_AFFINITY": "compact", "TUNER": "offline",
                     "GOMP_CPU_AFFINITY": "0", "OTTER_X": "1", "NPB_NITER": "1", "OVERHEAD_PREPIN": "1"}
        self.assertEqual(driver.clean_environment(inherited), {"PATH": "/bin"})
        args = driver.parser().parse_args(["--threads", "2", "--other-threads", "4"])
        native = driver.controlled_environment(args, "native", "switch", [0, 2, 4, 6])
        framework = driver.controlled_environment(args, "framework", "switch", [0, 2, 4, 6])
        self.assertEqual(native["OMP_NUM_THREADS"], "4")
        self.assertEqual(native["OMP_THREAD_LIMIT"], framework["OMP_THREAD_LIMIT"])
        self.assertEqual(native["OMP_PLACES"], "{0},{2},{4},{6}")
        self.assertNotIn("OMP_PLACES", framework)
        self.assertEqual(framework["OMP_PROC_BIND"], "false")

    def test_maps_are_passed_to_all_measured_processes_but_not_native_discovery(self):
        args = driver.parser().parse_args([])
        out = Path("/tmp/mock_binding")
        for mode in driver.MODES:
            self.assertIn("--map", driver.benchmark_command(args, out, mode, "fixed"))
        self.assertNotIn("--map", driver.benchmark_command(args, out, "native", "fixed", probe=True))
        self.assertIn("--map", driver.benchmark_command(args, out, "framework", "fixed", probe=True))

    def test_mapping_rejects_unrepresentable_or_duplicate_rows(self):
        self.assertEqual(driver.parse_mapping("MAP threads=2 cpus=0,2\n", [0, 1, 2, 3], [2]), {2: [0, 2]})
        for text in ("MAP threads=2 cpus=2,0\n", "MAP threads=2 cpus=0,4\n",
                     "MAP threads=2 cpus=0,2\nMAP threads=2 cpus=0,2\n", "MAP threads=1 cpus=0\n"):
            with self.subTest(text=text), self.assertRaises(ValueError):
                driver.parse_mapping(text, [0, 1, 2, 3], [2])

    def test_sample_validation_and_independent_process_aggregation(self):
        raw, values = driver.parse_samples(sample_log(), 2, 10)
        self.assertEqual(len(raw), 3)
        self.assertAlmostEqual(values["first"], 1)
        self.assertAlmostEqual(values["steady"], 2)
        invalid = [sample_log().replace("VALIDATION mapping=pass", ""),
                   sample_log().replace("VALIDATION mapping=pass", "VALIDATION mapping=fail"),
                   sample_log().replace("expected=42", "expected=43"),
                   sample_log().replace("status=pass", "status=fail"),
                   sample_log().replace("batch=2", "batch=1"),
                   sample_log().replace("batch=2", "batch=3"),
                   sample_log().replace("operations=10", "operations=9"),
                   sample_log() + "CHECKSUM actual=42 expected=42 status=pass\n"]
        for text in invalid:
            with self.subTest(text=text), self.assertRaises(ValueError):
                driver.parse_samples(text, 2, 10)
        weighted = sample_log().replace("batch=2 operations=10 seconds=0.000020000", "batch=2 operations=20 seconds=0.000060000")
        self.assertAlmostEqual(driver.parse_samples(weighted, 2)[1]["steady"], 80 / 30)
        self.assertEqual(driver.summarize([1])["cv_percent"], "")
        self.assertAlmostEqual(driver.summarize([1, 3])["p95_us"], 2.9)

    def test_dry_run_uses_no_subprocess_or_host_affinity_and_accepts_fixed_pool(self):
        with tempfile.TemporaryDirectory() as tmp, contextlib.redirect_stdout(io.StringIO()) as output:
            out = Path(tmp) / "not-created"
            with mock.patch.object(driver.subprocess, "Popen", side_effect=AssertionError("subprocess forbidden")), \
                 mock.patch.object(driver.os, "sched_getaffinity", side_effect=AssertionError("host probe forbidden")):
                code = driver.main(["--cpus", "0-31", "--threads", "32", "--scenario", "fixed", "--dry-run", "--out", str(out)])
            self.assertEqual(code, 0)
            self.assertFalse(out.exists())
            plan = json.loads(output.getvalue())
            self.assertEqual(len(plan["order"]), 10)
            self.assertEqual(plan["cases"]["fixed/native"]["environment"]["OMP_THREAD_LIMIT"], "32")

    def test_switch_rejects_odd_or_invalid_values_without_subprocess(self):
        with contextlib.redirect_stderr(io.StringIO()), mock.patch.object(driver.subprocess, "Popen", side_effect=AssertionError):
            for arguments in (["--iterations", "3"], ["--warmup", "1"], ["--timeout", "nan"], ["--repeats", "1"]):
                with self.subTest(arguments=arguments):
                    self.assertEqual(driver.main(["--dry-run", *arguments]), 2)

    def fake_run(self, command, cwd, env, log, timeout):
        self.calls.append((command, Path(cwd), dict(env)))
        self.assertNotIn("KMP_AFFINITY", env)
        log = Path(log)
        if command[0] == "mock++":
            Path(command[command.index("-o") + 1]).write_bytes(b"mock binary")
            log.write_text("mock compiler\n")
        elif command[0] == "ldd":
            log.write_text("mock linked runtime\n")
        else:
            mode = command[command.index("--mode") + 1]
            scenario = command[command.index("--scenario") + 1]
            if "--probe" in command:
                text = "MAP threads=2 cpus=0,2\n"
                if scenario == "switch":
                    text += "MAP threads=4 cpus=0,1,2,3\n"
                if mode == "framework":
                    maps = Path(command[command.index("--map") + 1]).read_text()
                    self.assertIn("2;0,2\n", maps)
                log.write_text(text)
            else:
                self.assertIn("--map", command)
                self.assertTrue(Path(command[command.index("--map") + 1]).is_file())
                log.write_text(sample_log(first=1e-6 if mode == "native" else 2e-6,
                                          steady=2e-6 if mode == "native" else 3e-6))
                self.assertEqual(env["OMP_PROC_BIND"], "spread" if mode == "native" else "false")
                self.assertFalse((Path(cwd) / "inputsp.data").exists())
        return 0

    def test_mock_end_to_end_build_probe_and_repeated_statistics(self):
        self.calls = []
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "results"
            with mock.patch.object(driver, "run_logged", side_effect=self.fake_run), \
                 mock.patch.object(driver, "capture_metadata", side_effect=lambda path, *args: (path / "metadata").mkdir()), \
                 mock.patch.object(driver.os, "sched_getaffinity", return_value={0, 1, 2, 3}), \
                 mock.patch.dict(driver.os.environ, {"KMP_AFFINITY": "compact", "OMP_PLACES": "cores"}), \
                 mock.patch.object(driver.subprocess, "Popen", side_effect=AssertionError("real processes forbidden")), \
                 contextlib.redirect_stdout(io.StringIO()):
                code = driver.main(["--cpus", "0-3", "--threads", "2", "--other-threads", "4", "--repeats", "2",
                                    "--iterations", "10", "--batches", "2", "--cxx", "mock++", "--out", str(out)])
            self.assertEqual(code, 0)
            with (out / "samples.csv").open() as stream:
                samples = list(csv.DictReader(stream))
            with (out / "summary.csv").open() as stream:
                summary = list(csv.DictReader(stream))
            with (out / "batches.csv").open() as stream:
                batches = list(csv.DictReader(stream))
            self.assertEqual(len(samples), 8)
            self.assertEqual(len(summary), 8)
            self.assertEqual(len(batches), 24)
            self.assertTrue(all(row["samples"] == "2" for row in summary))
            self.assertTrue((out / "summary.md").exists())
            self.assertTrue((out / "metadata/binary.sha256").exists())
            self.assertEqual(sum("--probe" in command for command, _, _ in self.calls), 4)


if __name__ == "__main__":
    unittest.main()
