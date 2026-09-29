"""Regression tests for inventory correctness, using synthetic assets only."""
import io
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

import audit_stock_fx as audit


class AuditTests(unittest.TestCase):
    def test_groups_lists_empty_directives_and_duplicate_names(self):
        text = '''// comment
Particle
{
 name Copy of Unnamed Particle 0
 shader
 [
 gfx/misc/steam
 gfx/effects/alpha_smoke.tga
 ]
 alpha
 {
  end 0
  flags linear nonlinear
 }
}
Particle
{
 name Copy of Unnamed Particle 0
}
'''
        roots = audit.parse(text)
        self.assertEqual(len(roots), 2)
        self.assertEqual(audit.fields(roots[0]['group'])['name'], ['Copy', 'of', 'Unnamed', 'Particle', '0'])
        shader = audit.parse('gfx/test\n{\n {\n glow\n rgbGen vertex\n blendFunc add\n }\n}\n')
        stage = audit.fields(shader[0]['group'][0]['group'])
        self.assertEqual(stage['glow'], [])
        self.assertEqual(stage['rgbgen'], ['vertex'])
        self.assertEqual(stage['blendfunc'], ['add'])
        shader = audit.parse('gfx/test\n{\n nomipmaps\n {\n map gfx/test\n }\n}\n', shader_mode=True)
        self.assertEqual(audit.fields(shader[0]['group'])['nomipmaps'], [])
        self.assertEqual(shader[0]['group'][1]['key'], '')

    def test_malformed_groups_fail_instead_of_silent_coverage(self):
        for text in ('Particle\n{\n life 400', 'Particle\n{\nshader\n[\ngfx/test'):
            with self.assertRaises(ValueError):
                audit.parse(text)

    def test_overlay_and_extension_normalization(self):
        with tempfile.TemporaryDirectory() as temp:
            paths = [Path(temp) / name for name in ('assets1.pk3', 'zz_test.pk3')]
            for path, life in zip(paths, (100, 500)):
                with zipfile.ZipFile(path, 'w') as z:
                    z.writestr('Effects/Test.efx', 'Particle\n{\nlife %d\nshader gfx/effects/Wcloud.tga\n}\n' % life)
            corpus = audit.Corpus(paths)
            try:
                errors = []
                effects = audit.effects_inventory(corpus, errors)
                self.assertEqual(errors, [])
                self.assertEqual(effects[0]['primitives'][0]['numeric']['life_ms'], [500])
                self.assertEqual(effects[0]['primitives'][0]['shaders'], ['gfx/effects/wcloud'])
                self.assertEqual(effects[0]['source']['archive'], 'zz_test.pk3')
                self.assertEqual(len(corpus.versions['effects/test.efx']), 2)
            finally:
                corpus.close()

    def test_bsp_entities_and_invalid_offset(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / 'assets.pk3'
            entities = b'{\n"classname" "fx_runner"\n"fxFile" "volumetric/steam"\n"delay" "400"\n}\x00'
            with zipfile.ZipFile(path, 'w') as z:
                z.writestr('maps/test.bsp', struct.pack('<4siii', b'RBSP', 1, 16, len(entities)) + entities)
                z.writestr('maps/bad.bsp', struct.pack('<4siii', b'RBSP', 1, -8, len(entities)) + entities)
            corpus = audit.Corpus([path])
            try:
                errors = []
                maps, usage = audit.map_inventory(corpus, errors)
                self.assertEqual(len(maps), 1)
                self.assertEqual(len(errors), 1)
                self.assertEqual(usage[0]['effect'], 'effects/volumetric/steam.efx')
                self.assertEqual(usage[0]['entity']['delay'], '400')
            finally:
                corpus.close()

    def test_conflicting_shader_roles_do_not_enable_medium(self):
        p = {'type': 'particle', 'name': 'Smoke', 'shaders': ['gfx/misc/black_smoke', 'gfx/effects/whiteflare'],
             'fields': {}, 'groups': {}}
        result = audit.classify('effects/test.efx', p)
        self.assertEqual(result['material'], 'mixed_shader_roles')
        self.assertEqual(result['representation'], 'none')
        p['groups']['volumetricmedia'] = {'extinction': ['0']}
        self.assertEqual(audit.classify('effects/test.efx', p)['representation'], 'explicit_particle')

    def test_requested_tga_can_resolve_jpeg(self):
        try:
            from PIL import Image
        except ImportError:
            self.skipTest('optional Pillow is not installed')
        with tempfile.TemporaryDirectory() as temp:
            buffer = io.BytesIO()
            Image.new('RGB', (8, 8), 'white').save(buffer, format='JPEG')
            path = Path(temp) / 'assets.pk3'
            with zipfile.ZipFile(path, 'w') as z:
                z.writestr('gfx/test.jpg', buffer.getvalue())
            corpus = audit.Corpus([path])
            try:
                shaders = {'gfx/test': [{'stages': [{'maps': ['gfx/test.tga'], 'blend': ['gl_one', 'gl_one']}]}]}
                image = audit.texture_metrics(corpus, shaders, {'gfx/test'})['gfx/test.tga']
                self.assertEqual(image['status'], 'ok')
                self.assertEqual(image['resolved'], 'gfx/test.jpg')
                self.assertEqual(image['effective_mask'], 'luminance')
            finally:
                corpus.close()


if __name__ == '__main__':
    unittest.main()
