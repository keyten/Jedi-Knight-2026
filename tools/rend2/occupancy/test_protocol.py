"""X13 source metadata parser and frozen public semantics. GPL-2.0-or-later."""
import json
from pathlib import Path
import unittest
import run


class OccupancyProtocol(unittest.TestCase):
    def test_entity_identity_is_not_a_pose(self):
        rows=run.entities(b'{"classname" "func_door" "model" "*3" "origin" "1 2 3"}{"classname" "trigger_once" "model" "*4"}')
        self.assertEqual(rows[0]['model'],'*3')
        self.assertNotIn('bake_state',rows[0])
        self.assertNotIn('swept_bounds',rows[1])

    def test_malformed_entity_rejected(self):
        for value in (b'{"key"}',b'{{}}',b'{"key" "value"'):
            with self.assertRaises(ValueError):run.entities(value)

    def test_frozen_result(self):
        path=Path(__file__).parent/'results/v1/stock.json'
        if not path.exists():path=run.ROOT/'build/x13-run/stock.json'
        if not path.exists():self.skipTest('Run stock diagnostics first')
        for name,record in json.loads(path.read_text()).items():
            self.assertIn(name,run.MAPS)
            self.assertEqual(record['historical_profile'],'UnknownProfile')
            self.assertEqual(record['source_truth'],'Unavailable')
            for profile in ('isq','linear'):
                outcome=record[profile]
                self.assertIn('status',outcome)
                if 'occupancy' in outcome:
                    self.assertFalse(outcome['occupancy']['claims_historical_state'])
                    self.assertEqual(sum(outcome['occupancy']['bake_classes']),outcome['occupancy']['offered_candidates'])


if __name__=='__main__':unittest.main()
