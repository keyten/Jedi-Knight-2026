import unittest
import zipfile

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


if __name__ == '__main__':
    unittest.main()
