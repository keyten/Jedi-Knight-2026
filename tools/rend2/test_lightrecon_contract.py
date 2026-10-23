#!/usr/bin/env python3
"""Contract check: regression detector must reject disabled-path mutations."""
import copy
import json
import unittest
import tempfile
from unittest import mock
from pathlib import Path
from lightrecon_regression import (BASELINE, differences, cpp_path, preprocess,
                                  production_audit, sources)


class Contracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.cpp = cpp_path()
        cls.case = json.loads((BASELINE / 'matrix.json').read_text())[0]
        cls.source = sources(cls.case)['fragment']
        cls.baseline = preprocess(cls.source, cls.cpp)

    def test_production_boundary(self):
        self.assertEqual(production_audit(), [])

    def test_lifecycle_owner_hooks_cannot_disappear(self):
        import lightrecon_regression as regression
        original = regression.read
        for file, removed in [('shared/rd-rend2/tr_bsp.cpp', 'reconstructedWorld.Finish('),
                              ('code/rd-rend2/tr_init.cpp', 'R_ClearReconstructedLighting();'),
                              ('codemp/rd-rend2/tr_init.cpp', 'R_ClearReconstructedLighting();')]:
            with self.subTest(file=file):
                def changed(path):
                    value = original(path)
                    return value.replace(removed, 'removed_hook(' if removed.endswith('(') else '') if path == file else value
                with mock.patch.object(regression, 'read', side_effect=changed):
                    self.assertTrue(regression.lifecycle_audit())

    def test_whitespace_comments_are_not_regressions(self):
        self.assertEqual(self.baseline, preprocess(self.source + '\n// unrelated formatting\n\n', self.cpp))

    def test_original_image_hooks_cannot_disappear(self):
        import lightrecon_regression as regression
        original = regression.read
        for removed in ('observations.Embedded(', 'observations.External(', 'observations.ReleaseSource()',
                        'observations.GridEmbedded(', 'observations.GridLayout(', 'observations.GridHdr(',
                        'observations.GridHdrSidecar('):
            with self.subTest(removed=removed):
                def changed(path):
                    text = original(path)
                    return text.replace(removed, 'removed_hook(') if path == 'shared/rd-rend2/tr_bsp.cpp' else text
                with mock.patch.object(regression, 'read', side_effect=changed):
                    self.assertTrue(regression.observation_audit())

    def test_original_surface_and_opt_in_debug_guards(self):
        import lightrecon_regression as regression
        original = regression.read
        for removed in ('observations.Surfaces(', 'observations.SurfaceMaterial('):
            def changed(path):
                text = original(path)
                return text.replace(removed, 'removed_hook(') if path == 'shared/rd-rend2/tr_bsp.cpp' else text
            with mock.patch.object(regression, 'read', side_effect=changed):
                self.assertTrue(regression.observation_audit())
        def depth_disabled(path):
            text = original(path)
            return text.replace('"*luxelDebug", lightmapsVertex', '"*luxelDebug", lightmaps2d') if path == 'shared/rd-rend2/tr_lightrecon.cpp' else text
        with mock.patch.object(regression, 'read', side_effect=depth_disabled):
            self.assertTrue(any('depth-tested' in failure for failure in regression.production_audit()))

    def test_disabled_future_branch_is_absent(self):
        added = '\n#ifdef USE_LIGHT_RECON_TEST\nuniform sampler3D u_LightReconTest;\n#endif\n'
        self.assertEqual(self.baseline, preprocess(self.source + added, self.cpp))

    def test_classification_source_guards(self):
        import lightrecon_regression as regression
        original = regression.read
        for path, token in [('shared/rd-rend2/tr_bsp.cpp', 'observations.VertexHdrSource('),
                            ('shared/rd-rend2/tr_bsp.cpp', 'authored = surf->shader;'),
                            ('shared/rd-rend2/tr_lightrecon.cpp', 'out.colour, in.color'),
                            ('shared/rd-rend2/tr_lightrecon.cpp', 'ds.vertexStyles[slot]')]:
            def changed(name):
                text = original(name)
                return text.replace(token, 'removed_capture') if name == path else text
            with mock.patch.object(regression, 'read', side_effect=changed):
                self.assertTrue(regression.observation_audit())

    def test_define_sampler_ubo_ssbo_and_work_mutations_are_detected(self):
        for label, added in [
            ('define', '#define USE_LIGHT_RECON_TEST\n'),
            ('sampler', 'uniform sampler3D u_TestAccidentalSampler;\n'),
            ('ubo', 'layout(std140) uniform AccidentalBlock { vec4 accidentalData; };\n'),
            ('ssbo', 'layout(std430, binding=0) buffer AccidentalStorage { vec4 data[]; };\n'),
            ('work', 'vec3 accidentalWork(vec3 x) { return sqrt(x) + exp(x); }\n'),
        ]:
            with self.subTest(label=label):
                actual = preprocess(self.source + added, self.cpp)
                diff = ''.join(differences(self.baseline, actual, label))
                self.assertIn('baseline/' + label, diff)
                self.assertIn('current/' + label, diff)
                self.assertNotEqual(actual, self.baseline)

    def test_binding_and_permutation_diffs_are_explicit(self):
        changed = copy.deepcopy(self.case)
        changed['settings']['r_forwardPlus'] = 1
        self.assertIn('r_forwardPlus', ''.join(differences(self.case, changed, 'permutation')))

    def test_bad_compiler_is_not_a_pass(self):
        with self.assertRaises(RuntimeError):
            preprocess('#error deliberate regression test\n', self.cpp)

    def test_removing_inactive_experimental_moment_samplers_is_allowed(self):
        case = next(c for c in json.loads((BASELINE / 'matrix.json').read_text()) if c['name'] == 'froxel-inject-raster')
        source = sources(case)['fragment']
        removed = source
        for channel in 'RGB':
            removed = removed.replace(f'uniform sampler3D u_VolumetricDirMoment{channel};', '')
        self.assertNotEqual(removed, source)
        self.assertEqual(preprocess(source, self.cpp), preprocess(removed, self.cpp))

    def test_image_rounding_budget_and_exact_default(self):
        from PIL import Image
        from gl43_validation.compare_images import compare
        with tempfile.TemporaryDirectory() as temporary:
            a, b = Path(temporary) / 'a.png', Path(temporary) / 'b.png'
            image = Image.new('RGB', (960, 540), (100, 100, 100)); image.save(a)
            image.putpixel((10, 10), (101, 100, 100)); image.save(b)
            self.assertFalse(compare(a, b)['passed'])
            self.assertTrue(compare(a, b, max_delta=1, max_pixels=5, max_mean=1e-5)['passed'])
            image.putpixel((10, 10), (102, 100, 100)); image.save(b)
            self.assertFalse(compare(a, b, max_delta=1, max_pixels=5, max_mean=1e-5)['passed'])
            for x in range(10, 16): image.putpixel((x, 10), (101, 100, 100))
            image.save(b)
            self.assertFalse(compare(a, b, max_delta=1, max_pixels=5, max_mean=1e-5)['passed'])

    def test_missing_runtime_diagnostics_cannot_pass(self):
        from test_lightrecon_legacy_runtime import validate_consumers
        with self.assertRaises(AssertionError):
            validate_consumers('lightRecon: build requested=3; use=0\n', 'A', 3)

    def test_missing_lifecycle_diagnostics_cannot_pass(self):
        from test_lightrecon_lifecycle_runtime import parse
        with self.assertRaises(AssertionError):
            parse('RECON_LIFECYCLE_DONE\n')


if __name__ == '__main__':
    unittest.main()
