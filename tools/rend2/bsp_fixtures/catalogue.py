"""ASR09 offline BSP fixture hypotheses. No canonical asset or runtime writes.

Pillow is used only to decode source textures. All topology, clipping and area
calculations use the standard library and original RBSP positions/UVs.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import defaultdict
import hashlib
import io
import json
import math
from pathlib import Path
import re
import struct
import zipfile

SCHEMA = "org.openjk.research.bsp-fixtures/1"
EPS = 1e-7


def digest(value):
    return hashlib.sha256(value).hexdigest()


def identity(*values):
    return digest(json.dumps([SCHEMA, *values], sort_keys=True, separators=(",", ":"),
                             allow_nan=False).encode())


def add(a, b):
    return tuple(x+y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x-y for x, y in zip(a, b))


def mul(a, s):
    return tuple(x*s for x in a)


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def unit(a):
    length = math.sqrt(dot(a, a))
    return mul(a, 1/length) if length > 1e-12 else None


class Budget:
    def __init__(self, work=5000000, pieces=250000):
        self.remaining = work
        self.max_pieces = pieces

    def use(self, n=1):
        self.remaining -= n
        if self.remaining < 0:
            raise ValueError("Fixture work limit exceeded; no partial catalogue published")


class Assets:
    """Explicit base-directory view: loose files, then reverse lexical PK3 order.

    This does not claim to reproduce an unspecified engine/mod search path.
    Every selected shader/image has a content hash in the research manifest.
    """
    def __init__(self, base):
        self.base = Path(base)
        self.archives = [zipfile.ZipFile(p) for p in sorted(self.base.glob("*.pk3"),
                        key=lambda p: p.name.lower(), reverse=True)]
        self.indices = [{n.lower(): n for n in z.namelist()} for z in self.archives]

    def close(self):
        for archive in self.archives:
            archive.close()

    def read(self, name):
        name = name.replace("\\", "/").lower()
        if name.startswith("/") or ".." in name.split("/") or ":" in name:
            raise ValueError("Unsafe asset path")
        path = self.base / name
        if path.is_file():
            if path.stat().st_size > 128*1024*1024:
                raise ValueError("Source asset size limit")
            return path.read_bytes(), "loose:"+name
        for archive, index in zip(self.archives, self.indices):
            if name in index:
                if archive.getinfo(index[name]).file_size > 128*1024*1024:
                    raise ValueError("Source asset size limit")
                return archive.read(index[name]), Path(archive.filename).name+":"+index[name]
        return None, None

    def shaders(self):
        names = {n for index in self.indices for n in index
                 if n.startswith("shaders/") and n.endswith(".shader")}
        names.update(p.relative_to(self.base).as_posix().lower()
                     for p in (self.base/"shaders").glob("*.shader"))
        table, conflicts = {}, []
        total_bytes = 0
        for name in sorted(names):
            data, source = self.read(name)
            total_bytes += len(data)
            if total_bytes > 64*1024*1024:
                raise ValueError("Shader text budget exceeded")
            for material, body in definitions(data.decode("latin1")):
                record = parse_shader(body)
                record.update(source=source, sha256=digest(body.encode("latin1")))
                if material in table:
                    conflicts.append(material)
                    # Deterministic explicit selection, flagged on affected fixtures.
                    continue
                table[material] = record
                if len(table) > 100000:
                    raise ValueError("Shader definition limit")
        return table, sorted(set(conflicts))


TOKEN = re.compile(r'//[^\n]*|/\*.*?\*/|"([^"\\]*(?:\\.[^"\\]*)*)"|[{}()]|[^\s{}()]+', re.S)


def tokens(text):
    return [m.group(1) if m.group(1) is not None else m.group()
            for m in TOKEN.finditer(text) if not m.group().startswith(("//", "/*"))]


def definitions(text):
    t = tokens(text)
    i = 0
    while i+1 < len(t):
        name = t[i].lower()
        if t[i+1] != "{":
            i += 1
            continue
        start, depth, j = i+2, 1, i+2
        while j < len(t) and depth:
            depth += (t[j] == "{") - (t[j] == "}")
            j += 1
        if depth:
            raise ValueError("Unbalanced shader definition: "+name)
        yield name, " ".join(t[start:j-1])
        i = j


def affine(uv, transforms):
    s, t = uv
    for kind, values in transforms:
        if kind == "scale":
            s, t = s*values[0], t*values[1]
        elif kind == "transform":
            a, b, c, d, e, f = values
            s, t = a*s+c*t+e, b*s+d*t+f
    return s, t


def parse_stage(t, ordinal):
    low = [v.lower() for v in t]
    layer = dict(ordinal=ordinal, texture="", clamp=False, transforms=[],
                 issues=[], colour=(1., 1., 1.), authored_luminance=None)
    layer["glow"] = "glow" in low or "emissive" in low or "emissivephysical" in low
    for i, word in enumerate(low):
        if word in ("map", "clampmap") and i+1 < len(t):
            layer["texture"], layer["clamp"] = low[i+1], word == "clampmap"
        if word in ("animmap", "oneshotanimmap", "videomap"):
            layer["issues"].append("animated_stage")
        if word in ("alphafunc", "alphagen"):
            layer["issues"].append("alpha_composition_unknown")
        if word == "rgbgen" and i+1 < len(t):
            mode = low[i+1]
            if mode == "const":
                nums = [v for v in t[i+2:i+7] if v not in ("(", ")")]
                layer["colour"] = tuple(float(v) for v in nums[:3])
            elif mode not in ("identity", "identitylighting"):
                layer["issues"].append("animated_or_dependent_colour")
        if word in ("emissive", "emissivephysical"):
            # Extended Rend2 stage forms are not guessed by this stock parser.
            layer["issues"].append("extended_emission_parameters_unknown")
        if word == "tcgen" and (i+1 == len(t) or low[i+1] not in ("texture", "base")):
            layer["issues"].append("coordinates_unsupported")
        if word == "tcmod" and i+1 < len(t):
            kind = low[i+1]
            count = {"scale": 2, "transform": 6}.get(kind)
            if count:
                values = tuple(float(v) for v in t[i+2:i+2+count])
                if len(values) != count or not all(math.isfinite(v) for v in values):
                    raise ValueError("Invalid static tcMod")
                layer["transforms"].append((kind, values))
            else:
                layer["issues"].extend(("coordinates_unsupported", "animated_stage"))
    if len(layer["colour"]) != 3 or any(not math.isfinite(x) or x < 0 for x in layer["colour"]):
        raise ValueError("Invalid shader colour")
    return layer


def parse_shader(body):
    t = tokens(body)
    top, stages, i = [], [], 0
    while i < len(t):
        if t[i] == "{":
            j = i+1
            while j < len(t) and t[j] != "}":
                j += 1
            if j == len(t) or "{" in t[i+1:j]:
                raise ValueError("Invalid shader stage")
            stages.append(parse_stage(t[i+1:j], len(stages)))
            i = j+1
        else:
            top.append(t[i].lower())
            i += 1
    def arg(name, default=None):
        return top[top.index(name)+1] if name in top and top.index(name)+1 < len(top) else default
    value = float(arg("q3map_surfacelight", 0))
    if not math.isfinite(value) or value < 0:
        raise ValueError("Invalid q3map_surfacelight")
    layers = [s for s in stages if s["glow"]]
    if not layers and value:
        # q3map uses a source image/editor/base hint, not proof of a glow stage.
        source = arg("q3map_lightimage") or arg("qer_editorimage")
        base = next((s for s in stages if s["texture"] and not s["texture"].startswith("$")), None)
        layer = dict(base) if base else parse_stage([], 0)
        if source:
            layer.update(texture=source, transforms=[], clamp=False)
        layers = [layer]
    cull = arg("cull", "front")
    issues = []
    if "deformvertexes" in top:
        issues.append("deformed_geometry")
    if "trans" in top:
        issues.append("translucent_composition_unknown")
    source_colour = None
    if "q3map_lightrgb" in top:
        start = top.index("q3map_lightrgb")+1
        source_colour = [float(v) for v in top[start:start+3]]
        if len(source_colour) != 3 or any(not math.isfinite(v) or v < 0 for v in source_colour):
            raise ValueError("Invalid q3map_lightRGB")
    if cull not in ("front", "frontsided", "back", "backsided", "none", "disable", "twosided"):
        issues.append("cull_policy_unknown")
    return dict(layers=layers, surface_light=value, cull=cull, issues=issues,
                excluded="sky" in top or "nodraw" in top or "skyparms" in top,
                source_colour=source_colour)


def read_bsp(data):
    if len(data) < 152 or data[:8] != b"RBSP\x01\0\0\0":
        raise ValueError("Expected original JA RBSP v1")
    lumps = []
    for k in range(18):
        offset, length = struct.unpack_from("<ii", data, 8+8*k)
        if offset < 0 or length < 0 or offset+length > len(data):
            raise ValueError("Invalid BSP lump extent")
        lumps.append(memoryview(data)[offset:offset+length])
    for k, size in ((1, 72), (7, 40), (10, 80), (11, 4), (13, 148)):
        if len(lumps[k]) % size:
            raise ValueError("Incomplete BSP record")
    if len(data) > 128*1024*1024 or len(lumps[10])//80 > 1024*1024 or len(lumps[11])//4 > 4*1024*1024 or len(lumps[13])//148 > 131072:
        raise ValueError("Original BSP geometry budget exceeded")
    names = [bytes(lumps[1][p:p+64]).split(b"\0", 1)[0].decode("latin1").lower()
             for p in range(0, len(lumps[1]), 72)]
    model_for = {}
    for m, p in enumerate(range(0, len(lumps[7]), 40)):
        first, count = struct.unpack_from("<ii", lumps[7], p+24)
        if first < 0 or count < 0 or first+count > len(lumps[13])//148:
            raise ValueError("Invalid model surface range")
        for s in range(first, first+count):
            if s in model_for:
                raise ValueError("Overlapping model surface ownership")
            model_for[s] = m
    surfaces = []
    for s, p in enumerate(range(0, len(lumps[13]), 148)):
        shader, _, kind, fv, nv, fi, ni = struct.unpack_from("<7i", lumps[13], p)
        if not 0 <= shader < len(names) or min(fv, nv, fi, ni) < 0 or fv+nv > len(lumps[10])//80 or fi+ni > len(lumps[11])//4:
            raise ValueError("Invalid BSP surface references")
        vertices = []
        for v in range(fv, fv+nv):
            xyzuv = struct.unpack_from("<5f", lumps[10], v*80)
            normal = struct.unpack_from("<3f", lumps[10], v*80+52)
            if any(not math.isfinite(x) or abs(x) > 1e8 for x in xyzuv+normal):
                raise ValueError("Nonfinite/unbounded original geometry")
            vertices.append(dict(p=xyzuv[:3], uv=xyzuv[3:], normal=normal))
        indices = struct.unpack_from("<%di" % ni, lumps[11], fi*4)
        if any(i < 0 or i >= nv for i in indices) or (kind in (1, 3) and ni % 3):
            raise ValueError("Invalid BSP triangle indices")
        surfaces.append(dict(ordinal=s, material=names[shader], model=model_for.get(s), kind=kind,
                             vertices=vertices, indices=indices,
                             plane_normal=struct.unpack_from("<3f", lumps[13], p+128)))
    return surfaces


def clip(poly, distance):
    out = []
    for a, b in zip(poly, poly[1:]+poly[:1]):
        da, db = distance(a), distance(b)
        if da >= 0:
            out.append(a)
        if (da < 0) != (db < 0):
            out.append(add(a, mul(sub(b, a), da/(da-db))))
    # Clipping along triangle/tile boundaries can repeat vertices. Remove them
    # before winding tests and convex subtraction (including closing repeats).
    clean = []
    for p in out:
        if not clean or max(abs(x-y) for x, y in zip(p, clean[-1])) > 1e-10:
            clean.append(p)
    if len(clean) > 1 and max(abs(x-y) for x, y in zip(clean[0], clean[-1])) <= 1e-10:
        clean.pop()
    return clean


def clipped(poly, lo, hi):
    for axis in range(2):
        poly = clip(poly, lambda p: p[axis]-lo[axis])
        poly = clip(poly, lambda p: hi[axis]-p[axis])
        if len(poly) < 3:
            return []
    return [p[2:] for p in poly]


def measure(poly):
    area, moment = 0., (0., 0., 0.)
    for i in range(1, len(poly)-1):
        a, b, c = poly[0], poly[i], poly[i+1]
        weight = math.sqrt(dot(cross(sub(b, a), sub(c, a)), cross(sub(b, a), sub(c, a))))/2
        area += weight
        moment = add(moment, mul(add(add(a, b), c), weight/3))
    return area, mul(moment, 1/area) if area > 1e-15 else (0., 0., 0.)


def mask_rectangles(width, height, pixels, threshold):
    """Coalesce lit runs, preserving gaps; RGB is a relative run-average prior."""
    rects, active = [], {}
    for y in range(height):
        runs, x = [], 0
        while x < width:
            if dot(pixels[y*width+x], (.2126, .7152, .0722)) <= threshold:
                x += 1
                continue
            start, colour = x, (0., 0., 0.)
            while x < width and dot(pixels[y*width+x], (.2126, .7152, .0722)) > threshold:
                colour = add(colour, pixels[y*width+x])
                x += 1
            runs.append((start, x, colour))
        next_active = {}
        for x0, x1, colour in runs:
            key = x0, x1
            r = active.pop(key, [x0, y, x1, y, (0., 0., 0.), 0])
            r[3], r[4], r[5] = y+1, add(r[4], colour), r[5]+x1-x0
            next_active[key] = r
        rects.extend(active.values())
        active = next_active
    rects.extend(active.values())
    return [(x0/width, y0/height, x1/width, y1/height, mul(rgb, 1/n))
            for x0, y0, x1, y1, rgb, n in rects]


class Masks:
    def __init__(self, assets, axis, threshold):
        self.assets, self.axis, self.threshold = assets, axis, threshold
        self.cache, self.manifest = {}, {}
        self.cached_rectangles = 0

    def get(self, texture):
        if texture in self.cache:
            return self.cache[texture]
        if len(self.cache) >= 4096:
            raise ValueError("Emission texture count limit")
        from PIL import Image
        data, source, selected = None, None, None
        # Same preferred extensions as Rend2's source loader for extensionless names.
        stem = re.sub(r"\.(tga|jpg|jpeg|png|dds|bmp)$", "", texture, flags=re.I)
        for name in dict.fromkeys([texture]+[stem+ext for ext in (".tga", ".jpg", ".png", ".dds", ".bmp")]):
            data, source = self.assets.read(name)
            if data is not None:
                selected = name
                break
        if data is None:
            self.cache[texture] = None
            self.manifest[texture] = dict(status="unavailable")
            return None
        with Image.open(io.BytesIO(data)) as image:
            if image.width*image.height > 16*1024*1024:
                raise ValueError("Source texture pixel limit")
            image.load()
            size = image.size
            # Decode transfer before box filtering; never interpret encoded RGB8 as irradiance.
            rgb = list(image.convert("RGB").getdata())
            alpha_unknown = ("A" in image.getbands() and image.getextrema()[-1] != (255, 255)) or "transparency" in image.info
        def linear(v):
            x = v/255.
            return x/12.92 if x <= .04045 else ((x+.055)/1.055)**2.4
        lut = [linear(v) for v in range(256)]
        w, h = min(size[0], self.axis), min(size[1], self.axis)
        pixels = []
        for y in range(h):
            for x in range(w):
                total, count = (0., 0., 0.), 0
                for yy in range(y*size[1]//h, (y+1)*size[1]//h):
                    for xx in range(x*size[0]//w, (x+1)*size[0]//w):
                        total = add(total, tuple(lut[v] for v in rgb[yy*size[0]+xx]))
                        count += 1
                pixels.append(mul(total, 1/count))
        result = mask_rectangles(w, h, pixels, self.threshold)
        self.cached_rectangles += len(result)
        if self.cached_rectangles > 250000:
            raise ValueError("Emission mask cache rectangle limit")
        self.manifest[texture] = dict(status="decoded", source=source, path=selected,
            sha256=digest(data), original_size=size, mask_size=(w, h),
            transfer="assumed_sRGB_to_relative_linear_RGB", alpha_unknown=alpha_unknown)
        self.cache[texture] = result
        return result


def footprint(triangle, layer, rects, budget):
    uvp = [(*affine(v["uv"], layer["transforms"]), *v["p"]) for v in triangle]
    # A singular UV mapping cannot resolve an emitting footprint.
    determinant = ((uvp[1][0]-uvp[0][0])*(uvp[2][1]-uvp[0][1])-
                   (uvp[1][1]-uvp[0][1])*(uvp[2][0]-uvp[0][0]))
    if abs(determinant) < 1e-15:
        return None
    if len(rects) == 1 and rects[0][:4] == (0., 0., 1., 1.):
        return [([v["p"] for v in triangle], rects[0][4])]
    low = [min(v[k] for v in uvp) for k in range(2)]
    high = [max(v[k] for v in uvp) for k in range(2)]
    out = []
    for x0, y0, x1, y1, rgb in rects:
        if layer["clamp"]:
            lo = (min(low[0], 0) if x0 == 0 else x0, min(low[1], 0) if y0 == 0 else y0)
            hi = (max(high[0], 1) if x1 == 1 else x1, max(high[1], 1) if y1 == 1 else y1)
            budget.use()
            poly = clipped(uvp, lo, hi)
            if len(poly) >= 3 and measure(poly)[0] > 1e-12:
                out.append((poly, rgb))
        else:
            nx = math.ceil(high[0]-x0)-math.floor(low[0]-x1)
            ny = math.ceil(high[1]-y0)-math.floor(low[1]-y1)
            budget.use(max(0, nx)*max(0, ny))
            for y in range(math.floor(low[1]-y1), math.ceil(high[1]-y0)):
                for x in range(math.floor(low[0]-x1), math.ceil(high[0]-x0)):
                    poly = clipped(uvp, (x+x0, y+y0), (x+x1, y+y1))
                    if len(poly) >= 3 and measure(poly)[0] > 1e-12:
                        out.append((poly, rgb))
    return out


def projection(normal):
    axis = min(range(3), key=lambda k: abs(normal[k]))
    seed = tuple(float(k == axis) for k in range(3))
    right = unit(cross(seed, normal))
    return right, cross(normal, right)


def orient(poly, normal):
    area_vector = (0., 0., 0.)
    for i in range(1, len(poly)-1):
        area_vector = add(area_vector, cross(sub(poly[i], poly[0]), sub(poly[i+1], poly[0])))
    if dot(area_vector, normal) < 0:
        return list(reversed(poly))
    return poly


def subtract_polygon(poly, cutter, normal):
    """Convex difference: disjoint convex pieces of poly outside cutter."""
    remaining, outside = poly, []
    for a, b in zip(cutter, cutter[1:]+cutter[:1]):
        side = cross(normal, sub(b, a))
        rejected = clip(remaining, lambda p: -dot(sub(p, a), side))
        if len(rejected) >= 3 and measure(rejected)[0] > 1e-10:
            outside.append(rejected)
        remaining = clip(remaining, lambda p: dot(sub(p, a), side))
        if len(remaining) < 3:
            break
    return outside


def interior_overlap(a, b, normal):
    # Separating axes also reject coincident boundaries. Adjacent tessellation
    # cells have no overlapping area and need no expensive polygon difference.
    for polygon in (a, b):
        for p, q in zip(polygon, polygon[1:]+polygon[:1]):
            axis = unit(cross(normal, sub(q, p)))
            if axis is None:
                continue
            aa, bb = [dot(v, axis) for v in a], [dot(v, axis) for v in b]
            if min(max(aa), max(bb))-max(min(aa), min(bb)) <= 1e-9:
                return False
    return True


def touches(a, b, normal):
    # Convex SAT: overlap or a shared edge; a lone touching corner stays separate.
    overlap = a
    for p, q in zip(b, b[1:]+b[:1]):
        side = cross(normal, sub(q, p))
        overlap = clip(overlap, lambda v: dot(sub(v, p), side)+1e-10)
        if len(overlap) < 3:
            break
    if len(overlap) >= 3 and measure(overlap)[0] > 1e-10:
        return True
    for p, q in zip(a, a[1:]+a[:1]):
        edge = sub(q, p)
        length = math.sqrt(dot(edge, edge))
        if length < EPS:
            continue
        direction = mul(edge, 1/length)
        for r, s in zip(b, b[1:]+b[:1]):
            if math.sqrt(dot(cross(sub(r, p), direction), cross(sub(r, p), direction))) > EPS:
                continue
            if math.sqrt(dot(cross(sub(s, p), direction), cross(sub(s, p), direction))) > EPS:
                continue
            lo, hi = sorted((dot(sub(r, p), direction), dot(sub(s, p), direction)))
            if min(length, hi)-max(0., lo) > EPS:
                return True
    return False


def components(pieces, normal, budget):
    parent = list(range(len(pieces)))
    def root(i):
        while parent[i] != i:
            parent[i] = parent[parent[i]]
            i = parent[i]
        return i
    right, up = projection(normal)
    bins = defaultdict(list)
    for i, piece in enumerate(pieces):
        xy = [(dot(p, right), dot(p, up)) for p in piece["polygon"]]
        lo = [math.floor((min(p[k] for p in xy)-EPS)/64) for k in range(2)]
        hi = [math.floor((max(p[k] for p in xy)+EPS)/64) for k in range(2)]
        budget.use((hi[0]-lo[0]+1)*(hi[1]-lo[1]+1))
        keys = [(x, y) for y in range(lo[1], hi[1]+1) for x in range(lo[0], hi[0]+1)]
        possible = {j for key in keys for j in bins[key]}
        for j in sorted(possible):
            budget.use()
            coplanar = all(abs(dot(sub(p, piece["polygon"][0]), normal)) <= EPS
                           for p in pieces[j]["polygon"])
            if coplanar and root(i) != root(j) and touches(piece["polygon"], pieces[j]["polygon"], normal):
                parent[root(i)] = root(j)
        for key in keys:
            bins[key].append(i)
    groups = defaultdict(list)
    for i, piece in enumerate(pieces):
        groups[root(i)].append(piece)
    return list(groups.values())


def rectangle(polys, normal, budget):
    right, up = projection(normal)
    xy = sorted(set((dot(p, right), dot(p, up)) for poly in polys for p in poly))
    def turn(o, a, b):
        return (a[0]-o[0])*(b[1]-o[1])-(a[1]-o[1])*(b[0]-o[0])
    def half(points):
        h = []
        for p in points:
            while len(h) > 1 and turn(h[-2], h[-1], p) <= 1e-10:
                h.pop()
            h.append(p)
        return h
    hull = half(xy)[:-1]+half(list(reversed(xy)))[:-1]
    best = None
    for a, b in zip(hull, hull[1:]+hull[:1]):
        budget.use(len(hull))
        r = unit(sub(b, a))
        if r is None:
            continue
        u = (-r[1], r[0])
        xs, ys = [dot(p, r) for p in hull], [dot(p, u) for p in hull]
        lo, hi = (min(xs), min(ys)), (max(xs), max(ys))
        size = (hi[0]-lo[0], hi[1]-lo[1])
        candidate = (size[0]*size[1], r, u, lo, hi, size)
        if best is None or candidate < best:
            best = candidate
    if best is None:
        return None
    area, r, u, lo, hi, size = best
    wr, wu = add(mul(right, r[0]), mul(up, r[1])), add(mul(right, u[0]), mul(up, u[1]))
    plane = dot(polys[0][0], normal)
    center = add(mul(normal, plane), add(mul(wr, (lo[0]+hi[0])/2), mul(wu, (lo[1]+hi[1])/2)))
    if size[1] > size[0]:
        wr, wu, size = wu, mul(wr, -1), tuple(reversed(size))
    return dict(center=center, right=wr, up=wu, width=size[0], height=size[1], area=area)


def summarize(group, normal, map_hash, material, model, budget):
    # Union all stage/source polygons. Coincident hints/layers never multiply support area.
    unique, seen = [], set()
    for piece in group:
        poly = piece["polygon"]
        key = tuple(sorted(tuple(round(x, 9) for x in p) for p in poly))
        if key in seen:
            continue
        seen.add(key)
        pending = [poly]
        for cutter in unique:
            next_pending = []
            for candidate in pending:
                budget.use()
                if interior_overlap(candidate, cutter, normal):
                    next_pending.extend(subtract_polygon(candidate, cutter, normal))
                else:
                    next_pending.append(candidate)
            pending = next_pending
            if not pending:
                break
        unique.extend(pending)
        if len(unique) > budget.max_pieces:
            raise ValueError("Fixture union piece limit")
    total, moment = 0., (0., 0., 0.)
    for poly in unique:
        area, center = measure(poly)
        total += area
        moment = add(moment, mul(center, area))
    if total < 1e-12:
        raise ValueError("Empty emitting support")
    known = all(p["known"] for p in group)
    issues = sorted({issue for p in group for issue in p["issues"]})
    sources = sorted({tuple(ref) for p in group for ref in p["source_triangles"]})
    support_key = sorted(tuple(sorted(tuple(round(x, 9) for x in p) for p in poly)) for poly in unique)
    fixture_id = identity(map_hash, material, model, sources, support_key, known)
    layers = []
    for layer_id in sorted({p["layer_id"] for p in group}):
        parts = [p for p in group if p["layer_id"] == layer_id]
        weights = [measure(p["polygon"])[0] for p in parts]
        area = sum(weights)
        rgb = tuple(sum(w*p["rgb"][k] for w, p in zip(weights, parts))/area for k in range(3))
        layers.append(dict(id=layer_id, stage=parts[0]["stage"], texture=parts[0]["texture"],
                           colour_prior=rgb if all(p["colour_known"] for p in parts) else None,
                           area=area, footprint=[p["polygon"] for p in parts],
                           provenance="inferred_relative_texture_colour"))
    bounds = rectangle(unique, normal, budget)
    coverage = min(1., total/bounds["area"]) if bounds and bounds["area"] else 0.
    suggestions = [dict(type="surface", footprint_reference="footprint")]
    if known and all(p["orientation_known"] for p in group) and bounds and coverage >= .98:
        suggestions.append(dict(type="rectangle", **bounds))
        if bounds["width"] <= 4:
            suggestions.append(dict(type="point", position=mul(moment, 1/total)))
        if bounds["width"] >= 6*max(bounds["height"], 1e-12):
            suggestions.append(dict(type="tube", start=add(bounds["center"], mul(bounds["right"], -bounds["width"]/2)),
                                    end=add(bounds["center"], mul(bounds["right"], bounds["width"]/2)),
                                    radius=bounds["height"]/2))
    return dict(id=fixture_id, material=material, model=model, source_triangles=sources,
                coordinate_space="world" if model == 0 else "original_brush_model_unplaced",
                surfaces=sorted({s for s, _ in sources}), centroid=mul(moment, 1/total),
                outward=normal if all(p["orientation_known"] for p in group) else None,
                normal_origin=sorted({p["normal_origin"] for p in group}),
                emitting_footprint_known=known, support_area=total, emitting_area=total if known else None,
                footprint_interpretation="static_RGB_mask_threshold_support" if known else "uncertain_whole_surface_support",
                optical_coverage_known=known and not any(x in issues for x in
                    ("alpha_composition_unknown", "translucent_composition_unknown", "extended_emission_parameters_unknown")),
                footprint=unique, rectangle_coverage=coverage, suggestions=suggestions,
                layers=layers, issues=issues, static_eligible=not any(x in issues for x in
                    ("moving_or_unknown_model", "deformed_geometry", "animated_stage", "animated_or_dependent_colour")),
                two_sided=any(p["two_sided"] for p in group), historical_bake_participation="unknown",
                colour_prior_interpretation="area-weighted lit-run averages; assumed sRGB transfer; relative only",
                priors=group[0]["priors"])


def catalogue(data, table, masks, options=None, conflicts=()):
    options = options or {}
    budget = Budget(options.get("max_work", 5000000), options.get("max_pieces", 250000))
    surfaces = read_bsp(data)
    buckets, unsupported, seen_triangles = defaultdict(list), [], {}
    luminous_layers = set()
    stats = dict(bsp_surfaces=len(surfaces), luminous_surfaces=0, source_triangles=0,
                 duplicate_triangles=0, degenerate_triangles=0, clipped_polygons=0)
    map_hash = digest(data)
    for surface in surfaces:
        shader = table.get(surface["material"])
        if not shader or shader["excluded"] or not shader["layers"]:
            continue
        stats["luminous_surfaces"] += 1
        base_issues = ["historical_bake_unknown", "colour_transfer_assumed"]+shader["issues"]
        if surface["model"] != 0:
            base_issues.append("moving_or_unknown_model")
        if surface["material"] in conflicts:
            base_issues.append("ambiguous_shader_definition")
        priors = [dict(origin="glow_display_hint", authored=False, intensity=None)] if any(l["glow"] for l in shader["layers"]) else []
        if shader["surface_light"]:
            priors.append(dict(origin="q3map_surfacelight", authored=True, value=shader["surface_light"],
                               unit="legacy_bake_tool_uncalibrated", colour=shader["source_colour"]))
        if surface["kind"] not in (1, 3) or not surface["indices"]:
            unsupported.append(dict(id=identity(map_hash, "unsupported", surface["ordinal"]),
                surface=surface["ordinal"], material=surface["material"], kind=surface["kind"],
                centroid=None, outward=None, footprint=None, priors=priors,
                issues=sorted(set(base_issues+["unsupported_geometry"]))))
            continue
        for ti in range(0, len(surface["indices"]), 3):
            triangle = [surface["vertices"][i] for i in surface["indices"][ti:ti+3]]
            points = [v["p"] for v in triangle]
            geometric = unit(cross(sub(points[1], points[0]), sub(points[2], points[0])))
            if geometric is None:
                stats["degenerate_triangles"] += 1
                continue
            stats["source_triangles"] += 1
            if stats["source_triangles"] > 500000:
                raise ValueError("Emission triangle limit")
            # RBSP front-facing draw indices are reversed relative to authored normals.
            # Use the original face normal, then verified vertex normals, to recover the side.
            source_normal = unit(surface["plane_normal"]) if surface["kind"] == 1 else None
            normal_origin = "bsp_face_plane"
            if source_normal is None or abs(dot(source_normal, geometric)) < .999:
                source_normal = unit(tuple(sum(v["normal"][k] for v in triangle) for k in range(3)))
                normal_origin = "bsp_vertex_normals"
            orientation_known = (source_normal is not None and abs(dot(source_normal, geometric)) >= .999
                                 and "cull_policy_unknown" not in base_issues)
            normal = mul(geometric, 1 if dot(geometric, source_normal) >= 0 else -1) if orientation_known else geometric
            if not orientation_known:
                normal_origin = "unknown"
            if shader["cull"] in ("back", "backsided"):
                normal = mul(normal, -1)
            # Deduplicate exact complete source facets; UV/material/model/side all participate.
            tri_key = (surface["material"], surface["model"], normal,
                       tuple(sorted((v["p"], v["uv"]) for v in triangle)))
            if tri_key in seen_triangles:
                stats["duplicate_triangles"] += 1
                seen_triangles[tri_key].append((surface["ordinal"], ti//3))
                continue
            seen_triangles[tri_key] = [(surface["ordinal"], ti//3)]
            for layer in shader["layers"]:
                budget.use()
                issues = base_issues+layer["issues"]
                rects = masks.get(layer["texture"]) if layer["texture"] and not layer["texture"].startswith("$") else None
                manifest = masks.manifest.get(layer["texture"], {})
                if manifest.get("alpha_unknown"):
                    issues = issues+["alpha_composition_unknown"]
                layer_id = identity(surface["material"], shader.get("sha256"), layer, manifest)
                known = rects is not None and "coordinates_unsupported" not in issues and "animated_stage" not in issues
                parts = footprint(triangle, layer, rects, budget) if known else None
                empty_mask = parts == []
                if parts:
                    luminous_layers.add((surface["ordinal"], layer_id))
                if parts is None or not parts:
                    issues = issues+["mask_unavailable" if rects is None else
                                     "no_luminous_mask_samples" if parts == [] else "coordinates_unsupported"]
                    parts, known = [(points, (1., 1., 1.))], False
                for polygon, rgb in parts:
                    polygon = orient(polygon, normal)
                    key = (surface["material"], surface["model"], tuple(round(x, 6) for x in normal),
                           round(dot(normal, points[0]), 5), known)
                    buckets[key].append(dict(polygon=polygon, surface=surface["ordinal"], triangle=ti//3,
                        known=known, orientation_known=orientation_known, normal_origin=normal_origin,
                        normal=normal,
                        source_triangles=seen_triangles[tri_key],
                        empty_mask=empty_mask,
                        issues=issues, layer_id=layer_id, stage=layer["ordinal"], texture=layer["texture"],
                        rgb=tuple(x*y for x, y in zip(rgb, layer["colour"])), colour_known=known,
                        two_sided=shader["cull"] in ("none", "disable", "twosided"), priors=priors))
                    stats["clipped_polygons"] += 1
                    if stats["clipped_polygons"] > budget.max_pieces:
                        raise ValueError("Fixture polygon limit; no partial catalogue published")
    fixtures = []
    for key, pieces in sorted(buckets.items(), key=lambda kv: str(kv[0])):
        # A dark triangle beside a lit one is a masked-out part of that surface,
        # not another whole-triangle lamp. Retain the uncertain shader hint only
        # when the entire original surface/layer has no luminous support.
        pieces = [p for p in pieces if not p["empty_mask"] or
                  (p["surface"], p["layer_id"]) not in luminous_layers]
        if not pieces:
            continue
        material, model, _, _, _ = key
        # Retain full precision for calculations; quantization only indexes possible groups.
        normal = pieces[0]["normal"]
        for group in components(pieces, normal, budget):
            fixtures.append(summarize(group, normal, map_hash, material, model, budget))
    fixtures.sort(key=lambda f: f["id"])
    if len({f["id"] for f in fixtures}) != len(fixtures):
        raise ValueError("Fixture ID collision")
    stats.update(fixtures=len(fixtures), supported_footprints=sum(f["emitting_footprint_known"] for f in fixtures),
                 unsupported_surfaces=len(unsupported),
                 shape_counts={kind: sum(any(s["type"] == kind for s in f["suggestions"]) for f in fixtures)
                               for kind in ("point", "rectangle", "tube", "surface")})
    used_materials = {s["material"] for s in surfaces}
    return dict(schema=SCHEMA, map_sha256=map_hash, options=options, counts=stats,
                fixtures=fixtures, unsupported=unsupported, masks=masks.manifest,
                shader_manifest={name: {k: shader.get(k) for k in ("source", "sha256")}
                    for name, shader in sorted(table.items()) if name in used_materials},
                interpretation="connected planar fixture hypotheses; historical lamp identity and bake participation unknown")


def write_catalogue(result, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    (output/"catalogue.json").write_text(json.dumps(result, indent=2, sort_keys=True, allow_nan=False)+"\n", encoding="utf-8")
    fields = ("id", "material", "surfaces", "centroid", "outward", "support_area", "emitting_area",
              "coordinate_space", "emitting_footprint_known", "optical_coverage_known", "static_eligible",
              "rectangle_coverage", "issues")
    lines = ["\t".join(fields)]
    for fixture in result["fixtures"]:
        lines.append("\t".join(json.dumps(fixture[k], separators=(",", ":")) for k in fields))
    (output/"fixtures.tsv").write_text("\n".join(lines)+"\n", encoding="utf-8")
    # Inspect actual support in any OBJ viewer; group names are stable full IDs.
    lines, offset = ["# ASR09 emitting/support polygons; uncertain records remain labelled"], 1
    for fixture in result["fixtures"]:
        lines.append("# RGB_support_known=%s optical_coverage_known=%s coordinates=%s issues=%s" %
                     (fixture["emitting_footprint_known"], fixture["optical_coverage_known"],
                      fixture["coordinate_space"], ",".join(fixture["issues"])))
        lines.append("g fixture_"+fixture["id"])
        for polygon in fixture["footprint"]:
            lines.extend("v %.9g %.9g %.9g" % tuple(p) for p in polygon)
            lines.append("f "+" ".join(str(i) for i in range(offset, offset+len(polygon))))
            offset += len(polygon)
    (output/"footprints.obj").write_text("\n".join(lines)+"\n", encoding="ascii")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--map", required=True, help="maps/name.bsp in the explicit asset view")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mask-axis", type=int, default=64)
    parser.add_argument("--threshold", type=float, default=.1)
    parser.add_argument("--max-work", type=int, default=5000000)
    parser.add_argument("--max-pieces", type=int, default=250000)
    args = parser.parse_args()
    if not 4 <= args.mask_axis <= 512 or not math.isfinite(args.threshold) or args.threshold < 0 or min(args.max_work, args.max_pieces) <= 0:
        parser.error("Invalid extraction limits")
    assets = Assets(args.base)
    try:
        data, source = assets.read(args.map)
        if data is None:
            parser.error("BSP unavailable in explicit asset view")
        table, conflicts = assets.shaders()
        options = dict(mask_axis=args.mask_axis, threshold=args.threshold, max_work=args.max_work,
                       max_pieces=args.max_pieces)
        result = catalogue(data, table, Masks(assets, args.mask_axis, args.threshold), options, conflicts)
        result.update(map_path=args.map, map_source=source,
                      shader_selection="lexical shader file order, first definition; duplicates flagged",
                      asset_selection="loose base then reverse lexical PK3; explicit view, no mod/runtime certification")
        write_catalogue(result, args.output)
        print(json.dumps(result["counts"], sort_keys=True))
    finally:
        assets.close()


if __name__ == "__main__":
    main()
