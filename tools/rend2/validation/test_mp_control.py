"""MP protocol/negative controls, never a game launch or a real A/A claim."""
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch
from PIL import Image
import numpy as np
from tools.rend2.validation import mp_control, runtime_mp


def section(label, paused='1', camera='1 2 3', world=True):
    draws = '2/3/4 shaders/batches/surfs 5 leafs 12 verts 12/12 tris 0.0 mtex 0.0 dc' if world else ''
    return (f'X19_{label}_BEGIN\ncl_paused = "{paused}"\nsv_paused = "{paused}"\n'
            f'maps/mp/duel4.bsp ({camera}) : 90\ncg_fov = "80"\n{draws}\nX19_{label}_END\n')


class MPControl(unittest.TestCase):
    def validate(self, text):
        return mp_control.validate_sections(text, 'mp/duel4', lambda *args: None)

    def test_menu_precedes_stock_termination(self):
        stock = '{ loadmenu { "ui/stock.menu" } }\n'
        combined = mp_control.menu_list(stock)
        self.assertTrue(combined.startswith('loadmenu { "ui/recon-freeze.menu" }'))
        self.assertTrue(combined.endswith(stock))

    def test_frozen_world_control(self):
        rows = self.validate(''.join(section(x) for x in mp_control.LABELS))
        self.assertEqual(len(rows), 5)
        self.assertTrue(all(r['world_frames'] == 1 for r in rows))

    def test_menu_without_world_fails(self):
        with self.assertRaisesRegex(ValueError, 'visible world'):
            self.validate(''.join(section(x, world=False) for x in mp_control.LABELS))

    def test_pause_and_camera_are_mandatory(self):
        for changed in (section('B', paused='0'), section('B', camera='1 2 4')):
            text = ''.join(changed if x == 'B' else section(x) for x in mp_control.LABELS)
            with self.assertRaises(ValueError):
                self.validate(text)

    def test_missing_and_duplicate_blocks_fail(self):
        text = ''.join(section(x) for x in mp_control.LABELS)
        for changed in (text.replace('X19_B_END', ''), text + section('A')):
            with self.assertRaises(ValueError):
                self.validate(changed)

    def test_image_aa_and_moving_negative_control(self):
        from tools.rend2.gl43_validation.compare_images import compare
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            image = np.arange(64 * 36 * 3, dtype=np.uint8).reshape((36, 64, 3))
            for label in mp_control.LABELS:
                Image.fromarray(image).save(root / ('x19-' + label.lower() + '.tga'))
            r = mp_control.validate_images(root, compare, runtime_mp.legacy.IMAGE_TOLERANCE)
            self.assertTrue(r['aa']['passed'] and r['ab']['passed'])
            Image.fromarray(np.roll(image, 1, axis=1)).save(root / 'x19-aa1.tga')
            with self.assertRaisesRegex(ValueError, 'A/A'):
                mp_control.validate_images(root, compare, runtime_mp.legacy.IMAGE_TOLERANCE)
            Image.fromarray(np.zeros_like(image)).save(root / 'x19-aa1.tga')
            with self.assertRaisesRegex(ValueError, 'flat'):
                mp_control.validate_images(root, compare, runtime_mp.legacy.IMAGE_TOLERANCE)

    def test_no_permission_no_process_or_preparation(self):
        with tempfile.TemporaryDirectory() as folder:
            args = SimpleNamespace(output=Path(folder) / 'run', maps=['mp/duel4'],
                                   profiles=['legacy', 'modern'], allow_mp_launch=False)
            with patch.object(runtime_mp.subprocess, 'Popen', side_effect=AssertionError('launch')):
                with patch.object(runtime_mp.legacy, 'prepare_runtime', side_effect=AssertionError('prep')):
                    rows = runtime_mp.run(args)
                    self.assertEqual([r['status'] for r in rows], ['Unperformed', 'Unperformed'])
                    self.assertEqual(runtime_mp.run_case(args, 'mp/duel4', 'legacy', 0)['status'], 'Unperformed')

    def test_one_launch_budget_and_no_retry(self):
        with tempfile.TemporaryDirectory() as folder:
            args = SimpleNamespace(output=Path(folder) / 'run', maps=['mp/duel4'],
                                   profiles=['legacy', 'modern'], allow_mp_launch=True, max_launches=1)
            with patch.object(runtime_mp, 'run_case', return_value=dict(status='Failed', reason='invalid A/A')) as call:
                with patch('builtins.print'):
                    rows = runtime_mp.run(args)
                self.assertEqual(call.call_count, 1)
                self.assertEqual([r['status'] for r in rows], ['Failed', 'Unperformed'])

    def test_command_stream_has_no_network_changes(self):
        stream = '\n'.join(mp_control.commands('mp/duel4', runtime_mp.legacy.CONSUMERS))
        self.assertNotIn('net_', stream)
        self.assertNotIn('connect ', stream)
        self.assertLess(stream.index('X19_AA1_END'), stream.index('X19_B_BEGIN'))

    def test_missing_input_remains_unperformed(self):
        with tempfile.TemporaryDirectory() as folder:
            args = SimpleNamespace(output=Path(folder) / 'run', maps=['mp/duel4'],
                                   profiles=['legacy'], allow_mp_launch=True, max_launches=1)
            with patch.object(runtime_mp, 'run_case', side_effect=FileNotFoundError('missing engine')):
                with patch('builtins.print'):
                    rows = runtime_mp.run(args)
                self.assertEqual(rows[0]['status'], 'Unperformed')


if __name__ == '__main__':
    unittest.main()
