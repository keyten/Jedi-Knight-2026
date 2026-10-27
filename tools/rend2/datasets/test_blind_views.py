"""Deterministic adversarial partition/access tests; no renderer or dependencies."""
import copy
import json
import math
from pathlib import Path
import tempfile
import unittest
from prepare_blind import assignment, eligible, prepare, quality, grid_views
from solver_view import TrainingView, FIELDS
from evaluation_view import EvaluationView


def sample(i, p):
    return dict(id=f'{i:064x}', image=0, page=0, x=i, y=0, surface=i, slot=0, style=0,
                coverage=2, issues=0, classification=1, fit_candidate=1,
                material_signals=1, material_uncertainty=7, kind=0, material='test/unknown',
                position=list(p), normal=[0,0,1], shading_normal=[0,0,1], area=1, area_error=0,
                footprint_radius=1, encoding=0, raw=[127,255,0], channel_flags=[1,9,3],
                transfer=0, primaries=0, source=None, scene=None, deluxe_state=1, deluxe=None,
                positive_region=0, negative_region=0, region_state=4, region_reasons=0,
                local_geometry_complete=0, local_geometry_issues=0)


class BlindTests(unittest.TestCase):
    def test_taxonomy(self):
        r=sample(0,[128]*3)
        q=quality(r)
        for flag in ('mapped','unknown_transfer','unknown_primaries','quantized','censored',
                     'local_geometry_uncertain','region_unassigned','deluxe_unavailable','material_uncertain'):
            self.assertIn(flag,q)
        self.assertNotIn('source_rgb_comparable_conditional',q)
        r['source']=[.5,.5,.5]; r['transfer']=1
        self.assertIn('source_relative_available',quality(r))
        self.assertNotIn('source_rgb_comparable_conditional',quality(r))
        r['coverage']=4
        self.assertIn('unsupported_geometry',quality(r)); self.assertFalse(eligible(r))
        r['coverage']=3
        self.assertIn('ambiguous_overlap',quality(r))

    def test_spatial_and_footprint_gap(self):
        for x in (-.001, 0, .001, 1023.999, 1024, 1024.001):
            self.assertIsNone(assignment(sample(0,[x,512,512]),1024,8,7))
        # Nearby samples cannot straddle a partition: excluded gap exceeds their support.
        for x in range(-1050,1050,3):
            a,b=sample(0,[x,512,512]),sample(1,[x+4,512,512])
            ar,br=assignment(a,1024,8,7),assignment(b,1024,8,7)
            self.assertTrue(ar is None or br is None or ar==br)
        r=sample(0,[512]*3); r['footprint_radius']=180
        self.assertIsNone(assignment(r,1024,8,7)) # skinny/curved large support

    def test_physical_isolation(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t); prefix=root/'fixture'
            source=dict(map_sha256='ab'*32,pages=[dict(image=0,width=40,height=1,missing=0)],
                        derived_complete=1,compiler_known=1,synthetic=1)
            prefix.with_suffix('.source.json').write_text(json.dumps(source))
            prefix.with_suffix('.public.jkobs').write_bytes(b'geometry-only test member')
            records=[sample(i,[i*1024+512,512,512]) for i in range(40)]
            prefix.with_suffix('.rows.jsonl').write_text(''.join(json.dumps(r)+'\n' for r in records))
            report=prepare(prefix,root/'one'); prepare(prefix,root/'two')
            self.assertTrue(report['all_pages_complete'])
            for name in ('train','validation','heldout'):
                self.assertGreater(report['partitions'][name],0)
            view=TrainingView(root/'one/inputs','ab'*32)
            one=list(view.observations()); self.assertEqual(one,list(view.observations()))
            self.assertTrue(all(set(r)==FIELDS for r in one))
            private=[]
            for role in ('validation','heldout'):
                private += [json.loads(r) for r in (root/f'one/evaluator/{role}.jsonl').read_text().splitlines()]
            self.assertFalse({r['id'] for r in one}&{r['id'] for r in private})
            self.assertEqual(sorted(p.name for p in (root/'one/inputs').iterdir()),
                             ['geometry.jkobs','grid.jsonl','manifest.json','train.jsonl'])
            self.assertFalse(hasattr(view,'heldout')); self.assertFalse(hasattr(view,'dataset'))
            with self.assertRaises(ValueError): TrainingView(root/'one/evaluator','ab'*32)
            with self.assertRaises(ValueError): TrainingView(root/'one/inputs','cd'*32)
            evaluator=EvaluationView(root/'one/evaluator','ab'*32)
            self.assertTrue(list(evaluator.observations('validation')))
            with self.assertRaises(PermissionError): list(evaluator.observations('heldout'))
            final=EvaluationView(root/'one/evaluator','ab'*32,permitted=('heldout',))
            self.assertTrue(list(final.observations('heldout')))
            for member in ('train.jsonl','geometry.jkobs','manifest.json'):
                self.assertEqual((root/'one/inputs'/member).read_bytes(),(root/'two/inputs'/member).read_bytes())
            train=root/'one/inputs/train.jsonl'; train.write_text(train.read_text()+'corrupt')
            with self.assertRaises(ValueError): list(view.observations())

    def test_duplicate_and_partial(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t); prefix=root/'f'
            prefix.with_suffix('.source.json').write_text(json.dumps(dict(map_sha256='ab'*32,
                pages=[dict(image=0,width=2,height=1,missing=0)],derived_complete=0)))
            prefix.with_suffix('.public.jkobs').write_bytes(b'public')
            r=sample(0,[512]*3)
            prefix.with_suffix('.rows.jsonl').write_text(json.dumps(r)+'\n')
            self.assertFalse(prepare(prefix,root/'partial')['all_pages_complete'])
            prefix.with_suffix('.rows.jsonl').write_text((json.dumps(r)+'\n')*2)
            with self.assertRaises(ValueError): prepare(prefix,root/'duplicate')

    def test_grid_record_isolation(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t); inputs=root/'inputs'; evaluator=root/'evaluator'
            inputs.mkdir(); evaluator.mkdir()
            items=[dict(record=0,ordinal=i,position=[512+1024*i,512,512],footprint_radius=1)
                   for i in range(40)]
            self.assertGreater(len({assignment(r,1024,8,7) for r in items}),1)
            path=root/'grid.jsonl'; path.write_text(''.join(json.dumps(r)+'\n' for r in items))
            result=grid_views(path,inputs,evaluator,'ab'*32,1024,8,7)
            self.assertEqual(result['withheld_shared_or_gap'],40)
            self.assertEqual((inputs/'grid.jsonl').stat().st_size,0)
            # Unique records can be emitted, but never to two roles.
            for i,r in enumerate(items): r['record']=i
            path.write_text(''.join(json.dumps(r)+'\n' for r in items))
            result=grid_views(path,inputs,evaluator,'ab'*32,1024,8,7)
            self.assertEqual(sum(result.values()),40)

    def test_image_alternatives_are_not_double_counted(self):
        with tempfile.TemporaryDirectory() as t:
            root=Path(t); prefix=root/'f'
            source=dict(map_sha256='ab'*32,derived_complete=1,pages=[
                dict(image=0,page=0,width=1,height=1,missing=0),
                dict(image=1,page=0,width=1,height=1,missing=0)])
            prefix.with_suffix('.source.json').write_text(json.dumps(source))
            prefix.with_suffix('.public.jkobs').write_bytes(b'public')
            a=sample(0,[512]*3); b=copy.deepcopy(a); b['image']=1; b['id']='cd'*32
            prefix.with_suffix('.rows.jsonl').write_text(json.dumps(a)+'\n'+json.dumps(b)+'\n')
            report=prepare(prefix,root/'views')
            self.assertTrue(report['all_pages_complete'])
            self.assertEqual(sum(report['partitions'].values()),1)
            self.assertEqual(report['partition_exclusions']['alternate_source_representation'],1)


if __name__=='__main__': unittest.main()
