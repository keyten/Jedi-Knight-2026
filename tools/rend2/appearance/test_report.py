import unittest
import json
from pathlib import Path
import tempfile
import hashlib
from tools.rend2.appearance.report import eligible, seal
from tools.rend2.appearance.run import semantic


class ReportTests(unittest.TestCase):
    def example(self):
        return dict(field_code=0, metrics=dict(geometry_L0=dict(available=8, requested=20)),
                    common_domain={k: dict(rmse_rgb8=v) for k, v in
                                   (('constant', 50), ('legacy_grid_entity_proxy', 40), ('geometry_L0', 30))},
                    support_check_blocked=0)

    def test_missing_not_black(self):
        x = self.example()
        x['field_code'] = 1
        self.assertFalse(eligible(x))
        x['field_code'] = 0
        x['common_domain']['geometry_L0']['rmse_rgb8'] = None
        self.assertFalse(eligible(x))

    def test_tiny_support_cannot_win(self):
        x = self.example()
        self.assertTrue(eligible(x))
        x['metrics']['geometry_L0']['available'] = 3
        self.assertFalse(eligible(x))

    def test_wall_cannot_win(self):
        x = self.example()
        x['support_check_blocked'] = 1
        self.assertFalse(eligible(x))

    def test_grid_superiority_is_retained(self):
        x = self.example()
        x['common_domain']['legacy_grid_entity_proxy']['rmse_rgb8'] = 20
        self.assertFalse(eligible(x))

    def test_zero_baseline_cannot_claim_improvement(self):
        x = self.example()
        for v in x['common_domain'].values():
            v['rmse_rgb8'] = 0
        self.assertFalse(eligible(x))

    def test_repeat_only_ignores_resource_timings(self):
        self.assertEqual(semantic(dict(seconds=1, chunk_content_key='a')),
                         semantic(dict(seconds=2, chunk_content_key='a')))
        self.assertNotEqual(semantic(dict(chunk_content_key='a')), semantic(dict(chunk_content_key='b')))

    def test_manifest_hashes_portable_lf_bytes(self):
        with tempfile.TemporaryDirectory() as folder:
            out = Path(folder)
            (out / 'receipt.json').write_bytes(b'{\r\n  "supported": false\r\n}\r\n')
            seal(out)
            data = (out / 'receipt.json').read_bytes()
            manifest = json.loads((out / 'manifest.json').read_text())
            self.assertNotIn(b'\r', data)
            self.assertEqual(manifest['artifacts']['receipt.json'], hashlib.sha256(data).hexdigest())


if __name__ == '__main__':
    unittest.main()
