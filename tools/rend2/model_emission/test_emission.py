"""Real owned MD3 bytes, source-image masks and retained static entity poses."""
from pathlib import Path
import struct
import tempfile
import unittest

from emission import extract, write
from bsp_fixtures.catalogue import Assets
from model_instances.instances import build


def model_bytes(reverse=False, reversed_shading=False, bulb_width=4, bulb_height=2,
                duplicate_bulb=False, no_shaders=False, different_duplicate_material=False, corner=False):
    surfaces = [("housing", [(0,0,0),(16,0,0),(16,8,0),(0,8,0)], "models/asr11/housing"),
                ("bulb", [(4,2,1),(4+bulb_width,2,1),(4+bulb_width,2+bulb_height,1),(4,2+bulb_height,1)], "models/asr11/bulb")]
    if duplicate_bulb:
        name, vertices, shader = surfaces[1]
        if corner:
            vertices=[(4,2,1),(4,4,1),(4,4,3),(4,2,3)]
        surfaces.append((name,vertices,"models/asr11/other" if different_duplicate_material else shader))
    frame = struct.pack("<10f16s", 0, 0, 0, max(16, 4+bulb_width), max(8, 2+bulb_height), 1,
                        0, 0, 0, 32, b"owned ASR11")
    body = bytearray(frame)
    for name, vertices, shader in surfaces:
        shaders = b"" if no_shaders else struct.pack("<64si", shader.encode(), 0)
        triangles = struct.pack("<6i", *( (0,2,1,0,3,2) if reverse else (0,1,2,0,2,3)))
        uv = struct.pack("<8f", 0,0,1,0,1,1,0,1)
        xyz = b"".join(struct.pack("<3hH", round(x*64), round(y*64), round(z*64),
                                   0 if reversed_shading else 128) for x,y,z in vertices)
        sh, tri, st, points = 108, 108+len(shaders), 108+len(shaders)+len(triangles), 108+len(shaders)+len(triangles)+len(uv)
        size = points+len(xyz)
        body.extend(struct.pack("<4s64s10i", b"IDP3", name.encode(), 0, 1, 0 if no_shaders else 1,
                                4, 2, tri, sh, st, points, size)+shaders+triangles+uv+xyz)
    return struct.pack("<4si64s9i", b"IDP3", 15, b"owned/asr11.md3", 0, 1, 0, len(surfaces), 0,
                       108, 164, 164, 108+len(body))+body


def bsp(entities):
    text = '{ "classname" "worldspawn" }\n'
    for fields in entities:
        text += '{\n'+''.join('"%s" "%s"\n' % (k,v) for k,v in fields.items())+'}\n'
    data = bytearray(152)
    data[:8] = b"RBSP\1\0\0\0"
    encoded = text.encode()+b"\0"
    for k in range(18):
        struct.pack_into("<ii", data, 8+8*k, 152, len(encoded) if k == 0 else 0)
    return bytes(data)+encoded


def texture(path, kind="slots", alpha=False):
    width, height, bpp = 16, 8, 32 if alpha else 24
    data = bytearray(struct.pack("<BBBHHBHHHHBB", 0,0,2,0,0,0,0,0,width,height,bpp,0x28 if alpha else 0x20))
    for y in range(height):
        for x in range(width):
            lit = kind == "solid" or (kind == "slots" and 4 <= x < 12)
            r,g,b = (255,188,137) if lit else (0,0,0)
            data.extend((b,g,r,128) if alpha else (b,g,r))
    path.write_bytes(data)


def author(base, shader=None, **model_options):
    for folder in ("models/asr11", "shaders", "maps"):
        (base/folder).mkdir(parents=True, exist_ok=True)
    (base/"models/asr11/fixture.md3").write_bytes(model_bytes(**model_options))
    texture(base/"models/asr11/glow.tga")
    texture(base/"models/asr11/white.tga", "solid")
    texture(base/"models/asr11/lens.tga", "solid", True)
    body = shader if shader is not None else '{ map models/asr11/white } { map models/asr11/glow blendFunc add glow }'
    text = 'models/asr11/housing\n{\n { map models/asr11/white }\n}\n'
    text += 'models/asr11/bulb\n{\n'+body+'\n}\n'
    text += 'models/asr11/other\n{\n'+body+'\n}\n'
    (base/"shaders/asr11.shader").write_text(text, encoding="ascii")


def instance(**fields):
    return dict(classname="misc_model_static", model="models/asr11/fixture.md3", **fields)


class EmissionTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.base = Path(self.directory.name)

    def tearDown(self):
        self.directory.cleanup()

    def run_scene(self, entities=None, shader=None, policy=None, **model_options):
        author(self.base, shader, **model_options)
        assets = Assets(self.base)
        try:
            models = build(bsp(entities or [instance()]), assets)
            return extract(models, assets, **(policy or {}))
        finally:
            assets.close()

    def test_masked_bulb_on_bright_non_emissive_housing(self):
        r = self.run_scene()
        f, = r["fixtures"]
        region, = r["regions"]
        self.assertEqual(region["md3_surfaces"], [1])
        self.assertAlmostEqual(region["emitting_area"], 4.)
        for actual, expected in zip(region["centroid"], (6.,3.,1.)):
            self.assertAlmostEqual(actual, expected)
        self.assertEqual(region["outward"], (0.,0.,-1.))
        self.assertAlmostEqual(region["source_triangle_RGB_coverage"], .5)
        self.assertAlmostEqual(r["diagnostics"][0]["resolved_RGB_fraction"], 4/136)
        self.assertEqual(region["colour_prior"][0], 1.)
        self.assertAlmostEqual(region["colour_prior"][1], ((188/255+.055)/1.055)**2.4)
        self.assertAlmostEqual(region["colour_prior"][2], ((137/255+.055)/1.055)**2.4)
        self.assertIn("point", [s["type"] for s in f["suggestions"]])
        self.assertEqual(r["diagnostics"][0]["surfaces"][0]["status"], "no_declared_emission_in_selected_shader")
        self.assertEqual(region["historical_bake_participation"], "unknown")
        layer = r["diagnostics"][0]["surfaces"][1]["layers"][0]
        self.assertEqual(layer["ordinal"], 1)
        self.assertEqual(layer["mask_provenance"]["path"], "models/asr11/glow.tga")
        self.assertEqual(len(layer["mask_provenance"]["sha256"]), 64)

    def test_repeated_instances_and_scaled_rotated_world_area(self):
        r = self.run_scene([instance(origin="10 20 30", angle="90", modelscale_vec="2 3 4"), instance(origin="110 20 30")])
        self.assertEqual(r["counts"]["cached_local_extractions"], 1)
        a,b = sorted(r["regions"], key=lambda r:r["centroid"][0])
        self.assertEqual(a["source_id"], b["source_id"])
        self.assertNotEqual(a["id"], b["id"])
        for actual, expected in zip(a["centroid"], (1.,32.,34.)):
            self.assertAlmostEqual(actual, expected)
        self.assertAlmostEqual(a["emitting_area"], 24.)
        self.assertAlmostEqual(b["emitting_area"], 4.)
        self.assertAlmostEqual(a["source_triangle_RGB_coverage"], .5)

    def test_mirrored_winding_and_reversed_shading_normals(self):
        r = self.run_scene([instance(modelscale_vec="-1 1 1")])
        region, = r["regions"]
        self.assertEqual(region["outward"], (0.,0.,1.))
        self.assertIn("mirrored_winding_changes_visible_side", region["issues"])
        r = self.run_scene(reversed_shading=True)
        region, = r["regions"]
        self.assertEqual(region["outward"], (0.,0.,-1.))
        self.assertIn("shading_normals_oppose_visible_side", region["issues"])
        r = self.run_scene(reverse=True)
        self.assertEqual(r["regions"][0]["outward"], (0.,0.,1.))

    def test_translucent_lens_and_two_sided_direction(self):
        r = self.run_scene(shader='{ map models/asr11/lens blendFunc GL_SRC_ALPHA GL_ONE_MINUS_SRC_ALPHA glow }')
        region, = r["regions"]
        self.assertAlmostEqual(region["emitting_area"], 8.)
        self.assertFalse(region["optical_coverage_known"])
        self.assertIn("source_alpha_coverage_unknown", region["issues"])
        self.assertIn("translucent_or_complex_blend", region["issues"])
        r = self.run_scene(shader='cull none { map models/asr11/glow glow }')
        region, = r["regions"]
        self.assertIsNone(region["outward"])
        self.assertEqual(region["direction_hypothesis"], "both_visible_sides")
        self.assertEqual(len(region["direction_alternatives"]), 2)

    def test_missing_dark_animated_untextured_unknown_never_emit_whole_mesh(self):
        for shader in ('{ map models/asr11/absent glow }',
                       '{ glow }',
                       '{ map models/asr11/glow rgbGen const ( 0 0 0 ) glow }',
                       '{ emissiveMap models/asr11/glow emissiveNits 0 }',
                       '{ animMap 2 models/asr11/white models/asr11/glow glow }',
                       '{ map models/asr11/glow tcMod scroll 1 0 glow }',
                       'deformVertexes autosprite { map models/asr11/glow glow }',
                       'q3map_surfacelight 100 { map models/asr11/white }'):
            with self.subTest(shader=shader):
                r = self.run_scene(shader=shader)
                self.assertEqual(r["regions"], [])
                self.assertEqual(r["fixtures"], [])
                self.assertEqual(r["diagnostics"][0]["status"], "no_resolved_emission_regions")
        r = self.run_scene(no_shaders=True)
        self.assertEqual(r["regions"], [])
        self.assertEqual(r["diagnostics"][0]["surfaces"][1]["status"], "unresolved_material")

    def test_layers_and_duplicate_mesh_do_not_multiply_emitting_area(self):
        r = self.run_scene(shader='{ map models/asr11/glow glow } { map models/asr11/glow glow }', duplicate_bulb=True)
        f, = r["fixtures"]
        region, = r["regions"]
        self.assertAlmostEqual(region["emitting_area"], 4.)
        self.assertAlmostEqual(f["emitting_RGB_area"], 4.)
        self.assertEqual(region["md3_surfaces"], [1,2])
        self.assertIsNone(region["colour_prior"])

    def test_overlapping_materials_union_and_corner_fixture_preserve_regions(self):
        r = self.run_scene(duplicate_bulb=True,different_duplicate_material=True)
        self.assertEqual(len(r["regions"]),2)
        fixture, = r["fixtures"]
        self.assertAlmostEqual(fixture["emitting_RGB_area"],4.)
        self.assertAlmostEqual(fixture["region_area_sum"],8.)
        self.assertIsNone(fixture["colour_prior"])
        r = self.run_scene(shader='{ map models/asr11/white glow }',duplicate_bulb=True,corner=True)
        self.assertEqual(len(r["regions"]),2)
        fixture, = r["fixtures"]
        self.assertAlmostEqual(fixture["emitting_RGB_area"],12.)
        self.assertIsNone(fixture["outward"])
        self.assertNotIn("rectangle",[s["type"] for s in fixture["suggestions"]])
        for actual,expected in zip(fixture["centroid"],(16/3,3.,4/3)):
            self.assertAlmostEqual(actual,expected)

    def test_disconnected_luminous_submeshes_are_separate_fixtures(self):
        # An actual mask gap leaves separate emitting regions even though both
        # are on the same original bulb surface.
        author(self.base)
        texture_path=self.base/'models/asr11/glow.tga'
        raw=bytearray(texture_path.read_bytes())
        for y in range(8):
            for x in range(16):
                value=255 if x<4 or x>=12 else 0
                start=18+(y*16+x)*3
                raw[start:start+3]=bytes([value,value,value])
        texture_path.write_bytes(raw)
        assets=Assets(self.base)
        try:
            r=extract(build(bsp([instance()]),assets),assets)
            self.assertEqual(len(r["fixtures"]),2)
            for region in r["regions"]:
                self.assertAlmostEqual(region["emitting_area"],2.)
        finally:assets.close()

    def test_authored_emissive_map_and_nits_remain_a_prior(self):
        r = self.run_scene(shader='{ map models/asr11/white rgbGen vertex emissiveMap models/asr11/glow emissiveNits 200 }')
        region, = r["regions"]
        self.assertAlmostEqual(region["emitting_area"], 4.)
        self.assertIn(dict(origin="authored_emissive_nits", value=200., unit="luminance_nits"),
                      [p["prior"] for p in region["priors"]])
        self.assertEqual(region["historical_bake_participation"], "unknown")
        self.assertTrue(region["static_eligible"])
        self.assertIsNotNone(region["colour_prior"])

    def test_long_panel_retains_tube_geometry(self):
        r = self.run_scene(shader='{ map models/asr11/white glow }', bulb_width=64, bulb_height=2)
        region, = r["regions"]
        tube, = [s for s in region["suggestions"] if s["type"] == "tube"]
        self.assertAlmostEqual(tube["radius"], 1.)
        self.assertAlmostEqual(sum((a-b)**2 for a,b in zip(tube["start"],tube["end"]))**.5,64.)

    def test_dynamic_or_missing_model_and_bsp_duplicate_stay_unresolved(self):
        author(self.base)
        assets=Assets(self.base)
        try:
            r=build(bsp([dict(classname="misc_model_breakable",model="models/asr11/fixture.md3"),
                         dict(classname="misc_model_static",model="models/asr11/missing.md3")]),assets)
            self.assertEqual(extract(r,assets)["regions"],[])
            r=build(bsp([instance()]),assets)
            r["instances"][0]["additional_geometry_eligible"]=False
            self.assertEqual(extract(r,assets)["regions"],[])
        finally: assets.close()

    def test_stable_export_and_budgets(self):
        r=self.run_scene()
        self.assertEqual(r,self.run_scene())
        with tempfile.TemporaryDirectory() as output:
            write(r,output)
            before={p.name:p.read_bytes() for p in Path(output).iterdir()}
            write(self.run_scene(),output)
            self.assertEqual(before,{p.name:p.read_bytes() for p in Path(output).iterdir()})
            self.assertIn(r["regions"][0]["id"].encode(),before["emitting-footprints.obj"])
        with self.assertRaisesRegex(ValueError,"work limit"):
            self.run_scene(policy=dict(max_work=1))
        with self.assertRaisesRegex(ValueError,"output budget"):
            self.run_scene(policy=dict(max_output_bytes=100))

    def test_mask_content_change_updates_source_identity_without_moving_support(self):
        author(self.base)
        assets=Assets(self.base)
        try:
            models=build(bsp([instance()]),assets)
            first=extract(models,assets)
            path=self.base/'models/asr11/glow.tga'
            raw=bytearray(path.read_bytes())
            for i in range(18,len(raw),3):
                raw[i],raw[i+2]=raw[i+2],raw[i]
            path.write_bytes(raw)
            second=extract(models,assets)
            a,b=first['regions'][0],second['regions'][0]
            self.assertEqual(a['centroid'],b['centroid'])
            self.assertEqual(a['emitting_area'],b['emitting_area'])
            self.assertEqual(a['instance_id'],b['instance_id'])
            self.assertNotEqual(a['source_id'],b['source_id'])
            self.assertNotEqual(a['colour_prior'],b['colour_prior'])
        finally:assets.close()


if __name__ == "__main__":
    unittest.main()
