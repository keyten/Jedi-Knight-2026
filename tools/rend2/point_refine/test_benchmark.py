import copy
import unittest
from benchmark import REQUIRED, validate


class WitnessTests(unittest.TestCase):
    def witness(self):
        return {"schema": "org.openjk.point-refinement-tests", "version": 1,
                "checks": 1500, "failures": 0,
                "cases": [{"name": name, "before_world": 10, "after_world": .1,
                           "locally_identifiable": False, "sources": [{"q": [1, 2, 3]}]}
                          for name in sorted(REQUIRED)]}

    def test_complete_positive(self):
        validate(self.witness())

    def test_partial_or_failed_is_not_pass(self):
        for field, value in [("checks", 20), ("failures", 1), ("version", 9)]:
            w = self.witness()
            w[field] = value
            with self.assertRaises(ValueError):
                validate(w)
        w = self.witness()
        w["cases"].pop()
        with self.assertRaises(ValueError):
            validate(w)

    def test_uncertainty_and_no_improvement(self):
        for name in ("symmetric_two_receivers", "parallel_direction_depth_ambiguity", "prior_driven_ambiguity", "open_multinormal"):
            w = self.witness()
            c = next(c for c in w["cases"] if c["name"] == name)
            c["locally_identifiable"] = True
            c["after_world"] = 10
            with self.assertRaises(ValueError):
                validate(w)

    def test_nonfinite_and_negative(self):
        for bad in (-1, float("nan"), float("inf")):
            w = copy.deepcopy(self.witness())
            w["cases"][0]["sources"][0]["q"][0] = bad
            with self.assertRaises(ValueError):
                validate(w)


if __name__ == "__main__":
    unittest.main()
