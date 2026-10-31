"""Adversarial dashboard checks: misses and unknowns cannot disappear."""
import copy
import unittest
from tools.rend2.uncertainty.benchmark import dashboard


class DashboardTests(unittest.TestCase):
    def witness(self):
        return dict(schema='org.openjk.uncertainty-population.v1',population='test',calibration_key='frozen',
            bins=[dict(event=0,bin=0,rate=.9,families=8,lower=.4,upper=1)],families=[
                dict(index=0,geometry='cal',split='calibration',stratum=0,true_sources=1,missed=0,joint_covered=True,trials=[]),
                dict(index=1,geometry='eval',split='evaluation',stratum=0,true_sources=2,missed=1,joint_covered=False,trials=[
                    dict(event=0,bin=0,label_known=True,label=False,rank=.9,rate_known=True,rate=.9,lower=.4),
                    dict(event=0,bin=0,label_known=True,label=True,rank=.9,rate_known=False,rate=0,lower=0)])])

    def test_false_confident_and_abstained_truth(self):
        result=dashboard(self.witness())
        self.assertEqual(result['missed_truth'],1)
        self.assertEqual(result['uncalibrated_candidates'],1)
        selected=[r for r in result['risk_coverage'] if r['mode']=='rate' and r['threshold']==.75][0]
        self.assertEqual(selected['risk'],1)
        self.assertEqual(selected['truth_recall'],0)
        self.assertEqual(selected['missed_or_abstained_truth'],2)
        self.assertGreater(result['existing_metric_diagnostic']['brier']['value'],.8)

    def test_unknown_is_not_zero_rate(self):
        data=self.witness();data['families'][1]['trials'][0]['rate_known']=False
        result=dashboard(data)
        self.assertIsNone(result['existing_metric_diagnostic']['brier']['value'])

    def test_reused_geometry_rejected(self):
        data=self.witness();data['families'][1]['geometry']='cal'
        with self.assertRaises(ValueError):dashboard(data)

    def test_split_overlap_rejected(self):
        data=copy.deepcopy(self.witness());data['families'][1]['index']=0
        with self.assertRaises(ValueError):dashboard(data)

    def test_calibration_admission_mismatch_rejected(self):
        data=self.witness();data['families'][1]['trials'][0]['calibration_eligible']=False
        with self.assertRaises(ValueError):dashboard(data)


if __name__=='__main__':unittest.main()
