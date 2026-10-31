"""Audit reporting never converts a completed process into recovered lights."""
import unittest
from architecture import audit
from collect import verdict


class AuditTest(unittest.TestCase):
    def test_no_runtime_dependency_in_numerical_modules(self):
        self.assertTrue(audit()['dependency_check_pass'])

    def test_successful_process_can_have_failed_solver(self):
        self.assertEqual(verdict(dict(state='Completed', profiles={
            'isq': {'producer': {'state': 'Budget'}},
            'linear': {'producer': {'state': 'Failed'}}})), 'NoAcceptedFit')

    def test_missing_is_not_black_or_success(self):
        self.assertEqual(verdict({'state': 'NativeRejected'}), 'NotEvaluated')

    def test_structural_completion_is_not_physical_certification(self):
        self.assertEqual(verdict({'profiles': {'isq': {'producer': {'state': 'Completed'}}}}),
                         'ConditionalFitOnly')


if __name__ == '__main__':
    unittest.main()
