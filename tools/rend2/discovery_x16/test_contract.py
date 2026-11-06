"""X16 admission/answer-boundary regressions; no synthetic physics substitute."""
import json
from pathlib import Path
import tempfile
import unittest
from tools.rend2.discovery_x16.fit import admit,frozen_geometry


class DiscoveryContract(unittest.TestCase):
    def test_fold_geometry_has_no_evaluation_values(self):
        with tempfile.TemporaryDirectory() as directory:
            path=Path(directory)/'public.txt'
            # Existing discovered proposals are public; no private/oracle source.
            row='15 2 0 1 1 0 0 0 0 0 1 16 1 1'
            path.write_text('JKJOINT1\n0\n1\n1 0 0 1\n1\n'+row+' 1 2 3\n1\n'+row+'\n1\n'+row+'\n')
            f=frozen_geometry(path)
            self.assertEqual(f['train'][0]['raw'],[1,2,3])
            for role in ('validation','heldout'):
                self.assertNotIn('raw',f[role][0])
                self.assertEqual(int(f[role][0]['id'],16)+1,15)

    def test_kind_region_fairness_unknown_score_and_order(self):
        with tempfile.TemporaryDirectory() as directory:
            base=Path(directory)/'new';base.mkdir()
            for shard in range(1,9):
                cs=[]
                for kind in (0,1):
                    cs.append(dict(id=str(shard*10+kind),profile='profile/isq/1',
                        source_kind=kind,kind=2,shape_score=0))
                    for index in range(3):
                        cs.append(dict(id=str(shard*100+kind*10+index),profile='profile/isq/1',
                            source_kind=kind,shape_score=None if index==2 else index*.01))
                (base/f'search-{shard}.json').write_text(json.dumps(dict(candidates=cs)))
            first=admit(base.parent,'new',0)
            self.assertEqual(len(first),16)
            self.assertEqual({c['shard'] for c in first},set(range(1,9)))
            self.assertEqual(sum(c['source_kind']==1 for c in first),8)
            self.assertTrue(all(c.get('kind',1)!=2 for c in first))
            for path in base.glob('*.json'):
                d=json.loads(path.read_text());d['candidates'].reverse();path.write_text(json.dumps(d))
            self.assertEqual(first,admit(base.parent,'new',0))
            self.assertEqual(admit(base.parent,'new',1),[])


if __name__=='__main__':unittest.main()
