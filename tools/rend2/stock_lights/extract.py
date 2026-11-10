#!/usr/bin/env python3
"""Extract surviving authored lighting from local JA stock PK3s (stdlib only).

No inverse fitting: entity blocks are copied verbatim; shader declarations and
their BSP surface geometry are exported separately from entity lights.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import struct
import zipfile


SCHEMA = "org.openjk.stock-authored-lights/1"
HEADER_SIZE = 152
# Match COM_Parse's quoted strings: backslashes are literal, not JSON escapes.
TOKEN = re.compile(r'\s+|//[^\n]*|/\*.*?\*/|"[^"]*"|[{}]|[^\s{}"]+', re.S)
SUN_KEYS = {"sun", "q3map_sun", "q3map_sunext"}
LIGHT_KEYS = SUN_KEYS | {
    "q3map_surfacelight", "q3map_skylight", "q3map_lightrgb",
    "q3map_lightimage", "q3map_lightstyle", "q3map_lightsubdivide",
    "q3map_backsplash", "q3map_baseshader", "q3map_lightmapfilterradius",
    "q3map_lightmapsamplesize", "q3map_surfacemodel", "qer_editorimage",
    "surfaceparm", "skyparms", "cull", "deformvertexes",
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def lex(text):
    """Keep token spans so entity fragments retain their exact source bytes."""
    pos = 0
    while pos < len(text):
        match = TOKEN.match(text, pos)
        if match is None:
            raise ValueError("Invalid/unclosed token at byte %d" % pos)
        word = match.group()
        if not word.isspace() and not word.startswith(("//", "/*")):
            yield word, match.start(), match.end()
        pos = match.end()


def entities(raw):
    """Strict flat entity parser; a failed parse must never become zero lights."""
    text = raw.rstrip(b"\0").decode("latin1")
    words = list(lex(text))
    result, i = [], 0
    while i < len(words):
        if words[i][0] != "{":
            raise ValueError("Expected opening entity brace")
        start, pairs = words[i][1], []
        i += 1
        while i < len(words) and words[i][0] != "}":
            if i + 1 >= len(words) or not all(
                    words[j][0].startswith('"') for j in (i, i + 1)):
                raise ValueError("Expected quoted entity key/value")
            pairs.append((words[i][0][1:-1], words[i + 1][0][1:-1]))
            i += 2
        if i == len(words):
            raise ValueError("Unclosed entity")
        # The runtime looks up the first matching key, not the last one.
        values = {}
        for key, value in pairs:
            values.setdefault(key, value)
        result.append(dict(index=len(result), keys=values, pairs=pairs,
                           raw=text[start:words[i][2]]))
        i += 1
    if not result or result[0]["keys"].get("classname") != "worldspawn":
        raise ValueError("Missing leading worldspawn")
    return result


def bsp_lumps(data):
    if len(data) < HEADER_SIZE or data[:8] != b"RBSP\x01\0\0\0":
        raise ValueError("Expected JA RBSP v1")
    result = []
    for i in range(18):
        offset, length = struct.unpack_from("<ii", data, 8 + i * 8)
        if length < 0 or offset < 0 or offset + length > len(data):
            raise ValueError("Invalid BSP lump %d" % i)
        if length and offset < HEADER_SIZE:
            raise ValueError("BSP lump overlaps header")
        result.append(memoryview(data)[offset:offset + length])
    return result


def number(value, count=1):
    if value is None:
        return None
    try:
        values = [float(v) for v in value.split()]
    except ValueError:
        return None
    if len(values) != count or not all(math.isfinite(v) for v in values):
        return None
    return values[0] if count == 1 else values


def light_record(entity, all_entities):
    keys = entity["keys"]
    result = dict(entity_index=entity["index"], keys=keys, pairs=entity["pairs"],
                  origin=number(keys.get("origin"), 3),
                  color=number(keys.get("_color", keys.get("color")), 3),
                  light=number(keys.get("_light", keys.get("light"))),
                  scale=number(keys.get("scale")), style=number(keys.get("style")),
                  targetname=keys.get("targetname"), target=keys.get("target"),
                  spawnflags=number(keys.get("spawnflags")),
                  interpretation="authored entity; intensity is in legacy compiler units")
    result["target_entities"] = [dict(entity_index=e["index"], keys=e["keys"])
        for e in all_entities if keys.get("target") and
        e["keys"].get("targetname") == keys["target"]]
    return result


def shader_definitions(text):
    """Return all definitions, including duplicates, with original shader text."""
    words = list(lex(text))
    i = 0
    while i < len(words):
        if i + 1 >= len(words) or words[i + 1][0] != "{":
            raise ValueError("Expected shader name and opening brace")
        name, start = words[i][0].strip('"').lower(), words[i][1]
        j, depth, top, glow = i + 2, 1, [], False
        while j < len(words) and depth:
            token = words[j][0]
            if token == "{":
                depth += 1
            elif token == "}":
                depth -= 1
            elif depth == 1:
                top.append((token.strip('"'), words[j][1]))
            elif token.lower() == "glow":
                glow = True
            j += 1
        if depth:
            raise ValueError("Unclosed shader: " + name)
        # Stock shader directives are line based. Preserve complete arguments,
        # instead of guessing a shader's compiler or physical light model.
        lines = []
        for token, offset in top:
            if token.lower() not in LIGHT_KEYS:
                continue
            line_end = text.find("\n", offset)
            line_end = len(text) if line_end < 0 else line_end
            args = [w[0].strip('"') for w in words[i + 2:j - 1]
                    if offset + len(token) <= w[1] < line_end and w[0] not in ("{", "}")]
            lines.append(dict(keyword=token.lower(), args=args))
        raw = text[start:words[j - 1][2]]
        yield name, dict(directives=lines, glow=glow, text=raw,
                         sha256=sha(raw.encode("latin1")))
        i = j


def scalar_directive(shader, key):
    matches = [d for d in shader["directives"] if d["keyword"] == key]
    return number(matches[-1]["args"][0]) if matches and matches[-1]["args"] else None


def surface_sources(lumps, shaders):
    for i, stride in ((1, 72), (7, 40), (10, 80), (11, 4), (13, 148)):
        if len(lumps[i]) % stride:
            raise ValueError("Incomplete BSP record in lump %d" % i)
    names = [bytes(lumps[1][p:p + 64]).split(b"\0", 1)[0].decode("latin1").lower()
             for p in range(0, len(lumps[1]), 72)]
    model_for = {}
    for model, p in enumerate(range(0, len(lumps[7]), 40)):
        first, count = struct.unpack_from("<ii", lumps[7], p + 24)
        if first < 0 or count < 0 or first + count > len(lumps[13]) // 148:
            raise ValueError("Invalid model surface range")
        for s in range(first, first + count):
            model_for[s] = model
    uses, emitters, glow_only = Counter(), [], 0
    for s, p in enumerate(range(0, len(lumps[13]), 148)):
        si, _, kind, fv, nv, fi, ni = struct.unpack_from("<7i", lumps[13], p)
        if not 0 <= si < len(names) or min(fv, nv, fi, ni) < 0 or \
                fv + nv > len(lumps[10]) // 80 or fi + ni > len(lumps[11]) // 4:
            raise ValueError("Invalid BSP surface references")
        name = names[si]
        uses[name] += 1
        definitions = shaders.get(name, [])
        values = [scalar_directive(d, "q3map_surfacelight") for d in definitions]
        if not any(v is not None and v > 0 for v in values):
            glow_only += any(d["glow"] for d in definitions)
            continue
        positions = [struct.unpack_from("<3f", lumps[10], v * 80)
                     for v in range(fv, fv + nv)]
        normals = [struct.unpack_from("<3f", lumps[10], v * 80 + 52)
                   for v in range(fv, fv + nv)]
        uv = [struct.unpack_from("<2f", lumps[10], v * 80 + 12)
              for v in range(fv, fv + nv)]
        indices = struct.unpack_from("<%di" % ni, lumps[11], fi * 4)
        if any(v < 0 or v >= nv for v in indices):
            raise ValueError("Invalid surface indices")
        emitters.append(dict(surface_index=s, shader=name, model_index=model_for.get(s),
            surface_type=kind, surfacelight_declarations=values,
            coordinate_space="world" if model_for.get(s) == 0 else "inline_model_local",
            positions=positions, normals=normals, uv=uv, indices=indices,
            patch_dimensions=list(struct.unpack_from("<2i", lumps[13], p + 140)) if kind == 2 else None,
            geometry="Bezier control net" if kind == 2 else "BSP vertices and local indices"))
    used = []
    for name in sorted(uses):
        for shader in shaders.get(name, []):
            if shader["directives"]:
                used.append(dict(shader, name=name, bsp_surfaces=uses[name],
                                 duplicate_definition=len(shaders[name]) > 1))
    return used, emitters, glow_only


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=True, allow_nan=False) + "\n",
                    encoding="utf-8")


def extract(base, output):
    archive_paths = sorted(base.glob("assets[0-3].pk3"), key=lambda p: p.name.lower())
    if not archive_paths:
        raise ValueError("No stock assets0-3.pk3 found in " + str(base))
    archives, maps, shader_files, variants = [], {}, {}, []
    for path in archive_paths:
        with zipfile.ZipFile(path) as archive:
            archives.append(dict(name=path.name, bytes=path.stat().st_size))
            for info in archive.infolist():
                name = info.filename.replace("\\", "/").lower()
                if name.startswith("shaders/") and name.endswith(".shader"):
                    shader_files[name] = (path, info.filename)
                if name.startswith("maps/") and name.endswith(".bsp"):
                    if ".." in PurePosixPath(name).parts or ":" in name:
                        raise ValueError("Unsafe map path")
                    data = archive.read(info)
                    es = entities(bytes(bsp_lumps(data)[0]))
                    lights = sum(e["keys"].get("classname", "").lower().startswith("light") for e in es)
                    variants.append(dict(archive=path.name, member=info.filename,
                                         sha256=sha(data), entities=len(es), light_entities=lights))
                    maps[name] = (path, info.filename)
    if not maps:
        raise ValueError("No stock BSPs found")
    shaders = {}
    for name, (path, member) in sorted(shader_files.items()):
        with zipfile.ZipFile(path) as archive:
            data = archive.read(member)
        for material, record in shader_definitions(data.decode("latin1")):
            shaders.setdefault(material, []).append(dict(record, archive=path.name,
                member=member, file_sha256=sha(data)))
    output.mkdir(parents=True, exist_ok=True)
    summary = dict(schema=SCHEMA, archives=archives, variants=variants, maps=[],
        selection="assets0-3 only; later archive overrides same member; shader definition conflicts retained",
        limitations=["Missing entity lights cannot be recovered by decompilation or adding -keeplights afterwards.",
            "Shader declarations are surviving authored metadata, not proof of historical compiler settings.",
            "Surface counts are BSP fragments, not original lamps or compiler emission samples.",
            "Glow alone is not a baked light source; glow-only surfaces counted separately.",
            "Inline-model coordinates need entity transforms; patches contain untessellated control nets.",
            "No texture-derived emission color, external model lights, FX lights, bounce lights or physical-unit calibration inferred."])
    combined = []
    for name, (path, member) in sorted(maps.items()):
        with zipfile.ZipFile(path) as archive:
            data = archive.read(member)
        lumps = bsp_lumps(data)
        raw = bytes(lumps[0])
        es = entities(raw)
        lights = [e for e in es if e["keys"].get("classname", "").lower().startswith("light")]
        used, emitters, glow_only = surface_sources(lumps, shaders)
        world = es[0]["keys"]
        suppressed = world.get("_noshadersun", "").startswith("1")
        suns = []
        for shader in used:
            for directive in shader["directives"]:
                if directive["keyword"] in SUN_KEYS:
                    suns.append(dict(shader=shader["name"], archive=shader["archive"],
                        member=shader["member"], directive=directive,
                        suppressed_by_worldspawn=suppressed,
                        duplicate_definition=shader["duplicate_definition"]))
        directory = output / PurePosixPath(name[:-4])
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "entities.ent").write_bytes(raw)
        (directory / "lights.ent").write_bytes(
            ("\n".join(e["raw"] for e in lights) + ("\n" if lights else "")).encode("latin1"))
        records = [light_record(e, es) for e in lights]
        result = dict(schema=SCHEMA, map=name, archive=path.name, member=member,
            bsp_sha256=sha(data), entity_lump_sha256=sha(raw), worldspawn=world,
            light_entities=records, shader_suns=suns, used_shader_metadata=used,
            info_null_targets=[dict(entity_index=e["index"], keys=e["keys"]) for e in es
                if e["keys"].get("classname") in ("info_null", "info_notnull", "target_position")],
            runtime_light_entities=[dict(entity_index=e["index"], keys=e["keys"]) for e in es
                if e["keys"].get("classname") in ("misc_dlight", "misc_spotlight")])
        write_json(directory / "lighting.json", result)
        write_json(directory / "surface-emitters.json", emitters)
        counts = dict(entities=len(es), light_entities=len(records),
            targetnamed_lights=sum(bool(e["keys"].get("targetname")) for e in lights),
            shader_sun_declarations=len(suns),
            unsuppressed_shader_sun_declarations=sum(not s["suppressed_by_worldspawn"] for s in suns),
            surfacelight_surfaces=len(emitters),
            glow_only_surfaces=glow_only,
            shader_conflicts=sum(s["duplicate_definition"] for s in used))
        summary["maps"].append(dict(map=name, archive=path.name, bsp_sha256=sha(data), **counts))
        combined.extend(dict(record, map=name, archive=path.name, bsp_sha256=sha(data)) for record in records)
        print(name, json.dumps(counts, sort_keys=True))
    summary["totals"] = dict(maps=len(maps), bsp_variants=len(variants),
        sp_maps=sum(not n.startswith("maps/mp/") for n in maps),
        mp_maps=sum(n.startswith("maps/mp/") for n in maps),
        light_entities=len(combined), maps_with_lights=sum(m["light_entities"] > 0 for m in summary["maps"]),
        surfacelight_surfaces=sum(m["surfacelight_surfaces"] for m in summary["maps"]),
        shader_sun_declarations=sum(m["shader_sun_declarations"] for m in summary["maps"]),
        unsuppressed_shader_sun_declarations=sum(m["unsuppressed_shader_sun_declarations"] for m in summary["maps"]))
    write_json(output / "lights.json", combined)
    write_json(output / "summary.json", summary)
    lines = ["# JA stock authored-light extraction", "", "```json",
             json.dumps(summary["totals"], indent=2), "```", "",
             "| Map | Light entities | Surfacelight BSP surfaces | Sun declarations (not suppressed) |",
             "|---|---:|---:|---:|"]
    for m in summary["maps"]:
        lines.append("| %s | %d | %d | %d (%d) |" % (m["map"], m["light_entities"],
            m["surfacelight_surfaces"], m["shader_sun_declarations"], m["unsuppressed_shader_sun_declarations"]))
    lines += ["", *["- " + value for value in summary["limitations"]], ""]
    (output / "report.md").write_text("\n".join(lines), encoding="utf-8")
    return summary


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", type=Path, required=True, help="Licensed GameData/base directory")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = extract(args.base, args.output)
    print("TOTAL", json.dumps(result["totals"], sort_keys=True))


if __name__ == "__main__":
    main()
