"""ASR10 bounded offline JA MD3 instance catalogue. No renderer/canonical writes.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import Counter, defaultdict
import json
import math
from pathlib import Path
import re
import struct
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from bsp_fixtures.catalogue import Assets, add, cross, digest, dot, mul, read_bsp, sub, unit

SCHEMA = "org.openjk.research.md3-instances/1"


def identity(*values):
    return digest(json.dumps([SCHEMA, *values], sort_keys=True, separators=(",", ":"), allow_nan=False).encode())


def path_key(value):
    value = value.replace("\\", "/").lower()
    if not value or value.startswith("/") or ":" in value or any(p in ("", ".", "..") for p in value.split("/")):
        raise ValueError("Invalid relative model/material path")
    return value


def material_key(value):
    return re.sub(r"\.(tga|jpg|png|dds|bmp)$", "", path_key(value)) if value else None


def numbers(value, count):
    result = tuple(float(v) for v in value.split())
    if len(result) != count or any(not math.isfinite(v) or abs(v) > 1e8 for v in result):
        raise ValueError("Invalid finite entity vector/scalar")
    return result


def integer(value, maximum=2147483647):
    result, = numbers(value, 1)
    if result != int(result) or abs(result) > maximum:
        raise ValueError("Invalid entity integer")
    return int(result)


def scalar(value, issues):
    # G_SpawnFloat/G_ParseField use atof. Some stock entities contain a scalar
    # followed by extra values; preserve and flag the tail while using the
    # actual loader's leading numeric value.
    match = re.match(r"^\s*([+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?)", value)
    if not match:
        raise ValueError("Invalid finite entity scalar")
    result = numbers(match.group(1), 1)[0]
    if value[match.end():].strip():
        issues.append("scalar_trailing_tokens_ignored_by_loader")
    return result


def entity_records(text, source_map=False):
    """Keep ordered epairs (SP angle precedence), duplicates and source ordinals.

    Source-map brush/patch bodies are skipped, not interpreted as geometry.
    Quoted Windows paths retain backslashes as in COM_Parse; no JSON unescaping.
    """
    if len(text) > 8*1024*1024:
        raise ValueError("Entity text budget")
    pattern = re.compile(r'//[^\n]*|/\*.*?\*/|"[^"\n]*"|[{}]|[^\s{}"]+', re.S)
    t = [m.group() for m in pattern.finditer(text.rstrip("\0"))
         if not m.group().startswith(("//", "/*"))]
    if len(t) > 1000000:
        raise ValueError("Entity token budget")
    records, i = [], 0
    while i < len(t):
        if t[i] != "{":
            raise ValueError("Expected entity opening brace")
        i += 1
        pairs = []
        while i < len(t) and t[i] != "}":
            if t[i] == "{" and source_map:
                depth, i = 1, i+1
                while i < len(t) and depth:
                    depth += (t[i] == "{")-(t[i] == "}")
                    i += 1
                if depth:
                    raise ValueError("Unclosed source brush/patch")
                continue
            if i+1 >= len(t) or not t[i].startswith('"') or not t[i+1].startswith('"'):
                raise ValueError("Expected quoted entity key/value")
            key, value = t[i][1:-1].lower(), t[i+1][1:-1]
            if len(key) > 1024 or len(value) > 4096:
                raise ValueError("Entity field size budget")
            pairs.append((key, value))
            i += 2
        if i == len(t):
            raise ValueError("Unclosed entity")
        records.append(dict(ordinal=len(records), pairs=pairs, fields=dict(pairs)))
        if len(records) > 65536:
            raise ValueError("Entity count budget")
        i += 1
    return records


def bsp_entities(data):
    if len(data) < 152 or data[:8] != b"RBSP\1\0\0\0":
        raise ValueError("Expected JA RBSP v1")
    start, length = struct.unpack_from("<ii", data, 8)
    if start < 152 or length < 0 or start+length > len(data):
        raise ValueError("Invalid original BSP entities")
    return entity_records(data[start:start+length].decode("latin1"))


def axes(angles):
    """AnglesToAxis: pitch/yaw/roll degrees, local X/Y/Z are columns."""
    pitch, yaw, roll = (math.radians(v) for v in angles)
    sp, cp, sy, cy, sr, cr = math.sin(pitch), math.cos(pitch), math.sin(yaw), math.cos(yaw), math.sin(roll), math.cos(roll)
    return ((cp*cy, cp*sy, -sp),
            (sr*sp*cy-cr*sy, sr*sp*sy+cr*cy, sr*cp),
            (cr*sp*cy+sr*sy, cr*sp*sy-sr*cy, cr*cp))


def placement(entity, profile, compiled=False):
    fields, issues = entity["fields"], []
    origin = numbers(fields.get("origin", "0 0 0"), 3)
    angle = (0., 0., 0.)
    if profile == "sp" and not compiled:
        # G_ParseField assigns both keys in source epair order.
        for key, value in entity["pairs"]:
            if key == "angles":
                angle = numbers(value, 3)
            elif key == "angle":
                angle = (0., scalar(value, issues), 0.)
    else:
        angle = numbers(fields["angles"], 3) if "angles" in fields else (0., scalar(fields.get("angle", "0"), issues), 0.)
    vector = numbers(fields.get("modelscale_vec", "1 1 1"), 3)
    uniform = scalar(fields.get("modelscale", "0" if profile == "sp" or compiled else "1"), issues)
    if compiled and "modelscale_vec" in fields:
        scale = vector
    elif (profile == "sp" or compiled) and uniform != 0:
        scale = (uniform,)*3
    elif "modelscale_vec" in fields:
        scale = vector
    elif profile == "mp" and not compiled:
        scale = (uniform,)*3
    else:
        scale = (1.,)*3
    if profile == "sp" and not compiled and 0 in scale:
        scale = tuple(v if v else 1. for v in scale)  # ScaleModelAxis ignores zero axes.
        issues.append("sp_zero_scale_axis_is_identity")
    columns = tuple(mul(axis, s) for axis, s in zip(axes(angle), scale))
    determinant = dot(cross(columns[0], columns[1]), columns[2])
    if abs(determinant) < 1e-12:
        issues.append("singular_transform")
    if determinant < 0:
        issues.append("mirrored_transform")
    return dict(origin=origin, angles=angle, scale=scale, columns=columns,
                determinant=determinant, convention="world = origin + columns * local; scale then pitch/yaw/roll",
                zoffset_culling_only=scalar(fields.get("zoffset", "0"), issues)), issues


def transform(point, pose):
    return add(pose["origin"], tuple(sum(pose["columns"][k][j]*point[k] for k in range(3)) for j in range(3)))


def normal_transform(normal, pose):
    # Inverse transpose preserves the authored visible side under reflected axes.
    # Triangle cross-product normals are separately exported for inspectability.
    if abs(pose["determinant"]) < 1e-12:
        return None
    return unit(tuple(sum(pose["columns"][k][j]*normal[k]/pose["scale"][k]**2 for k in range(3)) for j in range(3)))


def bounds(points):
    return dict(min=tuple(min(p[k] for p in points) for k in range(3)),
                max=tuple(max(p[k] for p in points) for k in range(3))) if points else None


def cstring(data):
    raw = bytes(data).split(b"\0", 1)[0]
    return raw.decode("latin1")


def decode_md3(data, frame=0):
    if len(data) < 108 or len(data) > 64*1024*1024:
        raise ValueError("MD3 byte limit/truncated header")
    h = struct.unpack_from("<4si64s9i", data)
    _, version, _, _, nf, nt, ns, _, of, ot, os, end = h
    if h[0] != b"IDP3" or version != 15 or end != len(data) or not 1 <= nf <= 1024 or not 0 <= nt <= 1024 or not 1 <= ns <= 128:
        raise ValueError("Unsupported or invalid MD3 header")
    if not 0 <= frame < nf:
        raise ValueError("Requested MD3 frame out of range")
    if not 108 <= of <= of+56*nf <= ot <= ot+112*nt*nf <= os <= end:
        raise ValueError("Invalid MD3 frame/tag extents")
    frame_bounds = struct.unpack_from("<10f", data, of+56*frame)
    if any(not math.isfinite(v) or abs(v) > 1e8 for v in frame_bounds) or any(frame_bounds[k] > frame_bounds[k+3] for k in range(3)):
        raise ValueError("Invalid MD3 source bounds")
    surfaces, offset, vertices, triangles = [], os, 0, 0
    for ordinal in range(ns):
        if offset+108 > end:
            raise ValueError("Truncated MD3 surface")
        s = struct.unpack_from("<4s64s10i", data, offset)
        _, name, _, snf, nsh, nv, ntri, tri, sh, st, xyz, size = s
        if s[0] != b"IDP3" or snf != nf or not 0 <= nsh <= 256 or not 0 < nv <= 65536 or not 0 <= ntri <= 131072 or not 108 <= size <= end-offset:
            raise ValueError("Invalid MD3 surface counts")
        regions = sorted([(tri, tri+12*ntri), (sh, sh+68*nsh), (st, st+8*nv), (xyz, xyz+8*nv*nf)])
        previous = 108
        for lo, hi in regions:
            if not previous <= lo <= hi <= size:
                raise ValueError("Overlapping/out-of-bounds MD3 arrays")
            previous = hi
        shaders = [cstring(data[offset+sh+68*k:offset+sh+68*k+64]) for k in range(nsh)]
        uv = [struct.unpack_from("<2f", data, offset+st+8*k) for k in range(nv)]
        if any(not math.isfinite(v) or abs(v) > 1e6 for pair in uv for v in pair):
            raise ValueError("Invalid MD3 UV")
        indices = [struct.unpack_from("<3i", data, offset+tri+12*k) for k in range(ntri)]
        if any(i < 0 or i >= nv for t in indices for i in t):
            raise ValueError("Invalid MD3 index")
        positions, normals = [], []
        for k in range(nv):
            x, y, z, encoded = struct.unpack_from("<3hH", data, offset+xyz+8*(frame*nv+k))
            positions.append((x/64., y/64., z/64.))
            lat, lng = (encoded >> 8)*math.tau/256, (encoded & 255)*math.tau/256
            normals.append((math.cos(lat)*math.sin(lng), math.sin(lat)*math.sin(lng), math.cos(lng)))
        surface_name = cstring(name).lower()
        renderer_name = surface_name[:-2] if len(surface_name) > 2 and surface_name[-2] == "_" else surface_name
        surfaces.append(dict(ordinal=ordinal, name=cstring(name), renderer_name=renderer_name,
            shaders=shaders, positions=positions, normals=normals, uv=uv, triangles=indices))
        vertices += nv
        triangles += ntri
        if vertices > 1000000 or triangles > 1000000:
            raise ValueError("MD3 decoded geometry limit")
        offset += size
    if offset != end:
        raise ValueError("Unaccounted MD3 surface data")
    return dict(frames=nf, selected_frame=frame, tag_count=nt, surfaces=surfaces,
                vertices=vertices, triangles=triangles, local_bounds=bounds([p for s in surfaces for p in s["positions"]]))


def remap_pairs(pairs, prefix):
    remaps = []
    for key, value in pairs:
        if key.startswith(prefix):
            if ";" not in value:
                remaps.append(dict(key=key, raw=value, valid=False))
                continue
            old, new = value.split(";", 1)
            remaps.append(dict(key=key, raw=value, valid=True,
                               old=material_key(old.strip()) if old.strip() != "*" else "*",
                               new=material_key(new.strip())))
    return remaps


def materials(surface, entity, world, compiled):
    issues, applications = [], []
    selected = material_key(surface["shaders"][0]) if surface["shaders"] else None
    inherited = selected
    # Static runtime loaders construct a zeroed refEntity: frame/skinNum 0,
    # no customSkin/customShader. Preserve supplied unsupported hints verbatim.
    if not compiled:
        if any(k in entity["fields"] for k in ("skin", "skinfile", "customskin", "shader", "customshader")) or any(k.startswith("_remap") for k in entity["fields"]):
            issues.append("static_loader_does_not_apply_entity_skin_or_remap")
        remaps = remap_pairs(world["pairs"], "remapshader")
        for remap in remaps:
            if remap["valid"] and remap["old"] == inherited:
                selected = remap["new"]
                applications = [dict(origin="worldspawn_runtime_remap", **remap)]
            elif not remap["valid"]:
                issues.append("invalid_world_shader_remap")
        if any(k.startswith("vertexremapshader") for k in world["fields"]):
            issues.append("vertex_light_remap_policy_unknown")
    else:
        # NetRadiant's model remaps are semicolon pairs. Source-only interpretation
        # is provisional until complete material/geometry correspondence exists.
        matched_targets = set()
        for remap in remap_pairs(entity["pairs"], "_remap"):
            if remap["valid"] and remap["old"] in (inherited, "*"):
                selected = remap["new"]
                matched_targets.add(selected)
                applications.append(dict(origin="compiler_entity_remap_hypothesis", **remap))
            elif not remap["valid"]:
                issues.append("unsupported_compiler_remap")
        if len(matched_targets) > 1:
            selected = None
            issues.append("compiler_remap_precedence_unresolved")
        if any(k in entity["fields"] for k in ("skin", "_skin")):
            selected = None
            issues.append("compiler_skin_selection_unresolved")
    if selected is None:
        issues.append("material_unresolved")
    return dict(inherited_md3_shader=inherited, selected_material=selected, applications=applications,
                resolution_status="conditional source names; shader registration/availability not evaluated",
                selection="first MD3 shader, static skinNum=0" if not compiled else "compiler material hypothesis"), issues


class TriangleIndex:
    def __init__(self, surfaces, world):
        self.lookup = defaultdict(list)
        self.count = 0
        self.comparisons = 0
        remaps = remap_pairs(world["pairs"], "remapshader")
        for s in surfaces:
            if s["model"] != 0 or s["kind"] not in (1, 3):
                continue
            for i in range(0, len(s["indices"]), 3):
                points = [s["vertices"][k]["p"] for k in s["indices"][i:i+3]]
                attributes = [(s["vertices"][k]["uv"],
                               s["vertices"][k]["normal"]) for k in s["indices"][i:i+3]]
                material = material_key(s["material"])
                for remap in remaps:
                    if remap["valid"] and remap["old"] == material_key(s["material"]):
                        material = remap["new"]
                # Compiler material names are the original shader assignment;
                # runtime remaps are a separate lookup view of the same facets.
                for key in {material_key(s["material"]), material}:
                    self.lookup[self.key(key, points)].append((s["ordinal"], i//3, points, attributes))
                self.count += 1
                if self.count > 1000000:
                    raise ValueError("BSP triangle correspondence limit")

    @staticmethod
    def key(material, points):
        # Centroid bins are an index only; adjacent bins cover roundoff at cell
        # boundaries. All three corners/UVs/sides are verified below.
        return (material, tuple(math.floor(sum(p[k] for p in points)/3*64) for k in range(3)))

    def match(self, material, points, attributes):
        found = []
        _, (x, y, z) = self.key(material, points)
        possible = (item for dz in (-1, 0, 1) for dy in (-1, 0, 1) for dx in (-1, 0, 1)
                    for item in self.lookup.get((material, (x+dx, y+dy, z+dz)), []))
        for s, t, other, other_attributes in possible:
            self.comparisons += 1
            if self.comparisons > 5000000:
                raise ValueError("BSP correspondence work limit")
            if all(any(max(abs(a-b) for a, b in zip(p, q)) < .001 and
                           max(abs(a-b) for a, b in zip(uv, quv)) < .0001 and
                           normal is not None and unit(qnormal) is not None and dot(normal, unit(qnormal)) > .99
                       for q, (quv, qnormal) in zip(other, other_attributes))
                   for p, (uv, normal) in zip(points, attributes)):
                found.append((s, t))
        return found


def build(data, assets, profile="sp", source_map=None, max_instances=4096,
          max_world_vertices=2000000, max_output_bytes=256*1024*1024, max_world_triangles=2000000):
    if profile not in ("sp", "mp") or min(max_instances, max_world_vertices, max_output_bytes, max_world_triangles) <= 0:
        raise ValueError("Invalid catalogue policy")
    original = bsp_entities(data)
    surfaces = read_bsp(data)
    world = original[0] if original else dict(fields={}, pairs=[])
    index = TriangleIndex(surfaces, world)
    records = [(e, "bsp", None) for e in original]
    source_hash = None
    if source_map is not None:
        source_hash = digest(source_map)
        records += [(e, "source_map", source_hash) for e in entity_records(source_map.decode("latin1"), True)
                    if e["fields"].get("classname", "").lower() == "misc_model"]
    instances, model_cache, asset_records, world_vertices, total_vertices = [], {}, {}, 0, 0
    world_triangles = 0
    map_hash = digest(data)
    for entity, origin, evidence_hash in records:
        fields = entity["fields"]
        classname, raw_model = fields.get("classname", "").lower(), fields.get("model", "")
        if not raw_model and classname not in ("misc_model", "misc_model_static"):
            continue
        is_brush = raw_model.startswith("*")
        if not is_brush and not raw_model.lower().endswith(".md3") and classname not in ("misc_model", "misc_model_static"):
            continue
        if len(instances) >= max_instances:
            raise ValueError("Model instance count limit")
        compiled = classname == "misc_model"
        static = classname == "misc_model_static" and origin == "bsp"
        role = "brush_model" if is_brush else "compiler_baked_model" if compiled else "external_static_runtime" if static else "dynamic_or_effect_model"
        try:
            pose, issues = placement(entity, profile, compiled)
        except ValueError as error:
            pose, issues = None, ["invalid_transform: "+str(error)]
        if any(k in fields for k in ("parent", "bolt", "bolttag", "attach", "bind")) or (compiled and fields.get("target")):
            issues.append("unresolved_parent_or_attachment")
        if compiled:
            issues.append("compiler_profile_hypothesis")
        if not (static or compiled):
            issues.append("dynamic_or_unsupported_entity_class")
        if len({k for k, _ in entity["pairs"]}) < len(entity["pairs"]):
            issues.append("duplicate_entity_keys")
        model = raw_model if is_brush else path_key(raw_model) if raw_model else None
        frame = integer(fields.get("_frame", fields.get("frame", "0")), 1023) if compiled else 0
        if not compiled and ("_frame" in fields or "frame" in fields):
            issues.append("runtime_static_loader_uses_frame_zero")
        instance_id = identity(map_hash, profile, origin, evidence_hash, entity["ordinal"], model)
        instance = dict(id=instance_id, entity_ordinal=entity["ordinal"], entity_origin=origin,
            source_map_sha256=evidence_hash, classname=classname, model=model, requested_model=raw_model,
            role=role, transform=pose, source_fields=fields, source_pairs=entity["pairs"], frame=frame,
            model_asset=None, asset_status="not_applicable" if is_brush else "unavailable", world_bounds=None,
            surfaces=[], bsp_presence="unknown", bsp_triangle_refs=[], static_eligible=False,
            additional_geometry_eligible=False, historical_bake_participation="unknown",
            occlusion_participation="unknown", issues=issues)
        if is_brush:
            instances.append(instance)
            continue
        if model is None:
            issues.append("model_path_missing")
            instances.append(instance)
            continue
        cache_key = model, frame
        if cache_key not in model_cache:
            if len(model_cache) >= 512:
                raise ValueError("MD3 asset count limit")
            asset_data, asset_source = assets.read(model)
            if asset_data is None:
                model_cache[cache_key] = (None, "unavailable", "model_asset_missing")
            else:
                try:
                    decoded = decode_md3(asset_data, frame)
                except ValueError as error:
                    model_cache[cache_key] = (None, "invalid_or_unsupported", str(error))
                else:
                    asset_id = identity(digest(asset_data), frame)
                    if asset_id not in asset_records:
                        total_vertices += decoded["vertices"]
                        if total_vertices > 2000000:
                            raise ValueError("Decoded model catalogue vertex limit")
                        decoded.update(id=asset_id, sha256=digest(asset_data), aliases={model: asset_source})
                        asset_records[asset_id] = decoded
                    else:
                        asset_records[asset_id]["aliases"][model] = asset_source
                    model_cache[cache_key] = (asset_id, "available", None)
        asset_id, status, error = model_cache[cache_key]
        instance.update(model_asset=asset_id, asset_status=status)
        if error:
            issues.append(error)
        eligible = pose is not None and (static or compiled) and not any(x in issues for x in ("singular_transform", "unresolved_parent_or_attachment"))
        if not asset_id or not eligible:
            instances.append(instance)
            continue
        model_record = asset_records[asset_id]
        if model_record["frames"] > 1:
            issues.append("multi_frame_asset_selected_pose_only")
        world_vertices += model_record["vertices"]
        world_triangles += model_record["triangles"]
        if world_vertices > max_world_vertices:
            raise ValueError("World transformed vertex limit")
        if world_triangles > max_world_triangles:
            raise ValueError("World model triangle query limit")
        all_points, matches, all_matched, ambiguous = [], [], True, False
        for surface in model_record["surfaces"]:
            points = [transform(p, pose) for p in surface["positions"]]
            world_normals = [normal_transform(n, pose) for n in surface["normals"]]
            all_points.extend(points)
            material, material_issues = materials(surface, entity, world, compiled)
            issues.extend(material_issues)
            refs = []
            for triangle in surface["triangles"]:
                found = index.match(material["selected_material"], [points[k] for k in triangle],
                                    [(surface["uv"][k], world_normals[k]) for k in triangle])
                if len(found) != 1:
                    all_matched = False
                    ambiguous |= len(found) > 1
                refs.extend(found)
            matches.extend(refs)
            instance["surfaces"].append(dict(id=identity(instance_id, asset_id, surface["ordinal"]),
                model_asset=asset_id, md3_surface=surface["ordinal"], name=surface["name"],
                renderer_name=surface["renderer_name"], material=material,
                vertices=len(points), triangles=len(surface["triangles"]), world_bounds=bounds(points),
                bsp_triangle_refs=sorted(set(refs))))
        instance["world_bounds"] = bounds(all_points)
        instance["bsp_triangle_refs"] = sorted(set(matches))
        if matches and all_matched:
            instance["bsp_presence"] = "complete_geometry_correspondence"
        elif matches:
            instance["bsp_presence"] = "partial_or_ambiguous_geometry_correspondence"
        else:
            instance["bsp_presence"] = "no_exact_geometry_correspondence_found"
        if ambiguous:
            issues.append("ambiguous_bsp_geometry_correspondence")
        instance["static_eligible"] = static and not any(x in issues for x in ("duplicate_entity_keys",))
        # Compiler models are already represented by BSP evidence; never import
        # a second copy, even when compiler topology/remaps cannot be matched.
        instance["additional_geometry_eligible"] = instance["static_eligible"] and not matches
        if static and matches:
            issues.append("runtime_geometry_overlaps_existing_bsp_support")
        instances.append(instance)
    reference_users = defaultdict(list)
    for instance in instances:
        instance["issues"] = sorted(set(instance["issues"]))
        for ref in instance["bsp_triangle_refs"]:
            reference_users[tuple(ref)].append(instance)
    for users in reference_users.values():
        if len(users) > 1:
            for instance in users:
                instance["issues"] = sorted(set(instance["issues"]+["shared_bsp_correspondence_not_unique_ownership"]))
    instances.sort(key=lambda i: i["id"])
    if len({i["id"] for i in instances}) != len(instances):
        raise ValueError("Instance ID collision")
    result = dict(schema=SCHEMA, map_sha256=map_hash, profile=profile, source_map_sha256=source_hash,
        assets=asset_records, instances=instances,
        policy=dict(max_instances=max_instances, max_world_vertices=max_world_vertices,
                    max_world_triangles=max_world_triangles, max_output_bytes=max_output_bytes),
        counts=dict(entities=len(original), instances=len(instances), assets=len(asset_records),
            static_eligible=sum(i["static_eligible"] for i in instances), roles=dict(sorted(Counter(i["role"] for i in instances).items())),
            asset_status=dict(sorted(Counter(i["asset_status"] for i in instances).items()))),
        limitations=["Stripped compiler model instances cannot be enumerated from BSP entities alone.",
                    "Source-map/compiler correspondence is conditional, not historical instance identity.",
                    "External static models are not automatically original bake geometry or occluders.",
                    "Scripted spawns, attachments, Ghoul2 and procedural/RMG transforms are not recovered."])
    if len(json.dumps(result, allow_nan=False).encode()) > max_output_bytes//2:
        raise ValueError("Catalogue output size limit")
    return result


def write(result, output, export_obj=False):
    directory = Path(output)
    payload = json.dumps(result, indent=2, sort_keys=True, allow_nan=False)+"\n"
    if len(payload.encode()) > result["policy"]["max_output_bytes"]:
        raise ValueError("Catalogue formatted output size limit")
    payloads = {"instances.json": payload}
    fields = ("id", "entity_ordinal", "role", "model", "asset_status", "static_eligible", "additional_geometry_eligible", "bsp_presence", "world_bounds", "issues")
    rows = ["\t".join(fields)]+["\t".join(json.dumps(i[k], separators=(",", ":")) for k in fields) for i in result["instances"]]
    payloads["instances.tsv"] = "\n".join(rows)+"\n"
    if export_obj:
        rows, offset = ["# ASR10 conditional external static poses; not certified bake geometry/occluders"], 1
        for instance in result["instances"]:
            if not instance["additional_geometry_eligible"]:
                continue
            asset = result["assets"][instance["model_asset"]]
            for surface in asset["surfaces"]:
                rows.append("g instance_%s_surface_%d" % (instance["id"], surface["ordinal"]))
                rows.extend("v %.9g %.9g %.9g" % transform(p, instance["transform"]) for p in surface["positions"])
                rows.extend("f "+" ".join(str(offset+k) for k in triangle) for triangle in surface["triangles"])
                offset += len(surface["positions"])
        payload = "\n".join(rows)+"\n"
        if len(payload.encode()) > result["policy"]["max_output_bytes"]:
            raise ValueError("OBJ output size limit")
        payloads["instances.obj"] = payload
    # Finish all budget checks before publishing any file.
    if sum(len(p.encode("utf-8")) for p in payloads.values()) > result["policy"]["max_output_bytes"]:
        raise ValueError("Combined catalogue output size limit")
    directory.mkdir(parents=True, exist_ok=True)
    for name, payload in payloads.items():
        (directory/name).write_text(payload, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--map", required=True)
    parser.add_argument("--profile", choices=("sp", "mp"), default="sp")
    parser.add_argument("--source-map", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--export-obj", action="store_true")
    args = parser.parse_args()
    assets = Assets(args.base)
    try:
        data, source = assets.read(args.map)
        if data is None:
            raise ValueError("Original BSP unavailable")
        if args.source_map and args.source_map.stat().st_size > 8*1024*1024:
            raise ValueError("Source map text size limit")
        result = build(data, assets, args.profile, args.source_map.read_bytes() if args.source_map else None)
        result.update(map_path=args.map, map_source=source,
            asset_selection="explicit loose base then reverse lexical PK3; paths case folded; no mod/runtime search-path certification")
        write(result, args.output, args.export_obj)
        print(json.dumps(result["counts"], sort_keys=True))
    finally:
        assets.close()


if __name__ == "__main__":
    main()
