import unittest
from measure_physicalization_coverage import parse_report


class CoverageTests(unittest.TestCase):
    def test_coverage_counts_parts_and_files_separately(self):
        log = ('FXAUDIT|effects/fire.efx|0|1|3|0|0|0|0|3|0\n'
               'FXAUDIT|effects/fire.efx|1|1|2|2|0|0|2|0|0\n'
               'FXAUDIT_END|effects/fire.efx|2\n'
               'FXAUDIT|effects/new.efx|0|1|1|0|0|1|0|0|0\n'
               'FXAUDIT_END|effects/new.efx|1\n')
        result = parse_report(log, ['effects/fire.efx','effects/new.efx'])
        self.assertEqual(result['stages']['stage1']['effects'], 1)
        self.assertEqual(result['stages']['composite']['effects'], 2)
        self.assertEqual(result['stages']['emission']['effects'], 2)
        self.assertEqual(result['stages']['emission']['sprite_primitives'], 3)
        self.assertEqual(result['stages']['emission']['shader_alternatives'], 6)
        self.assertEqual(result['adaptive_alternatives'], 2)

    def test_unparsed_files_are_explicit_failures(self):
        result = parse_report('FXAUDIT_ERROR|effects/missing.efx\n', ['effects/missing.efx'])
        self.assertEqual(result['failed_effect_paths'], ['effects/missing.efx'])
        self.assertEqual(result['parsed_effects'], 0)

    def test_out_of_scope_fixture_and_incomplete_rows_not_counted(self):
        log = ('FXAUDIT|effects/test.efx|0|1|1|1|0|0|0|0|0\n'
               'FXAUDIT_END|effects/test.efx|1\n'
               'FXAUDIT|effects/partial.efx|0|1|1|1|0|0|0|0|0\n')
        result = parse_report(log, ['effects/partial.efx'])
        self.assertEqual(result['shader_alternatives'], 0)
        self.assertEqual(result['stages']['stage1']['effects'], 0)


if __name__ == '__main__': unittest.main()
