"""Parser/attribution checks only; no benchmark is launched."""
import tempfile
from pathlib import Path
import unittest

from logs import compare, parse_log


class LogTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.path = Path(self.directory.name) / 'sample.log'

    def parse(self, extra='', total='2.000000000', klass='C'):
        self.path.write_text(f'''- SP Benchmark
 Class = {klass}
 Size = 162x 162x 162
 Iterations = 400
 Verification = SUCCESSFUL
 Time in seconds = 2.00
 iteration total: {total} s
parallel region compute_rhs:43  1.250000000 s  step: 62.5%
    for region compute_rhs:50  1.000000000 s  step: 50.0%
parallel region add:44  0.500000000 s  step: 25.0%
    for region add:44  0.500000000 s  step: 25.0%
{extra}
''')
        return parse_log(self.path)

    def test_parent_child_not_double_counted(self):
        value = self.parse()
        result = compare(value, value)
        self.assertEqual(result['decomposition']['baseline_parent_s'], 1.75)
        self.assertEqual(result['decomposition']['baseline_residual_s'], 0.25)
        self.assertIsNone(result['regions'][0]['share_of_total_delta_percent'])

    def test_workload_mismatch_rejected(self):
        left = self.parse()
        right = self.parse(klass='D')
        with self.assertRaisesRegex(ValueError, 'incomparable workload'):
            compare(left, right)

    def test_precision_marker_preferred(self):
        result = self.parse('OVERHEAD iteration_seconds=2.123456789')
        self.assertEqual(result['seconds'], 2.123456789)
        self.assertEqual(result['time_source'], 'wrapper')

    def test_duplicate_parent_rejected(self):
        with self.assertRaisesRegex(ValueError, 'duplicate parent'):
            self.parse('parallel region add:44  0.1 s  step: 5%')

    def test_multiple_runs_rejected(self):
        with self.assertRaisesRegex(ValueError, 'do not concatenate'):
            self.parse('iteration total: 3.0 s')

    def test_rounded_fallback_without_regions(self):
        self.path.write_text('Time in seconds = 1.23\n')
        value = parse_log(self.path)
        result = compare(value, value)
        self.assertIsNone(result['decomposition'])
        self.assertTrue(any('四舍五入' in warning for warning in result['warnings']))

    def test_changed_child_boundaries_not_matched(self):
        left = self.parse()
        right = self.parse('    for region compute_rhs:92-182 (nowait)  0.1 s')
        self.assertTrue(any('分组不同' in warning for warning in compare(left, right)['warnings']))


if __name__ == '__main__':
    unittest.main()
