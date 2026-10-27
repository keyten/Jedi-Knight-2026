"""Optional real compiled-fixture replay, preserving original tolerances and hashes.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import hashlib
import json
from pathlib import Path
import unittest
from compiler_boundary import load
from compiler_trace import Replay
from geometry import unit
from compare_bakes import CASES,floor_samples,metrics
from bake_model import encode_color


class CompilerBoundary(unittest.TestCase):
    def test_original_witness_and_full_boundary(self):
        path=Path('build/bake-model-comparison-final/authoring/base/maps/multi_sun.bsp')
        if not path.exists(): self.skipTest('Pinned original compiled BSP unavailable')
        frozen=json.loads(Path('tools/rend2/bake_model/validation.json').read_text())
        self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(),frozen['cases']['multi_sun']['bsp_sha256'])
        lumps,_,casters=load(path); case=next(c for c in CASES if c['name']=='multi_sun')
        old=metrics(case,lumps)
        self.assertEqual(old['unchecked_sky_wall_boundary_channels'],105)
        self.assertEqual(old['unchecked_sky_wall_boundary_max_error'],28)
        trace=Replay(lumps,casters); boundary=0; mismatched=0
        for row in floor_samples(lumps):
            p=(row['world'][0],row['world'][1],1.)
            visible,_=trace.trace(p,unit((1.,0.,1.)))
            self.assertEqual(encode_color((120.+visible*40/2**.5,120.,120.)),row['rgb'])
            boundary+=abs(p[0]+15)<2
            mismatched+=encode_color((120.+(p[0]<-15)*40/2**.5,120.,120.))!=row['rgb']
        self.assertEqual(boundary,35); self.assertEqual(mismatched,17)


if __name__=='__main__': unittest.main()
