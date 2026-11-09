"""ASR11 conditional luminous MD3 regions. Offline research, never runtime lights.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import Counter, defaultdict
import copy
import io
import json
import math
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from bsp_fixtures import catalogue as geometry
from model_instances import instances as models

SCHEMA = "org.openjk.research.md3-emission/1"


def identity(*values):
    return geometry.digest(json.dumps([SCHEMA, *values], sort_keys=True, separators=(",", ":"), allow_nan=False).encode())


def stage_tokens(body):
    t, stages, i = geometry.tokens(body), [], 0
    while i < len(t):
        if t[i] != "{":
            i += 1
            continue
        end = t.index("}", i+1)
        stages.append(t[i+1:end])
        i = end+1
    return stages


def stage(t, ordinal):
    """Extend the existing stock parser with audited emissive/optical facts."""
    layer = geometry.parse_stage(t, ordinal)
    low = [v.lower() for v in t]
    explicit, image, modulation, scale, nits = False, None, (1., 1., 1.), 1., None
    priors, issues = [], list(layer["issues"])
    # The legacy parser intentionally leaves extended parameters unresolved.
    issues = [x for x in issues if x != "extended_emission_parameters_unknown"]
    for i, word in enumerate(low):
        if word == "emissivemap":
            image, explicit = low[i+1], True
        elif word == "emissivecolor":
            modulation, explicit = models.numbers(" ".join(t[i+1:i+4]), 3), True
            image = image or "$whiteimage"
        elif word == "emissivescale":
            values = []
            for value in t[i+1:i+4]:
                try:
                    values.append(models.numbers(value, 1)[0])
                except ValueError:
                    break
            if len(values) not in (1, 3):
                issues.append("emissive_scale_unsupported")
            elif len(values) == 1:
                scale = values[0]
            else:
                modulation, scale = tuple(values), 1.
            image, explicit = image or "$whiteimage", True
        elif word in ("emissivenits", "emissiveluminance"):
            nits, explicit = models.numbers(t[i+1], 1)[0], True
            image = image or "$whiteimage"
        elif word in ("emissive", "emissivephysical"):
            issues.append("unsupported_emission_keyword")
        if word == "blendfunc" and i+1 < len(t):
            mode = low[i+1]
            if mode not in ("add", "gl_one") or (mode == "gl_one" and (i+2 == len(t) or low[i+2] != "gl_one")):
                issues.append("translucent_or_complex_blend")
        if word in ("heightmap", "parallaxdepth", "parallaxmap"):
            issues.append("runtime_displaced_coordinates_unknown")
    if explicit:
        layer.update(texture=image, colour=modulation, glow=True, clamp=False)
        # generic/lightall sample emissive RGB with u_EmissiveParams, separately
        # from the base stage's rgbGen. Its bundle also has repeat addressing.
        if "animated_or_dependent_colour" in issues:
            issues.remove("animated_or_dependent_colour")
            issues.append("base_colour_generator_separate_from_emissive")
        priors.append(dict(origin="authored_emissive_scale", value=scale, unit="relative_shader"))
        if nits is not None:
            priors.append(dict(origin="authored_emissive_nits", value=nits, unit="luminance_nits"))
        # Emissive map uses diffuse UVs in generic/lightall; static tcMods from
        # the shared parser therefore apply. Complex/animated coordinates veto.
    else:
        priors.append(dict(origin="glow_display_hint", value=None, unit="unknown"))
    if any(v < 0 for v in layer["colour"]) or scale < 0 or (nits is not None and nits < 0):
        issues.append("negative_emission_parameter_unsupported")
    layer.update(issues=sorted(set(issues)), priors=priors, stage_tokens=t,
        emission_declared=explicit or "glow" in low, relative_scale=scale,
        zero_authored_emission=explicit and (scale == 0 or nits == 0),
        colour_modulation_known="animated_or_dependent_colour" not in issues)
    return layer


class Materials:
    def __init__(self, assets):
        self.assets = assets
        self.selected, self.conflicts = assets.shaders()
        self.files, self.manifest = {}, {}

    def get(self, name):
        if not name or name not in self.selected:
            return None
        if name in self.manifest:
            return self.manifest[name]
        original = self.selected[name]
        source = original["source"]
        if source not in self.files:
            data, selected_source = self.assets.read(source.split(":", 1)[1])
            if data is None or selected_source != source:
                raise ValueError("Selected shader source changed during extraction")
            definitions = {}
            for material, body in geometry.definitions(data.decode("latin1")):
                definitions.setdefault(material, body)
            self.files[source] = (geometry.digest(data), definitions)
        file_hash, definitions = self.files[source]
        body = definitions[name]
        result = copy.deepcopy(original)
        result.update(file_sha256=file_hash, declaration_sha256=original["sha256"],
            layers=[stage(t, ordinal) for ordinal, t in enumerate(stage_tokens(body))],
            conflict=name in self.conflicts)
        result["layers"] = [l for l in result["layers"] if l["emission_declared"]]
        self.manifest[name] = result
        return result


class Masks:
    """Source-linear RGB thresholding, using ASR09's lit-run representation.

    Modulation precedes thresholding. Run-average color is a relative prior,
    not a calibrated irradiance or a claim of per-texel color integration.
    """
    def __init__(self, assets, axis=64, threshold=.1):
        if not 4 <= axis <= 512 or not math.isfinite(threshold) or threshold < 0:
            raise ValueError("Invalid emission mask policy")
        self.assets, self.axis, self.threshold = assets, axis, threshold
        self.images, self.cache, self.manifest, self.cells, self.rectangles = {}, {}, {}, 0, 0

    def image(self, texture):
        if texture in self.images:
            return self.images[texture]
        if len(self.images) >= 4096:
            raise ValueError("Emission source image count limit")
        if texture == "$whiteimage":
            value = (1, 1, [(1., 1., 1.)])
            self.images[texture] = value
            self.manifest[texture] = dict(status="defined_builtin", source="renderer $whiteimage", sha256=None,
                                          alpha_unknown=False, transfer="defined relative white")
            return value
        from PIL import Image, UnidentifiedImageError
        stem = texture.rsplit(".", 1)[0] if texture.lower().endswith((".tga", ".jpg", ".jpeg", ".png", ".dds", ".bmp")) else texture
        data, source, selected = None, None, None
        if not texture.startswith("$"):
            for path in dict.fromkeys([texture]+[stem+ext for ext in (".tga", ".jpg", ".png", ".dds", ".bmp")]):
                data, source = self.assets.read(path)
                if data is not None:
                    selected = path
                    break
        if data is None:
            self.images[texture] = None
            self.manifest[texture] = dict(status="unavailable", source=None, sha256=None)
            return None
        provenance = dict(source=source, path=selected, sha256=geometry.digest(data))
        try:
            with Image.open(io.BytesIO(data)) as image:
                if image.width*image.height > 16*1024*1024:
                    raise ValueError("Source texture pixel limit")
                image.load()
                width, height = image.size
                rgb = list(image.convert("RGB").getdata())
                alpha_unknown = ("A" in image.getbands() and image.getextrema()[-1] != (255, 255)) or "transparency" in image.info
        except (OSError, UnidentifiedImageError) as error:
            self.images[texture] = None
            self.manifest[texture] = dict(status="undecodable", reason=str(error), **provenance)
            return None
        w, h = min(width, self.axis), min(height, self.axis)
        self.cells += w*h
        if self.cells > 1000000:
            raise ValueError("Emission decoded mask cache limit")
        lut = [v/255/12.92 if v/255 <= .04045 else ((v/255+.055)/1.055)**2.4 for v in range(256)]
        pixels = []
        for y in range(h):
            for x in range(w):
                total, count = (0., 0., 0.), 0
                for yy in range(y*height//h, (y+1)*height//h):
                    for xx in range(x*width//w, (x+1)*width//w):
                        total = geometry.add(total, tuple(lut[v] for v in rgb[yy*width+xx]))
                        count += 1
                pixels.append(geometry.mul(total, 1/count))
        result = w, h, pixels
        self.images[texture] = result
        self.manifest[texture] = dict(status="decoded", original_size=(width, height), mask_size=(w, h),
            transfer="assumed_sRGB_to_relative_linear_RGB", alpha_unknown=alpha_unknown, **provenance)
        return result

    def get(self, layer):
        texture = layer["texture"]
        modulation = tuple(layer["colour"]) if layer["colour_modulation_known"] else (1., 1., 1.)
        key = texture, modulation
        if key not in self.cache:
            image = self.image(texture) if texture else None
            if image is None:
                self.cache[key] = None
            else:
                w, h, pixels = image
                values = [tuple(a*b for a, b in zip(rgb, modulation)) for rgb in pixels]
                rects = geometry.mask_rectangles(w, h, values, self.threshold)
                self.rectangles += len(rects)
                if self.rectangles > 250000 or len(self.cache) >= 4096:
                    raise ValueError("Emission mask rectangle/cache limit")
                self.cache[key] = rects
        return self.cache[key]


UNSUPPORTED = {"coordinates_unsupported", "animated_stage", "deformed_geometry",
               "emissive_scale_unsupported", "negative_emission_parameter_unsupported",
               "unsupported_emission_keyword", "runtime_displaced_coordinates_unknown"}


def local_regions(asset, references, materials, masks, budget):
    """Clip each supported emissive stage in original model UV/model space."""
    buckets, diagnostics, pieces_count = defaultdict(list), [], 0
    for reference in references:
        surface = asset["surfaces"][reference["md3_surface"]]
        name = reference["material"]["selected_material"]
        shader = materials.get(name)
        diagnostic = dict(md3_surface=surface["ordinal"], name=surface["name"],
            material=name, material_provenance=reference["material"], triangles=len(surface["triangles"]),
            status="unresolved_material", layers=[], issues=[])
        diagnostics.append(diagnostic)
        if shader is None:
            continue  # Bright implicit/albedo textures do not become emitters.
        if shader["conflict"] or shader["excluded"]:
            diagnostic["issues"].append("selected_shader_conflicting_or_excluded")
            continue
        if not shader["layers"]:
            diagnostic["status"] = "no_declared_emission_in_selected_shader"
            if shader["surface_light"]:
                diagnostic["issues"].append("compiler_surfacelight_prior_without_display_emission")
            continue
        diagnostic["status"] = "emission_declared_unresolved"
        for layer in shader["layers"]:
            issues = sorted(set(shader["issues"]+layer["issues"]))
            layer_id = identity(asset["id"], name, shader["file_sha256"],
                                shader["declaration_sha256"], layer)
            info = dict(id=layer_id, ordinal=layer["ordinal"], texture=layer["texture"],
                        status="unresolved", issues=issues, source_triangles=0, clipped_polygons=0,
                        source_triangle_count=len(surface["triangles"]), unresolved_triangles=len(surface["triangles"]),
                        masked_out_triangles=0,
                        shader_source=shader["source"], declaration_sha256=shader["declaration_sha256"],
                        priors=layer["priors"])
            diagnostic["layers"].append(info)
            if UNSUPPORTED.intersection(issues):
                continue
            rects = masks.get(layer)
            image = masks.manifest.get(layer["texture"], {})
            layer_id = identity(layer_id,image,masks.axis,masks.threshold)
            info.update(id=layer_id, mask_provenance=image)
            if rects is None:
                info["issues"] = sorted(set(issues+["emission_mask_unavailable"]))
                continue
            if image.get("alpha_unknown"):
                issues = sorted(set(issues+["source_alpha_coverage_unknown"]))
            info["issues"], info["mask_provenance"] = issues, image
            if layer["zero_authored_emission"]:
                rects = []
            if not rects:
                info["status"] = "no_supported_luminous_mask_samples"
                info.update(unresolved_triangles=0,masked_out_triangles=len(surface["triangles"]))
                continue
            known_side = "cull_policy_unknown" not in issues
            two_sided = shader["cull"] in ("none", "disable", "twosided")
            for ti, triangle in enumerate(surface["triangles"]):
                budget.use()
                vertices = [dict(p=surface["positions"][k], uv=surface["uv"][k]) for k in triangle]
                winding = geometry.unit(geometry.cross(geometry.sub(vertices[1]["p"], vertices[0]["p"]),
                                                       geometry.sub(vertices[2]["p"], vertices[0]["p"])))
                if winding is None:
                    info["issues"] = sorted(set(info["issues"]+["degenerate_source_triangle"]))
                    continue
                # JA culls GL_FRONT by default: visible normal opposes the
                # counterclockwise cross product. Packed shading normals do not
                # select the emitting side.
                normal = geometry.mul(winding, 1 if shader["cull"] in ("back", "backsided") else -1)
                if two_sided:
                    first = next(v for v in normal if abs(v) > 1e-8)
                    normal = geometry.mul(normal, 1 if first > 0 else -1)
                shading = geometry.unit(tuple(sum(surface["normals"][k][j] for k in triangle) for j in range(3)))
                piece_issues = list(issues)
                if not two_sided and shading and geometry.dot(shading, normal) < -.5:
                    piece_issues.append("shading_normals_oppose_visible_side")
                parts = geometry.footprint(vertices, layer, rects, budget)
                if parts is None:
                    info["issues"] = sorted(set(info["issues"]+["singular_uv_coordinates"]))
                    continue
                info["unresolved_triangles"] -= 1
                if parts:
                    info["source_triangles"] += 1
                else:
                    info["masked_out_triangles"] += 1
                for polygon, rgb in parts:
                    polygon = geometry.orient(polygon, normal)
                    key = (name, tuple(round(v, 6) for v in normal), round(geometry.dot(normal, polygon[0]), 5), two_sided, known_side)
                    buckets[key].append(dict(polygon=polygon, rgb=rgb, surface=surface["ordinal"],
                        source_triangles=[(surface["ordinal"], ti)], layer_id=layer_id, stage=layer["ordinal"],
                        texture=layer["texture"], known=True, orientation_known=known_side,
                        normal=normal, normal_origin="MD3 winding and selected cull policy", two_sided=two_sided,
                        colour_known=layer["colour_modulation_known"], issues=piece_issues, priors=layer["priors"]))
                    info["clipped_polygons"] += 1
                    pieces_count += 1
                    if pieces_count > budget.max_pieces:
                        raise ValueError("Model emission polygon limit")
            if info["clipped_polygons"]:
                info["status"] = "supported_RGB_mask_geometry"
                diagnostic["status"] = "supported_RGB_mask_geometry"
            elif "singular_uv_coordinates" not in info["issues"]:
                info["status"] = "no_supported_luminous_mask_samples"
    regions = []
    for key, pieces in sorted(buckets.items(), key=lambda p: str(p[0])):
        normal = pieces[0]["normal"]
        for component in geometry.components(pieces, normal, budget):
            source_id = identity(asset["id"], key, sorted({p["layer_id"] for p in component}),
                                 sorted({tuple(ref) for p in component for ref in p["source_triangles"]}),
                                 [p["polygon"] for p in component])
            regions.append(dict(source_id=source_id, material=key[0], normal=normal, pieces=component))
    return regions, diagnostics


def world_region(local, instance, map_hash, budget):
    pose = instance["transform"]
    normal = models.normal_transform(local["normal"], pose)
    normal = geometry.mul(normal, -1 if pose["determinant"] < 0 else 1)
    pieces = []
    for piece in local["pieces"]:
        transformed = dict(piece, polygon=geometry.orient([models.transform(p, pose) for p in piece["polygon"]], normal))
        pieces.append(transformed)
    summary = geometry.summarize(pieces, normal, map_hash, local["material"], 0, budget)
    source_id = local["source_id"]
    summary.update(id=identity(instance["id"], source_id), source_id=source_id,
        instance_id=instance["id"], model_asset=instance["model_asset"], model=instance["model"],
        geometric_normal=normal,
        md3_surfaces=summary.pop("surfaces"), direction_hypothesis="winding_and_cull_visible_side",
        instance_issues=instance["issues"], optical_coverage_known=False if any(
            issue in summary["issues"] for issue in ("translucent_or_complex_blend", "source_alpha_coverage_unknown")) else summary["optical_coverage_known"],
        occlusion_participation="unknown")
    summary["priors"] = [dict(layer_id=p["layer_id"], prior=prior)
                          for p in pieces for prior in p["priors"]]
    summary["priors"] = list({json.dumps(v, sort_keys=True): v for v in summary["priors"]}.values())
    summary["static_eligible"] = instance["static_eligible"] and not any(
        x in summary["issues"] for x in ("animated_or_dependent_colour", "animated_stage", "deformed_geometry"))
    summary["colour_prior"] = summary["layers"][0]["colour_prior"] if len(summary["layers"]) == 1 else None
    summary["colour_composition"] = "single_layer_relative_RGB" if len(summary["layers"]) == 1 else "separate_layer_priors_no_assumed_composition"
    if summary["two_sided"]:
        summary.update(outward=None, direction_hypothesis="both_visible_sides", direction_alternatives=[normal, geometry.mul(normal, -1)])
    elif summary["outward"] is None:
        summary["direction_hypothesis"] = "unresolved_cull_policy"
    if pose["determinant"] < 0:
        summary["issues"] = sorted(set(summary["issues"]+["mirrored_winding_changes_visible_side"]))
    return summary


def edge_contact(a, b):
    for p, q in zip(a, a[1:]+a[:1]):
        direction = geometry.unit(geometry.sub(q, p))
        if direction is None:
            continue
        length = geometry.dot(geometry.sub(q, p), direction)
        for r, s in zip(b, b[1:]+b[:1]):
            if any(math.sqrt(geometry.dot(geometry.cross(geometry.sub(v, p), direction),
                                         geometry.cross(geometry.sub(v, p), direction))) > 1e-6 for v in (r, s)):
                continue
            lo, hi = sorted(geometry.dot(geometry.sub(v, p), direction) for v in (r, s))
            if min(length, hi)-max(0., lo) > 1e-6:
                return True
    return False


def fixture_groups(regions, budget):
    """Connected luminous support within one attested instance; housing never joins lamps."""
    parent = list(range(len(regions)))
    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    boxes = [models.bounds([p for poly in r["footprint"] for p in poly]) for r in regions]
    for i, a in enumerate(regions):
        for j in range(i):
            budget.use()
            if root(i) == root(j) or any(boxes[i]["max"][k]+1e-6 < boxes[j]["min"][k] or boxes[j]["max"][k]+1e-6 < boxes[i]["min"][k] for k in range(3)):
                continue
            contact = False
            for pa in a["footprint"]:
                for pb in regions[j]["footprint"]:
                    budget.use()
                    normal = a["geometric_normal"]
                    coplanar = abs(geometry.dot(normal, regions[j]["geometric_normal"])) > .999999 and all(
                        abs(geometry.dot(geometry.sub(p, pa[0]), normal)) <= 1e-6 for p in pb)
                    if (coplanar and geometry.touches(geometry.orient(pa, normal), geometry.orient(pb, normal), normal)) or edge_contact(pa, pb):
                        contact = True
                        break
                if contact:
                    break
            if contact:
                parent[root(i)] = root(j)
    groups = defaultdict(list)
    for i, region in enumerate(regions):
        groups[root(i)].append(region)
    return list(groups.values())


def fixture_measure(regions, budget):
    """Union coplanar regions across materials before fixture area/centroid."""
    planes = []
    for region in regions:
        normal = region["geometric_normal"]
        match = next((p for p in planes if abs(geometry.dot(p[0], normal)) > .999999 and
                      all(abs(geometry.dot(geometry.sub(v, p[1][0][0]), p[0])) <= 1e-6
                          for poly in region["footprint"] for v in poly)), None)
        if match is None:
            match = (normal, [])
            planes.append(match)
        match[1].extend(geometry.orient(poly, match[0]) for poly in region["footprint"])
    total, moment = 0., (0.,0.,0.)
    for normal, polygons in planes:
        unique, seen = [], set()
        for poly in polygons:
            key = tuple(sorted(tuple(round(v,9) for v in p) for p in poly))
            if key in seen:
                continue
            seen.add(key)
            pending = [poly]
            for cutter in unique:
                next_pending = []
                for candidate in pending:
                    budget.use()
                    next_pending.extend(geometry.subtract_polygon(candidate,cutter,normal)
                                        if geometry.interior_overlap(candidate,cutter,normal) else [candidate])
                pending = next_pending
                if not pending:
                    break
            unique.extend(pending)
            if len(unique) > budget.max_pieces:
                raise ValueError("Cross-material fixture union limit")
        for poly in unique:
            area, center = geometry.measure(poly)
            total += area
            moment = geometry.add(moment,geometry.mul(center,area))
    return total, geometry.mul(moment,1/total)


def extract(catalogue, assets, mask_axis=64, threshold=.1, max_work=5000000,
            max_pieces=250000, max_regions=100000, max_output_bytes=256*1024*1024):
    if catalogue.get("schema") != models.SCHEMA or min(max_work, max_pieces, max_regions, max_output_bytes) <= 0:
        raise ValueError("Invalid model emission input/policy")
    budget = geometry.Budget(max_work, max_pieces)
    materials, masks = Materials(assets), Masks(assets, mask_axis, threshold)
    regions, fixtures, diagnostics, cache = [], [], [], {}
    local_pieces, world_pieces = 0, 0
    for instance in catalogue["instances"]:
        if not instance["static_eligible"] or not instance["additional_geometry_eligible"] or "vertex_light_remap_policy_unknown" in instance["issues"]:
            diagnostics.append(dict(instance_id=instance["id"], status="instance_not_eligible_for_additional_static_geometry",
                                    issues=instance["issues"], asset_status=instance["asset_status"], surfaces=[]))
            continue
        asset = catalogue["assets"][instance["model_asset"]]
        key = identity(asset["id"], [(r["md3_surface"], r["material"]) for r in instance["surfaces"]])
        if key not in cache:
            cache[key] = local_regions(asset, instance["surfaces"], materials, masks, budget)
            local_pieces += sum(len(r["pieces"]) for r in cache[key][0])
            if local_pieces > max_pieces:
                raise ValueError("Local model emission cache polygon limit")
        local, statuses = cache[key]
        world = [world_region(r, instance, catalogue["map_sha256"], budget) for r in local]
        world_pieces += sum(len(r["footprint"]) for r in world)
        if world_pieces > max_pieces:
            raise ValueError("World model emission polygon limit")
        if len(regions)+len(world) > max_regions:
            raise ValueError("World emission region limit")
        source_areas = {(s["ordinal"], ti): geometry.measure([models.transform(s["positions"][k], instance["transform"]) for k in t])[0]
                        for s in asset["surfaces"] for ti, t in enumerate(s["triangles"])}
        model_area = sum(source_areas.values())
        for r in world:
            source_area = sum(source_areas[tuple(ref)] for ref in r["source_triangles"])
            r["source_triangle_area"] = source_area
            r["source_triangle_RGB_coverage"] = r["emitting_area"]/source_area if source_area else None
            r["model_surface_area"] = model_area
        diagnostics.append(dict(instance_id=instance["id"], status="supported_emission_regions" if world else "no_resolved_emission_regions",
            surfaces=statuses, source_geometry_area=model_area, resolved_RGB_area=sum(r["emitting_area"] for r in world),
            resolved_RGB_fraction=sum(r["emitting_area"] for r in world)/model_area if model_area else None))
        instance_fixtures = []
        for group in fixture_groups(world, budget):
            area, center = fixture_measure(group, budget)
            region_area_sum = sum(r["emitting_area"] for r in group)
            common = group[0]["outward"]
            if common is None or any(r["outward"] is None or geometry.dot(common, r["outward"]) < .999 for r in group):
                common = None
            suggestions = group[0]["suggestions"] if len(group) == 1 else [dict(type="surface_collection", region_ids=sorted(r["id"] for r in group))]
            box = models.bounds([p for r in group for poly in r["footprint"] for p in poly])
            if len(group) > 1 and max(b-a for a, b in zip(box["min"], box["max"])) <= 4:
                suggestions.append(dict(type="point", position=center))
            ids = sorted(r["id"] for r in group)
            colour = tuple(sum(r["colour_prior"][k]*r["emitting_area"] for r in group)/area for k in range(3)) if all(r["colour_prior"] is not None for r in group) and abs(region_area_sum-area) < 1e-6 else None
            instance_fixtures.append(dict(id=identity(instance["id"], ids),
                source_id=identity(instance["model_asset"],sorted(r["source_id"] for r in group)),
                instance_id=instance["id"], model=instance["model"],
                region_ids=ids, centroid=center, outward=common, colour_prior=colour, emitting_RGB_area=area, suggestions=suggestions,
                region_area_sum=region_area_sum,
                static_eligible=all(r["static_eligible"] for r in group), historical_bake_participation="unknown",
                optical_coverage_known=all(r["optical_coverage_known"] for r in group),
                issues=sorted({x for r in group for x in r["issues"]})))
        fixtures.extend(instance_fixtures)
        diagnostics[-1]["resolved_RGB_area"] = sum(f["emitting_RGB_area"] for f in instance_fixtures)
        diagnostics[-1]["resolved_RGB_fraction"] = diagnostics[-1]["resolved_RGB_area"]/model_area if model_area else None
        regions.extend(world)
    if len({r["id"] for r in regions}) != len(regions) or len({f["id"] for f in fixtures}) != len(fixtures):
        raise ValueError("Model emission identity collision")
    result = dict(schema=SCHEMA, map_sha256=catalogue["map_sha256"], profile=catalogue["profile"],
        policy=dict(mask_axis=mask_axis, threshold=threshold, max_work=max_work, max_pieces=max_pieces,
                    max_regions=max_regions, max_output_bytes=max_output_bytes),
        regions=sorted(regions, key=lambda r: r["id"]), fixtures=sorted(fixtures, key=lambda f: f["id"]),
        instance_manifest={i["id"]:{k:i[k] for k in ("model","model_asset","role","entity_ordinal","entity_origin",
            "frame","transform","source_fields","asset_status","issues","static_eligible","additional_geometry_eligible",
            "bsp_presence","bsp_triangle_refs")} for i in catalogue["instances"]},
        model_manifest={key:dict(sha256=a["sha256"],selected_frame=a["selected_frame"],frames=a["frames"],aliases=a["aliases"],
            vertices=a["vertices"],triangles=a["triangles"],surfaces=[dict(ordinal=s["ordinal"],name=s["name"],shaders=s["shaders"],
                vertices=len(s["positions"]),triangles=len(s["triangles"])) for s in a["surfaces"]]) for key,a in catalogue["assets"].items()},
        diagnostics=diagnostics, shader_manifest=materials.manifest, mask_manifest=masks.manifest,
        counts=dict(instances=len(catalogue["instances"]), eligible_instances=sum(i["additional_geometry_eligible"] for i in catalogue["instances"]),
                    regions=len(regions), fixtures=len(fixtures), cached_local_extractions=len(cache),
                    no_region_instances=sum(d["status"] == "no_resolved_emission_regions" for d in diagnostics),
                    surface_status=dict(sorted(Counter(s["status"] for d in diagnostics for s in d["surfaces"]).items())),
                    layer_status=dict(sorted(Counter(l["status"] for d in diagnostics for s in d["surfaces"] for l in s["layers"]).items()))))
    if len(json.dumps(result, allow_nan=False).encode()) > max_output_bytes//2:
        raise ValueError("Emission catalogue output budget")
    return result


def write(result, output):
    directory = Path(output)
    payloads = {"emission.json": json.dumps(result, sort_keys=True, indent=2, allow_nan=False)+"\n"}
    fields = ("id", "source_id", "instance_id", "model", "material", "md3_surfaces", "centroid", "outward", "emitting_area",
              "source_triangle_RGB_coverage", "optical_coverage_known", "static_eligible", "issues")
    payloads["regions.tsv"] = "\t".join(fields)+"\n"+"".join("\t".join(json.dumps(r[k], separators=(",", ":")) for k in fields)+"\n" for r in result["regions"])
    rows, offset = ["# ASR11 supported conditional luminous RGB geometry; bake and optical participation unknown"], 1
    for region in result["regions"]:
        rows.append("g region_"+region["id"])
        rows.append("# optical_coverage_known=%s issues=%s" % (region["optical_coverage_known"], ",".join(region["issues"])))
        for polygon in region["footprint"]:
            rows.extend("v %.9g %.9g %.9g" % tuple(p) for p in polygon)
            rows.append("f "+" ".join(str(k) for k in range(offset, offset+len(polygon))))
            offset += len(polygon)
    payloads["emitting-footprints.obj"] = "\n".join(rows)+"\n"
    if sum(len(p.encode()) for p in payloads.values()) > result["policy"]["max_output_bytes"]:
        raise ValueError("Formatted emission output budget")
    directory.mkdir(parents=True, exist_ok=True)
    for name, payload in payloads.items():
        (directory/name).write_text(payload, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--map", required=True)
    parser.add_argument("--profile", choices=("sp", "mp"), default="sp")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mask-axis", type=int, default=64)
    parser.add_argument("--threshold", type=float, default=.1)
    args = parser.parse_args()
    assets = models.Assets(args.base)
    try:
        data, source = assets.read(args.map)
        if data is None:
            raise ValueError("Original BSP unavailable")
        instances = models.build(data, assets, args.profile)
        result = extract(instances, assets, args.mask_axis, args.threshold)
        result.update(map_path=args.map, map_source=source)
        write(result, args.output)
        print(json.dumps(result["counts"], sort_keys=True))
    finally:
        assets.close()


if __name__ == "__main__":
    main()
