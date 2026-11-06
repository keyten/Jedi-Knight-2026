"""X17 report contracts; native tests own the lighting/geometry equations."""
import unittest
from tools.rend2.path_support.report import cpu_result, coverage
from tools.rend2.path_support.run import bits, MAPS


class Reports(unittest.TestCase):
    def test_ctest_real_success(self):
        self.assertTrue(cpu_result('100% tests passed out of 45\nTotal Test time (real) = 282.35 sec')['passed'])
        self.assertTrue(cpu_result('100% tests passed, 0 tests failed out of 45')['passed'])
        self.assertFalse(cpu_result('97% tests passed, 1 tests failed out of 45')['passed'])
        self.assertFalse(cpu_result('process exited 0')['passed'])

    def test_nonempty_only(self):
        record=dict(branches=[dict(attempts=[dict(sources=0,coverage_used=64,coverage_admitted=64),
             dict(sources=1,coverage_used=55,coverage_admitted=64)])])
        self.assertEqual(coverage(record),[(55,64)])

    def test_scope_bits_not_visibility(self):
        self.assertEqual(bits((1<<15)|(1<<16)|(1<<17)),
                         ['BakeOccupancyUnknown','UnboundedOccupancy','ConditionalBakePath'])
        self.assertEqual(len(MAPS),8)


if __name__=='__main__': unittest.main()
