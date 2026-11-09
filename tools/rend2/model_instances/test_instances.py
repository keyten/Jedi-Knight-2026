"""Owned MD3/RBSP geometry, loader semantics and negative placement checks."""
from pathlib import Path
import struct
import tempfile
import unittest
import zipfile

from instances import (Assets, bounds, build, decode_md3, entity_records,
                       normal_transform, placement, transform, write)


def md3(frames=1, surfaces=2):
    positions = [(0, 0, 0), (2, 0, 0), (0, 4, 0)]
    body = bytearray()
    for f in range(frames):
        body.extend(struct.pack("<10f16s", f, 0, 0, 2+f, 4, 1, 0, 0, 0, 5, b"owned"))
    for s in range(surfaces):
        shaders = struct.pack("<64si", ("textures/asr10/"+("housing" if s else "emitter")).encode(), 0)
        triangles = struct.pack("<3i", 0, 1, 2)
        uv = struct.pack("<6f", 0, 0, 1, 0, 0, 1)
        # JA front culling displays the clockwise side of this triangle (-Z).
        xyz = b"".join(struct.pack("<3hH", (x+f)*64, y*64, s*64, 128)
                       for f in range(frames) for x, y, z in positions)
        size = 108+len(shaders)+len(triangles)+len(uv)+len(xyz)
        body.extend(struct.pack("<4s64s10i", b"IDP3", ("part%d_1" % s).encode(),
            0, frames, 1, 3, 1, 176, 108, 188, 212, size)+shaders+triangles+uv+xyz)
    end = 108+len(body)
    return struct.pack("<4si64s9i", b"IDP3", 15, b"owned/asr10.md3", 0,
                       frames, 0, surfaces, 0, 108, 108+56*frames, 108+56*frames, end)+body


def entity(fields):
    return '{\n'+''.join('"%s" "%s"\n' % (k, v) for k, v in fields)+"}\n"


def rbsp(text):
    data = bytearray(152)
    data[:8] = b"RBSP\1\0\0\0"
    encoded = text.encode("latin1")+b"\0"
    for k in range(18):
        struct.pack_into("<ii", data, 8+8*k, 152, len(encoded) if k == 0 else 0)
    return bytes(data)+encoded


def record(**fields):
    return entity_records(entity(list(fields.items())))[0]


def bsp_geometry(data, duplicate=False, wrong_uv=False, wrong_side=False):
    data = bytearray(data)
    surfaces = decode_md3(md3())["surfaces"]
    if duplicate:
        surfaces = surfaces+surfaces
    shaders = b"".join(struct.pack("<64sii", s["shaders"][0].encode(), 0, 0) for s in surfaces)
    vertices, indices, records = bytearray(), bytearray(), bytearray()
    for si, s in enumerate(surfaces):
        for p, uv, normal in zip(s["positions"], s["uv"], s["normals"]):
            v = bytearray(80)
            struct.pack_into("<5f", v, 0, *p, *(tuple(x+.25 for x in uv) if wrong_uv else uv))
            struct.pack_into("<3f", v, 52, *(tuple(-x for x in normal) if wrong_side else normal))
            vertices.extend(v)
        indices.extend(struct.pack("<3i", 0, 1, 2))
        record_bytes = bytearray(148)
        struct.pack_into("<7i", record_bytes, 0, si, -1, 3, si*3, 3, si*3, 3)
        records.extend(record_bytes)
    models = struct.pack("<6f4i", 0, 0, 0, 2, 4, 1, 0, len(surfaces), 0, 0)
    for k, block in ((1, shaders), (7, models), (10, vertices), (11, indices), (13, records)):
        struct.pack_into("<ii", data, 8+8*k, len(data), len(block))
        data.extend(block)
    return bytes(data)


class MemoryAssets:
    def __init__(self, files=None):
        self.files = files if files is not None else {"models/fixture.md3": md3()}

    def read(self, name):
        return self.files.get(name.lower()), "owned:"+name.lower()


def scene(*objects):
    return rbsp(entity([("classname", "worldspawn")])+''.join(entity(list(o.items())) for o in objects))


class InstanceTests(unittest.TestCase):
    def test_md3_frame_surface_indices_uv_and_quantization(self):
        a = decode_md3(md3(2), 1)
        self.assertEqual(a["frames"], 2)
        self.assertEqual(a["local_bounds"], dict(min=(1., 0., 0.), max=(3., 4., 1.)))
        self.assertEqual(a["surfaces"][0]["triangles"], [(0, 1, 2)])
        self.assertEqual(a["surfaces"][0]["uv"], [(0., 0.), (1., 0.), (0., 1.)])
        self.assertEqual(a["surfaces"][0]["renderer_name"], "part0")
        for x, y in zip(a["surfaces"][0]["normals"][0], (0., 0., -1.)):
            self.assertAlmostEqual(x, y)

    def test_translation_yaw_pitch_roll_and_vector_scale(self):
        pose, _ = placement(record(origin="10 20 30", angle="90", modelscale_vec="2 3 4"), "sp")
        p = transform((2, 4, 1), pose)
        for x, y in zip(p, (-2, 24, 34)):
            self.assertAlmostEqual(x, y)
        for angles, local, expected in (("90 0 0", (1, 0, 0), (0, 0, -1)),
                                         ("0 0 90", (0, 1, 0), (0, 0, 1)),
                                         ("0 90 0", (1, 0, 0), (0, 1, 0))):
            pose, _ = placement(record(angles=angles), "sp")
            for x, y in zip(transform(local, pose), expected):
                self.assertAlmostEqual(x, y)

    def test_sp_mp_scale_precedence_zero_scale_and_angle_order(self):
        e = record(modelscale="2", modelscale_vec="3 4 5")
        self.assertEqual(placement(e, "sp")[0]["scale"], (2., 2., 2.))
        self.assertEqual(placement(e, "mp")[0]["scale"], (3., 4., 5.))
        self.assertEqual(placement(e, "sp", compiled=True)[0]["scale"], (3., 4., 5.))
        e = record(modelscale="0", modelscale_vec="0 2 0")
        self.assertEqual(placement(e, "sp")[0]["scale"], (1., 2., 1.))
        self.assertIn("singular_transform", placement(e, "mp")[1])
        e = entity_records('{ "angles" "10 20 30" "angle" "90" }')[0]
        self.assertEqual(placement(e, "sp")[0]["angles"], (0., 90., 0.))
        self.assertEqual(placement(e, "mp")[0]["angles"], (10., 20., 30.))
        pose, issues = placement(record(modelscale="1.75 1.75 1.75"), "sp")
        self.assertEqual(pose["scale"], (1.75,)*3)
        self.assertIn("scalar_trailing_tokens_ignored_by_loader", issues)

    def test_reflection_inverse_transpose_and_actual_vertex_bounds(self):
        pose, issues = placement(record(modelscale_vec="-2 3 4", angle="90"), "sp")
        self.assertIn("mirrored_transform", issues)
        self.assertEqual(normal_transform((0, 0, 1), pose), (0., 0., 1.))
        transformed = [transform(p, pose) for p in ((0, 0, 0), (2, 0, 0), (0, 4, 1))]
        b = bounds(transformed)
        self.assertAlmostEqual(b["min"][0], -12.)
        self.assertAlmostEqual(b["min"][1], -4.)
        self.assertEqual(b["max"][2], 4.)

    def test_multiple_instances_shared_asset_world_bounds_zoffset(self):
        data = scene(dict(classname="misc_model_static", model="MODELS\\Fixture.MD3", origin="10 20 30", zoffset="1000"),
                     dict(classname="misc_model_static", model="models/fixture.md3", origin="100 200 300"))
        result = build(data, MemoryAssets())
        self.assertEqual(len(result["assets"]), 1)
        a, b = sorted(result["instances"], key=lambda i: i["entity_ordinal"])
        self.assertNotEqual(a["id"], b["id"])
        self.assertNotEqual(a["surfaces"][0]["id"], b["surfaces"][0]["id"])
        self.assertEqual(a["world_bounds"], dict(min=(10., 20., 30.), max=(12., 24., 31.)))
        self.assertEqual(b["world_bounds"], dict(min=(100., 200., 300.), max=(102., 204., 301.)))
        self.assertTrue(a["additional_geometry_eligible"])
        self.assertEqual(a["historical_bake_participation"], "unknown")
        self.assertEqual(a["occlusion_participation"], "unknown")

    def test_missing_invalid_dynamic_brush_and_attachment_never_invent_geometry(self):
        data = scene(dict(classname="misc_model_static", model="models/missing.md3"),
                     dict(classname="misc_model_static", model="models/bad.md3"),
                     dict(classname="misc_model_breakable", model="models/fixture.md3", spawnflags="2"),
                     dict(classname="func_door", model="*1"),
                     dict(classname="misc_model_static", model="models/fixture.md3", parent="moving"),
                     dict(classname="misc_model_static"),
                     dict(classname="misc_model_static", model="models/fixture.md3", origin="nan 0 0"))
        result = build(data, MemoryAssets({"models/fixture.md3": md3(2), "models/bad.md3": b"IDP3"}))
        for i in result["instances"]:
            self.assertIsNone(i["world_bounds"])
            self.assertEqual(i["surfaces"], [])
            self.assertFalse(i["static_eligible"])
            self.assertFalse(i["additional_geometry_eligible"])
            self.assertEqual(i["occlusion_participation"], "unknown")

    def test_runtime_frame_zero_instead_of_entity_animation_hint(self):
        data = scene(dict(classname="misc_model_static", model="models/fixture.md3", _frame="1"))
        r = build(data, MemoryAssets({"models/fixture.md3": md3(2)}))
        i, = r["instances"]
        self.assertEqual(i["frame"], 0)
        self.assertEqual(i["world_bounds"]["min"], (0., 0., 0.))
        self.assertIn("multi_frame_asset_selected_pose_only", i["issues"])
        self.assertIn("runtime_static_loader_uses_frame_zero", i["issues"])
        self.assertEqual(i["historical_bake_participation"], "unknown")

    def test_inherited_shader_world_remap_and_ignored_runtime_skin(self):
        world = entity([("classname", "worldspawn"), ("remapshader0", "textures/asr10/emitter;textures/asr10/remapped")])
        obj = entity([("classname", "misc_model_static"), ("model", "models/fixture.md3"),
                      ("skin", "custom.skin"), ("_remap", "textures/asr10/emitter;textures/asr10/ignored")])
        result = build(rbsp(world+obj), MemoryAssets())
        i, = result["instances"]
        material = i["surfaces"][0]["material"]
        self.assertEqual(material["inherited_md3_shader"], "textures/asr10/emitter")
        self.assertEqual(material["selected_material"], "textures/asr10/remapped")
        self.assertIn("static_loader_does_not_apply_entity_skin_or_remap", i["issues"])
        self.assertEqual(i["source_fields"]["skin"], "custom.skin")

    def test_source_map_stripped_models_no_duplicate_runtime_geometry(self):
        source = ('{ "classname" "worldspawn" { { ignored patch } } }\n'+
            entity([("classname", "misc_model"), ("model", "models/fixture.md3"), ("_frame", "1"),
                    ("_remap", "textures/asr10/emitter;textures/asr10/compiled")])+
            entity([("classname", "misc_model_static"), ("model", "models/fixture.md3")])).encode()
        data = scene(dict(classname="misc_model_static", model="models/fixture.md3"))
        r = build(data, MemoryAssets({"models/fixture.md3": md3(2)}), source_map=source)
        self.assertEqual(len(r["instances"]), 2)
        compiled, = [i for i in r["instances"] if i["role"] == "compiler_baked_model"]
        self.assertEqual(compiled["frame"], 1)
        self.assertFalse(compiled["additional_geometry_eligible"])
        self.assertEqual(compiled["surfaces"][0]["material"]["selected_material"], "textures/asr10/compiled")

    def test_path_case_pk3_selection_and_missing_model(self):
        with tempfile.TemporaryDirectory() as td:
            with zipfile.ZipFile(Path(td)/"assets0.pk3", "w") as z:
                z.writestr("Models/Fixture.MD3", md3())
            assets = Assets(td)
            try:
                r = build(scene(dict(classname="misc_model_static", model="MODELS/FIXTURE.MD3")), assets)
                self.assertEqual(r["instances"][0]["asset_status"], "available")
                self.assertEqual(next(iter(r["assets"].values()))["aliases"]["models/fixture.md3"], "assets0.pk3:Models/Fixture.MD3")
            finally:
                assets.close()

    def test_asset_content_aliases_and_instance_lineage(self):
        data = scene(dict(classname="misc_model_static", model="models/a.md3"),
                     dict(classname="misc_model_static", model="models/b.md3"))
        r = build(data, MemoryAssets({"models/a.md3": md3(), "models/b.md3": md3()}))
        self.assertEqual(len(r["assets"]), 1)
        asset, = r["assets"].values()
        self.assertEqual(set(asset["aliases"]), {"models/a.md3", "models/b.md3"})
        self.assertNotEqual(r["instances"][0]["id"], r["instances"][1]["id"])
        changed = build(data, MemoryAssets({"models/a.md3": md3(2), "models/b.md3": md3()}))
        a = next(i for i in r["instances"] if i["model"] == "models/a.md3")
        b = next(i for i in changed["instances"] if i["model"] == "models/a.md3")
        self.assertEqual(a["id"], b["id"])
        self.assertNotEqual(a["model_asset"], b["model_asset"])
        self.assertNotEqual(a["surfaces"][0]["id"], b["surfaces"][0]["id"])

    def test_existing_bsp_geometry_suppression_requires_uv_and_side(self):
        data = scene(dict(classname="misc_model_static", model="models/fixture.md3"))
        i, = build(bsp_geometry(data), MemoryAssets())["instances"]
        self.assertEqual(i["bsp_presence"], "complete_geometry_correspondence")
        self.assertEqual(len(i["bsp_triangle_refs"]), 2)
        self.assertFalse(i["additional_geometry_eligible"])
        for option in (dict(wrong_uv=True), dict(wrong_side=True)):
            i, = build(bsp_geometry(data, **option), MemoryAssets())["instances"]
            self.assertEqual(i["bsp_triangle_refs"], [])
            self.assertEqual(i["bsp_presence"], "no_exact_geometry_correspondence_found")
        i, = build(bsp_geometry(data, duplicate=True), MemoryAssets())["instances"]
        self.assertIn("ambiguous_bsp_geometry_correspondence", i["issues"])
        self.assertFalse(i["additional_geometry_eligible"])

    def test_inherited_runtime_remap_applies_to_bsp_comparison_view(self):
        data = rbsp(entity([("classname", "worldspawn"), ("remapshader0", "textures/asr10/emitter;textures/asr10/remapped")])+
                    entity([("classname", "misc_model_static"), ("model", "models/fixture.md3")]))
        i, = build(bsp_geometry(data), MemoryAssets())["instances"]
        self.assertEqual(i["bsp_presence"], "complete_geometry_correspondence")
        self.assertFalse(i["additional_geometry_eligible"])

    def test_bounded_invalid_data_and_deterministic_exports(self):
        data = scene(dict(classname="misc_model_static", model="models/fixture.md3"))
        r = build(data, MemoryAssets())
        self.assertEqual(r, build(data, MemoryAssets()))
        with tempfile.TemporaryDirectory() as td:
            write(r, td, True)
            first = {p.name: p.read_bytes() for p in Path(td).iterdir()}
            write(build(data, MemoryAssets()), td, True)
            self.assertEqual(first, {p.name: p.read_bytes() for p in Path(td).iterdir()})
            self.assertIn(r["instances"][0]["id"].encode(), first["instances.obj"])
        with self.assertRaisesRegex(ValueError, "vertex limit"):
            build(data, MemoryAssets(), max_world_vertices=1)
        with self.assertRaisesRegex(ValueError, "triangle query limit"):
            build(data, MemoryAssets(), max_world_triangles=1)
        with self.assertRaisesRegex(ValueError, "output size limit"):
            build(data, MemoryAssets(), max_output_bytes=100)
        damaged = bytearray(md3())
        struct.pack_into("<i", damaged, 164+176, 10000)
        with self.assertRaisesRegex(ValueError, "index"):
            decode_md3(damaged)
        with self.assertRaises(ValueError):
            build(scene(dict(classname="misc_model_static", model="../outside.md3")), MemoryAssets())
        with self.assertRaises(ValueError):
            placement(record(origin="nan 0 0"), "sp")


if __name__ == "__main__":
    unittest.main()
