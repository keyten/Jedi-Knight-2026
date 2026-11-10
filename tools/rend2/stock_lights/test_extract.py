"""Parser regressions that could silently lose or invent authored light data."""
import struct
import unittest

from tools.rend2.stock_lights.extract import (
    bsp_lumps, entities, light_record, scalar_directive, shader_definitions,
)


WORLD = b'{"classname" "worldspawn"}\n'


class ExtractionTests(unittest.TestCase):
    def test_comments_braces_in_values_and_literal_windows_paths(self):
        block = b'{ "classname" "light" "note" "} // literal" "path" "a\\b" }'
        result = entities(WORLD + b'/* { fake light } */\n' + block + b'\n\0')
        self.assertEqual(len(result), 2)
        self.assertEqual(result[1]["raw"].encode("latin1"), block)
        self.assertEqual(result[1]["keys"]["path"], "a\\b")

    def test_truncated_entities_are_errors_not_empty_light_sets(self):
        for raw in (b'', WORLD + b'{"classname" "light"',
                    WORLD + b'{"classname"}', WORLD + b'{"classname" "light}',
                    b'{"classname" "light"}'):
            with self.subTest(raw=raw), self.assertRaises(ValueError):
                entities(raw)

    def test_duplicate_entity_keys_preserved_with_runtime_first_match(self):
        es = entities(WORLD + b'{"classname" "light" "light" "250" "light" "900"}')
        self.assertEqual(es[1]["keys"]["light"], "250")
        self.assertEqual(es[1]["pairs"][-1], ("light", "900"))

    def test_authored_zero_negative_and_missing_fields_not_replaced_by_defaults(self):
        es = entities(WORLD + b'{"classname" "light" "light" "-50" "scale" "0" '
                      b'"origin" "1 2 3" "target" "aim"}\n'
                      b'{"classname" "info_null" "targetname" "aim" "origin" "4 5 6"}')
        record = light_record(es[1], es)
        self.assertEqual(record["light"], -50)
        self.assertEqual(record["scale"], 0)
        self.assertIsNone(record["color"])
        self.assertEqual(record["target_entities"][0]["entity_index"], 2)

    def test_invalid_rbsp_extents_and_wrong_game_rejected(self):
        valid = b'RBSP\x01\0\0\0' + bytes(144)
        self.assertEqual(len(bsp_lumps(valid)), 18)
        for offset, length in ((-1, 1), (152, 1), (8, 1), (152, -1)):
            bad = bytearray(valid)
            struct.pack_into('<ii', bad, 8, offset, length)
            with self.subTest(offset=offset, length=length), self.assertRaises(ValueError):
                bsp_lumps(bad)
        with self.assertRaises(ValueError):
            bsp_lumps(b'IBSP' + valid[4:])

    def test_commented_lights_glow_and_sun_metadata_remain_distinct(self):
        text = '''textures/test
{
 // q3map_surfacelight 9999
 sun 1 .5 .2 80 75 35 // sky
 q3map_surfacelight 100
 { map textures/glow
   glow
 }
}
textures/test
{
 { map textures/glow
   glow
 }
}'''
        definitions = list(shader_definitions(text))
        self.assertEqual([name for name, _ in definitions], ['textures/test'] * 2)
        shader = definitions[0][1]
        self.assertEqual(scalar_directive(shader, 'q3map_surfacelight'), 100)
        self.assertEqual(shader['directives'][0]['args'], ['1', '.5', '.2', '80', '75', '35'])
        self.assertTrue(definitions[1][1]['glow'])
        self.assertIsNone(scalar_directive(definitions[1][1], 'q3map_surfacelight'))


if __name__ == '__main__':
    unittest.main()
