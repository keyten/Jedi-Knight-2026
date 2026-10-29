import copy
import unittest
from benchmark import SCENES, MODELS, validate


class Gates(unittest.TestCase):
    def data(self):
        a = dict(schema="org.openjk.s08-tests", version=1, checks=3000, failures=0,
                 cases=[dict(name=n, black_mse=10, heldout_mse=.01) for n in SCENES])
        b = dict(cases=[dict(name=n, retained_kinds=[1, 2], models=[dict(kind=k) for k in (1, 2, 3, 4)]) for n in MODELS])
        return a, b

    def test_positive(self):
        validate(*self.data())

    def test_black_or_missing_cannot_pass(self):
        for field, value in (("black_mse", 0), ("heldout_mse", 10), ("heldout_mse", float("nan"))):
            a, b = self.data()
            a["cases"][0][field] = value
            with self.assertRaises(ValueError):
                validate(a, b)
        a, b = self.data()
        a["cases"].pop()
        with self.assertRaises(ValueError):
            validate(a, b)

    def test_no_forced_ambiguity(self):
        a, b = self.data()
        next(c for c in b["cases"] if c["name"] == "missing_shape_evidence")["retained_kinds"] = [3]
        with self.assertRaises(ValueError):
            validate(a, b)


if __name__ == "__main__":
    unittest.main()
