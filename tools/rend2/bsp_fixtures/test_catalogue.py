"""Geometry adversaries use real RBSP records; compiler tests are in compiled.py."""
import json
from pathlib import Path
import struct
import tempfile
import unittest

from catalogue import (catalogue, definitions, mask_rectangles, parse_shader,
                       read_bsp, write_catalogue)


def quad(x=0., y=0., z=64., width=16., height=8., material="fixture", model=0,
         uv=(0., 0., 1., 1.), kind=1, reverse=True):
    u, v, U, V = uv
    return dict(material=material, model=model, kind=kind,
                vertices=[(x, y, z, u, v), (x+width, y, z, U, v),
                          (x+width, y+height, z, U, V), (x, y+height, z, u, V)],
                indices=[0, 2, 1, 0, 3, 2] if reverse else [0, 1, 2, 0, 2, 3],
                normal=(0., 0., 1.))


def rbsp(surfaces):
    """Owned serialized RBSP facets. This does not claim compiler provenance."""
    names = sorted({s["material"] for s in surfaces})
    lumps = [bytearray() for _ in range(18)]
    for name in names:
        lumps[1].extend(struct.pack("<64sii", name.encode(), 0, 0))
    for model in range(max(s["model"] for s in surfaces)+1):
        refs = [i for i, s in enumerate(surfaces) if s["model"] == model]
        first = min(refs) if refs else 0
        count = len(refs)
        assert refs == list(range(first, first+count))
        lumps[7].extend(struct.pack("<6f4i", -1024, -1024, -1024, 1024, 1024, 1024,
                                    first, count, 0, 0))
    for s in surfaces:
        fv, fi = len(lumps[10])//80, len(lumps[11])//4
        for xyzuv in s["vertices"]:
            v = bytearray(80)
            struct.pack_into("<5f", v, 0, *xyzuv)
            struct.pack_into("<3f", v, 52, *s["normal"])
            lumps[10].extend(v)
        lumps[11].extend(struct.pack("<%di" % len(s["indices"]), *s["indices"]))
        record = bytearray(148)
        struct.pack_into("<7i", record, 0, names.index(s["material"]), -1, s["kind"],
                         fv, len(s["vertices"]), fi, len(s["indices"]))
        struct.pack_into("<3f", record, 128, *s["normal"])
        lumps[13].extend(record)
    data = bytearray(152)
    data[:8] = b"RBSP\1\0\0\0"
    for k, lump in enumerate(lumps):
        struct.pack_into("<ii", data, 8+k*8, len(data), len(lump))
        data.extend(lump)
    return bytes(data)


class Masks:
    def __init__(self, values=None):
        self.values = values or {"solid": [(0., 0., 1., 1., (1., .5, .25))]}
        self.manifest = {}

    def get(self, texture):
        self.manifest[texture] = dict(status="owned_relative_linear")
        return self.values.get(texture)


def extract(surfaces, body="{ map solid glow }", masks=None, options=None):
    shader = parse_shader(body)
    return catalogue(rbsp(surfaces), {"fixture": shader}, masks or Masks(), options)


class CatalogueTests(unittest.TestCase):
    def test_side_centroid_extent_and_compiler_glow_aliases(self):
        r = extract([quad()], "q3map_surfacelight 100 q3map_lightRGB .1 1 .1 { map solid glow }")
        f, = r["fixtures"]
        self.assertEqual(f["outward"], (0., 0., 1.))
        self.assertEqual(f["centroid"], (8., 4., 64.))
        self.assertEqual(f["emitting_area"], 128.)
        self.assertEqual(len(f["priors"]), 2)
        self.assertEqual(f["layers"][0]["colour_prior"], (1., .5, .25))
        rect, = [s for s in f["suggestions"] if s["type"] == "rectangle"]
        self.assertEqual((rect["width"], rect["height"]), (16., 8.))
        self.assertEqual(rect["center"], f["centroid"])
        back = extract([quad()], "cull back { map solid glow }")["fixtures"][0]
        self.assertEqual(back["outward"], (0., 0., -1.))

    def test_bulb_strip_and_panel(self):
        for width, height, shape in ((2, 2, "point"), (64, 2, "tube"), (16, 8, "rectangle")):
            with self.subTest(shape=shape):
                f, = extract([quad(width=width, height=height)])["fixtures"]
                self.assertIn(shape, [s["type"] for s in f["suggestions"]])
                self.assertEqual(f["emitting_area"], width*height)

    def test_two_mask_regions_one_bsp_surface(self):
        pixels = [(1., .5, .25) if x < 8 or x >= 24 else (0., 0., 0.)
                  for _ in range(8) for x in range(32)]
        masks = Masks({"slots": mask_rectangles(32, 8, pixels, .1)})
        r = extract([quad(width=32)], "{ map slots glow }", masks)
        self.assertEqual(len(r["fixtures"]), 2)
        fs = sorted(r["fixtures"], key=lambda f: f["centroid"])
        for f, expected in zip(fs, [(4., 4., 64.), (28., 4., 64.)]):
            for actual, wanted in zip(f["centroid"], expected):
                self.assertAlmostEqual(actual, wanted)
        self.assertEqual([f["emitting_area"] for f in fs], [64., 64.])
        self.assertNotEqual(fs[0]["id"], fs[1]["id"])

    def test_overlapping_uvs_and_nearby_disconnected_geometry(self):
        a, b = quad(width=4, height=4), quad(x=4.01, width=4, height=4)
        # Both polygons deliberately use identical UVs in ONE original surface.
        a["vertices"].extend(b["vertices"])
        a["indices"].extend(i+4 for i in b["indices"])
        r = extract([a])
        self.assertEqual(len(r["fixtures"]), 2)
        self.assertAlmostEqual(sum(f["emitting_area"] for f in r["fixtures"]), 32.)
        for f in r["fixtures"]:
            rect, = [s for s in f["suggestions"] if s["type"] == "rectangle"]
            self.assertAlmostEqual(rect["width"], 4., places=5)

    def test_layers_overlap_union_and_exact_duplicate_triangles(self):
        masks = Masks({"solid": [(0., 0., 1., 1., (1., 0., 0.))],
                       "half": [(0., 0., .5, 1., (0., 0., 1.))]})
        r = extract([quad(), quad()], "{ map solid glow } { map half glow }", masks)
        f, = r["fixtures"]
        self.assertEqual(r["counts"]["duplicate_triangles"], 2)
        self.assertEqual(f["surfaces"], [0, 1])
        self.assertEqual(f["emitting_area"], 128.)
        self.assertEqual(len(f["layers"]), 2)
        self.assertEqual(sorted(l["area"] for l in f["layers"]), [64., 128.])
        self.assertEqual({tuple(l["colour_prior"]) for l in f["layers"]}, {(1., 0., 0.), (0., 0., 1.)})

    def test_partial_overlap_union_and_disjoint_layers(self):
        f, = extract([quad(), quad(x=8)])["fixtures"]
        self.assertAlmostEqual(f["emitting_area"], 192.)
        self.assertEqual(f["centroid"], (12., 4., 64.))
        masks = Masks({"left": [(0., 0., .25, 1., (1., 0., 0.))],
                       "right": [(.75, 0., 1., 1., (0., 0., 1.))]})
        fs = extract([quad(width=32)], "{ map left glow } { map right glow }", masks)["fixtures"]
        self.assertEqual(len(fs), 2)
        for f in fs:
            rect, = [s for s in f["suggestions"] if s["type"] == "rectangle"]
            self.assertAlmostEqual(rect["width"], 8.)
            self.assertEqual(len(f["layers"]), 1)

    def test_dark_triangle_beside_lit_triangle_is_not_an_extra_lamp(self):
        surface = quad(width=4, height=4)
        other = quad(x=8, width=4, height=4, uv=(.5, 0., 1., 1.))
        surface["vertices"] = [(x, y, z, u*.5, v) for x, y, z, u, v in surface["vertices"]]
        surface["vertices"].extend(other["vertices"])
        surface["indices"].extend(i+4 for i in other["indices"])
        f, = extract([surface], "{ map half glow }", Masks({"half": [(0., 0., .5, 1., (1., 1., 1.))]}))["fixtures"]
        self.assertTrue(f["emitting_footprint_known"])
        self.assertAlmostEqual(f["emitting_area"], 16.)
        self.assertEqual(f["centroid"], (2., 2., 64.))

    def test_irregular_footprint_has_no_fabricated_rectangle(self):
        a, b = quad(width=2, height=1), quad(y=1, width=1, height=1)
        f, = extract([a, b])["fixtures"]
        self.assertAlmostEqual(f["emitting_area"], 3.)
        self.assertAlmostEqual(f["centroid"][0], 5/6)
        self.assertAlmostEqual(f["centroid"][1], 5/6)
        self.assertEqual([s["type"] for s in f["suggestions"]], ["surface"])

    def test_static_scale_transform_and_order(self):
        masks = Masks({"half": [(0., 0., .5, 1., (1., 1., 1.))]})
        r = extract([quad(width=8, height=8)], "{ map half glow tcMod scale 2 1 }", masks)
        self.assertEqual(len(r["fixtures"]), 2)
        for actual, expected in zip(sorted(f["centroid"][0] for f in r["fixtures"]), [1., 5.]):
            self.assertAlmostEqual(actual, expected)
        f, = extract([quad(width=8, height=8)],
                     "{ map half glow tcMod transform 0 1 -1 0 1 0 }", masks)["fixtures"]
        for actual, expected in zip(f["centroid"], (4., 6., 64.)):
            self.assertAlmostEqual(actual, expected)
        self.assertEqual(f["emitting_area"], 32.)
        f, = extract([quad(width=8, height=8)],
                     "{ clampmap half glow tcMod scale 2 1 tcMod transform 1 0 0 1 -1 0 }", masks)["fixtures"]
        self.assertEqual(f["emitting_area"], 48.)
        self.assertEqual(f["centroid"], (3., 4., 64.))

    def test_clamp_repeat_and_negative_uv(self):
        masks = Masks({"half": [(0., 0., .5, 1., (1., 1., 1.))]})
        surface = quad(width=24, uv=(-1., 0., 2., 1.))
        fs = extract([surface], "{ map half glow }", masks)["fixtures"]
        self.assertEqual(len(fs), 3)
        self.assertAlmostEqual(sum(f["emitting_area"] for f in fs), 96.)
        f, = extract([surface], "{ clampmap half glow }", masks)["fixtures"]
        self.assertAlmostEqual(f["emitting_area"], 96.)
        self.assertAlmostEqual(f["centroid"][0], 6.)

    def test_unknown_geometry_dark_missing_and_moving(self):
        r = extract([quad(kind=2)])
        self.assertEqual(r["fixtures"], [])
        self.assertIsNone(r["unsupported"][0]["centroid"])
        self.assertIsNone(r["unsupported"][0]["footprint"])
        for body, masks, issue in (("{ map absent glow }", Masks(), "mask_unavailable"),
                ("{ map dark glow }", Masks({"dark": []}), "no_luminous_mask_samples"),
                ("{ map solid glow tcMod scroll 1 0 }", Masks(), "coordinates_unsupported")):
            f, = extract([quad()], body, masks)["fixtures"]
            self.assertFalse(f["emitting_footprint_known"])
            self.assertIsNone(f["emitting_area"])
            self.assertIn(issue, f["issues"])
            self.assertIsNone(f["layers"][0]["colour_prior"])
            self.assertEqual([s["type"] for s in f["suggestions"]], ["surface"])
        f, = extract([quad(model=1)])["fixtures"]
        self.assertFalse(f["static_eligible"])
        self.assertEqual(f["historical_bake_participation"], "unknown")
        f, = extract([quad()], "deformVertexes autosprite { map solid glow }")["fixtures"]
        self.assertFalse(f["static_eligible"])

    def test_singular_uv_unknown_side_and_corner_contact(self):
        f, = extract([quad(uv=(0., 0., 0., 0.))])["fixtures"]
        self.assertFalse(f["emitting_footprint_known"])
        surface = quad()
        surface["normal"] = (0., 0., 0.)
        f, = extract([surface])["fixtures"]
        self.assertIsNone(f["outward"])
        r = extract([quad(width=1, height=1), quad(x=1, y=1, width=1, height=1)])
        self.assertEqual(len(r["fixtures"]), 2)

    def test_deterministic_ids_and_inspection_exports(self):
        r = extract([quad(), quad(x=64)])
        again = extract([quad(), quad(x=64)])
        self.assertEqual(r, again)
        with tempfile.TemporaryDirectory() as td:
            write_catalogue(r, td)
            first = {p.name: p.read_bytes() for p in Path(td).iterdir()}
            write_catalogue(again, td)
            self.assertEqual(first, {p.name: p.read_bytes() for p in Path(td).iterdir()})
            for f in r["fixtures"]:
                self.assertIn(f["id"].encode(), first["fixtures.tsv"])
                self.assertIn(f["id"].encode(), first["footprints.obj"])
            self.assertEqual(json.loads(first["catalogue.json"])["schema"], r["schema"])

    def test_parser_and_limits_fail_without_partial_output(self):
        defs = list(definitions('/* comment */ FIXTURE { // note\n { map "solid" glow } }'))
        self.assertEqual(defs[0][0], "fixture")
        with self.assertRaisesRegex(ValueError, "work limit"):
            extract([quad()], options={"max_work": 0})
        bad = bytearray(rbsp([quad()]))
        struct.pack_into("<i", bad, 8+13*8, len(bad)+1)
        with self.assertRaisesRegex(ValueError, "lump extent"):
            read_bsp(bad)
        with self.assertRaises(ValueError):
            parse_shader("{ map solid glow tcMod scale nan 1 }")


if __name__ == "__main__":
    unittest.main()
