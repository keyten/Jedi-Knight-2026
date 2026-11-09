"""Mathematical/admission counterexamples, not historical-source certificates."""
import unittest
from tools.rend2.idf.prototype import limiter, response, vote


def row(i,p,n,b):
    return dict(id=str(i),position=p,normal=n,raw=[b]*3)


class IDF(unittest.TestCase):
    def test_positive_additivity_gives_bound_at_true_source(self):
        p=[0,0,10]; other=[8,0,10]
        rows=[row(i,[x,0,0],[0,0,1],0) for i,x in enumerate((-8,-4,0,4,8))]
        for r in rows:
            r['raw']=[50*response(r,p)+25*response(r,other)]*3
        d=limiter(rows,p,[0]*5,'raw')
        self.assertGreaterEqual(d['bound'],50)
        for r in rows: r['raw']=[50*response(r,p)]*3
        self.assertAlmostEqual(limiter(rows,p,[0]*5,'raw')['bound'],50)

    def test_zero_coefficient_and_unknown_ray_supply_no_cap(self):
        rows=[row(0,[0,0,0],[0,0,-1],0),row(1,[0,0,0],[0,0,1],20)]
        self.assertIsNone(limiter(rows,[0,0,10],[0,2],'geometry')['bound'])
        self.assertEqual(limiter(rows,[0,0,10],[0,0],'geometry')['labels'],[1])

    def test_saturation_has_no_upper_bound_even_for_other_channels(self):
        r=row(0,[0,0,0],[0,0,1],10);r['raw']=[255,4,2]
        self.assertIsNone(limiter([r],[0,0,10],[0],'censored')['bound'])

    def test_raw_ablation_does_not_assume_unknown_geometry_is_clear(self):
        r=row(0,[0,0,0],[0,0,1],10)
        self.assertIsNone(limiter([r],[0,0,10],[2],'raw')['bound'])

    def test_unknown_transfer_breaks_physical_bound(self):
        r=row(0,[0,0,0],[0,0,1],4)
        r['raw']=[2]*3  # sqrt transfer maps linear B=4 to code value 2
        self.assertLess(limiter([r],[0,0,1],[0],'raw')['bound'],4)

    def test_votes_follow_limiter_changes_not_brightness_maximum(self):
        fields=[dict(labels=[i],bound=1) for i in range(3)]
        self.assertEqual(vote(fields),3)
        self.assertEqual(vote([dict(labels=[0],bound=1000)]*7),1)
        self.assertAlmostEqual(vote([dict(labels=list(range(64)))]*7),1)

    def test_noise_interval_contains_exact_single_source_intensity(self):
        p=[0,0,10];r=row(0,[0,0,0],[0,0,1],1)
        self.assertGreaterEqual(limiter([r],p,[0],'censored')['bound'],100)


if __name__=='__main__': unittest.main()
