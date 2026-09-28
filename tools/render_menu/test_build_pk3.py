import unittest
import zipfile
import json
import re

from build_pk3 import HERE, item, validate_sp_menu


class SPMenuValidationTests(unittest.TestCase):
    def test_symbolic_multi_is_valid(self):
        validate_sp_menu(item('test', 'Choice', 0, 0, 100,
                             'cvarFloatList { "Off" 0 "On" 1 }', 'ITEM_TYPE_MULTI'))

    def test_numeric_enums_are_rejected(self):
        good = item('test', 'Choice', 0, 0, 100)
        for symbol, numeric in [('ITEM_TYPE_BUTTON', '12'),
                                ('WINDOW_STYLE_EMPTY', '0'), ('ITEM_ALIGN_LEFT', '0')]:
            with self.subTest(symbol=symbol), self.assertRaises(AssertionError):
                validate_sp_menu(good.replace(symbol, numeric))
        with self.assertRaises(AssertionError):
            validate_sp_menu('menuDef { descAlignment 1 }')

    def test_multi_data_on_text_is_rejected(self):
        with self.assertRaises(AssertionError):
            validate_sp_menu(item('test', 'Choice', 0, 0, 100,
                                 'cvarFloatList { "Off" 0 "On" 1 }', 'ITEM_TYPE_TEXT'))

    def test_edit_data_on_multi_is_rejected(self):
        with self.assertRaises(AssertionError):
            validate_sp_menu(item('test', 'Choice', 0, 0, 100, 'maxChars 128', 'ITEM_TYPE_MULTI'))

    def test_packaged_menu(self):
        with zipfile.ZipFile(HERE / 'zzzz_render2026_menu.pk3') as archive:
            self.assertIsNone(archive.testzip())
            validate_sp_menu(archive.read('ui/ingame.menu').decode('cp1252'))

    def test_all_overlay_fields_have_safe_controls(self):
        with zipfile.ZipFile(HERE / 'zzzz_render2026_menu.pk3') as archive:
            text = archive.read('ui/ingame.menu').decode('cp1252')
        overlay = text[text.index('menuDef { name render2026_0'):]
        self.assertNotRegex(overlay, r'type\s+ITEM_TYPE_(?:EDITFIELD|NUMERICFIELD)')
        self.assertNotRegex(overlay, r'fullScreen\s+1')
        controls = json.loads((HERE/'controls.json').read_text())
        for c in controls:
            self.assertIn('ui_r2026_' + c['name'], overlay)
            if not c['enum'] and c['name'] != 'r_colorGradingLUT':
                self.assertLess(c['range'][0], c['range'][1])
        self.assertIn('cvarTest ui_r2026_restartPending enableCvar { "1" }', overlay)

    def test_mode_labels_and_navigation(self):
        with zipfile.ZipFile(HERE / 'zzzz_render2026_menu.pk3') as archive:
            text = archive.read('ui/ingame.menu').decode('cp1252')
        controls = json.loads((HERE/'controls.json').read_text())
        byname = {c['name']:c for c in controls}
        self.assertEqual(byname['r_hdr']['enum'], [['Off',0],['On',1]])
        self.assertEqual(byname['r_bloom']['enum'], [['Off',0],['Legacy',-1],['Modern',1]])
        names = set(re.findall(r'menuDef\s*\{\s*name\s+(render2026_\w+)',text))
        for target in re.findall(r'open\s+(render2026_\w+)',text): self.assertIn(target,names)


if __name__ == '__main__':
    unittest.main()
