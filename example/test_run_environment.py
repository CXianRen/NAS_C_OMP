#!/usr/bin/env python3
"""Check launcher environment handling without building or running OpenMP code."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class RunEnvironmentTests(unittest.TestCase):
    settings = {
        'OMP_NUM_THREADS': '3',
        'OMP_PLACES': 'cores',
        'OMP_PROC_BIND': 'spread',
        'OMP_DYNAMIC': 'true',
        'OMP_THREAD_LIMIT': '13',
        'OMP_WAIT_POLICY': 'active',
        'KMP_AFFINITY': 'verbose,compact',
        'GOMP_CPU_AFFINITY': '1 2',
    }

    def launch(self, tuner=None, settings=None, target='run'):
        environment = {
            key: value for key, value in os.environ.items()
            if not key.startswith(('OMP_', 'KMP_', 'GOMP_'))
            and key not in ('TUNER', 'MAKEFLAGS', 'MFLAGS', 'MAKEOVERRIDES')
        }
        environment.update(self.settings if settings is None else settings)
        with tempfile.TemporaryDirectory(prefix='example-run-env-') as temporary:
            program = Path(temporary) / 'capture_environment'
            program.write_text(
                '#!/usr/bin/env python3\n'
                'import json, os\n'
                'print(json.dumps({key: value for key, value in os.environ.items() '
                'if key.startswith(("OMP_", "KMP_", "GOMP_")) or key == "TUNER"}))\n'
            )
            program.chmod(0o755)
            command = ['make', '-s', '--no-print-directory', '-o', str(program),
                       f'TARGET={program}', f'BUILD_DIR={temporary}/build', 'THREADS=6', target]
            if tuner is not None:
                command.append(f'TUNER={tuner}')
            result = subprocess.run(command, cwd=Path(__file__).resolve().parent,
                                    env=environment, text=True, capture_output=True,
                                    timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            return json.loads(result.stdout)

    def assert_passthrough(self, tuner, effective_tuner):
        for target in ('run', 'test'):
            with self.subTest(target=target):
                self.assertEqual(self.launch(tuner, target=target),
                                 dict(self.settings, TUNER=effective_tuner))

    def test_none_preserves_environment(self):
        self.assert_passthrough('none', 'none')

    def test_default_preserves_environment(self):
        self.assert_passthrough(None, 'none')

    def test_empty_preserves_environment(self):
        self.assert_passthrough('', '')

    def test_case_insensitive_none_preserves_environment(self):
        self.assert_passthrough('NoNe', 'NoNe')

    def test_none_does_not_add_openmp_defaults(self):
        self.assertEqual(self.launch('none', settings={}), {'TUNER': 'none'})

    def test_enabled_tuner_keeps_manual_binding_environment(self):
        expected = dict(self.settings, TUNER='dummy', OMP_PROC_BIND='false',
                        OMP_DYNAMIC='false', OMP_NUM_THREADS='6')
        for key in ('OMP_PLACES', 'KMP_AFFINITY', 'GOMP_CPU_AFFINITY'):
            expected.pop(key)
        self.assertEqual(self.launch('dummy'), expected)


if __name__ == '__main__':
    unittest.main()
