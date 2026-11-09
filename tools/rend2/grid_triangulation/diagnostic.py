"""Private geometry/direction SVG; schematic projections, not renderer lighting.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from html import escape
import math
from pathlib import Path
import re
import xml.etree.ElementTree as ET


def render(path, report, rows, mesh):
    roi = report["roi_world"]
    svg = ['<svg xmlns="http://www.w3.org/2000/svg" width="1200" height="730" viewBox="0 0 1200 730">',
           '<rect width="1200" height="730" fill="#101923"/>',
           '<g font-family="sans-serif" fill="#dce9f0">',
           '<text x="28" y="30" font-size="21">Original lightgrid • conditional source-location diagnostic</text>',
           '<text x="28" y="56" font-size="13">Cyan: train  ·  Orange: validation/held-out  ·  Red: hypotheses  ·  Grey: captured static triangles</text>']
    for panel, axes in enumerate(((0, 1), (0, 2))):
        x0, y0, width, height = 35+panel*600, 95, 545, 525
        a, b = axes
        scale = min(width/(roi[a+3]-roi[a]), height/(roi[b+3]-roi[b]))
        def xy(p): return x0+(p[a]-roi[a])*scale, y0+height-(p[b]-roi[b])*scale
        clip = f"panel{panel}"
        svg += [f'<text x="{x0}" y="82" font-size="16">World {"XYZ"[a]} / {"XYZ"[b]} (WU)</text>',
                f'<defs><clipPath id="{clip}"><rect x="{x0}" y="{y0}" width="{width}" height="{height}"/></clipPath></defs>',
                f'<g clip-path="url(#{clip})">']
        for tri in mesh["triangles"]:
            points = " ".join(f"{x:.2f},{y:.2f}" for x, y in map(xy, tri))
            svg.append(f'<polygon points="{points}" fill="none" stroke="#83929c" stroke-width=".6" opacity=".23"/>')
        for row in rows:
            x, y = xy(row["position"])
            d = row["direction"]
            colour = "#5ce6e0" if row["role"] == "train" else "#ffc06c"
            ex, ey = x+d[a]*22, y-d[b]*22
            svg += [f'<circle cx="{x:.2f}" cy="{y:.2f}" r="2.8" fill="{colour}"/>',
                    f'<path d="M{x:.2f},{y:.2f} L{ex:.2f},{ey:.2f}" stroke="{colour}" stroke-width="1.2"/>']
        for c in report["search"]["candidates"]:
            x, y = xy(c["position"])
            tx = max(x0+8, min(x+10, x0+width-100))
            svg += [f'<circle cx="{x:.2f}" cy="{y:.2f}" r="7" fill="none" stroke="#ff7288" stroke-width="2"/>',
                    f'<text x="{tx:.2f}" y="{y-9:.2f}" font-size="13" fill="#ff9aaa">#{c["rank"]}{" Unknown" if c["status"] == "unknown" else ""}</text>']
            region = c["region"]
            if region["axes_world"]:
                for length, direction in zip(region["axes_world"], region["axes_directions"]):
                    dx, dy = length*direction[a]*scale, -length*direction[b]*scale
                    svg.append(f'<path d="M{x-dx:.2f},{y-dy:.2f} L{x+dx:.2f},{y+dy:.2f}" stroke="#ff7288" stroke-dasharray="4 3" opacity=".7"/>')
        svg += ['</g>', f'<rect x="{x0}" y="{y0}" width="{width}" height="{height}" fill="none" stroke="#617482"/>',
                f'<text x="{x0}" y="{y0+height+21}" font-size="12">Bounds: {roi[a]:g}…{roi[a+3]:g} / {roi[b]:g}…{roi[b+3]:g}</text>']
    summary = (f'{report["input_cells"]} original cells; {report["eligible_cells"]} eligible; '
               f'{len(report["search"]["candidates"])} alternatives ({sum(c["status"] == "conditional_location" for c in report["search"]["candidates"])} conditional); {len(report["search"]["unknowns"])} Unknown clusters. '
               f'Geometry mesh coverage: {mesh["coverage_complete"]}; issues: {mesh["issues"]}.')
    svg += [f'<text x="28" y="675" font-size="13">{escape(summary)}</text>',
            '<text x="28" y="700" font-size="13">All locations conditional. Compression, sample nudges, aggregate/reflected light and dynamic occupancy can remain unknown.</text>',
            '</g></svg>']
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(svg)+"\n", encoding="utf-8")
    raster_preview(path)


def raster_preview(path):
    """Optional Pillow rasterization of this module's small SVG subset for visual QA."""
    try:
        from PIL import Image, ImageDraw, ImageFont
    except ImportError:
        return
    root = ET.fromstring(path.read_text(encoding="utf-8"))
    canvas = Image.new("RGBA", (1200, 730))
    clips = {}
    for node in root.iter():
        if node.tag.endswith("clipPath"):
            rect = list(node)[0]
            x, y, w, h = [float(rect.get(k)) for k in ("x", "y", "width", "height")]
            clips[node.get("id")] = tuple(map(round, (x, y, x+w, y+h)))
    def colour(s, opacity):
        if not s or s == "none": return None
        return tuple(int(s[i:i+2], 16) for i in (1, 3, 5))+(round(255*opacity),)
    def visit(node, target, inherited_fill="#000000"):
        tag = node.tag.split("}")[-1]
        if tag in ("defs", "clipPath"): return
        fill = node.get("fill", inherited_fill)
        if tag in ("svg", "g"):
            group = Image.new("RGBA", canvas.size)
            for child in node: visit(child, group, fill)
            clip = node.get("clip-path")
            if clip:
                box = clips[clip[5:-1]]
                clipped = Image.new("RGBA", canvas.size)
                clipped.paste(group.crop(box), box[:2])
                group = clipped
            target.alpha_composite(group)
            return
        draw = ImageDraw.Draw(target)
        opacity = float(node.get("opacity", "1"))
        fc, sc = colour(fill, opacity), colour(node.get("stroke"), opacity)
        width = max(1, round(float(node.get("stroke-width", "1"))))
        if tag == "rect":
            x, y = float(node.get("x", "0")), float(node.get("y", "0"))
            box = (x, y, x+float(node.get("width")), y+float(node.get("height")))
            draw.rectangle(box, fill=fc, outline=sc, width=width)
        elif tag == "circle":
            x, y, r = (float(node.get(k)) for k in ("cx", "cy", "r"))
            draw.ellipse((x-r, y-r, x+r, y+r), fill=fc, outline=sc, width=width)
        elif tag == "polygon":
            points = [tuple(map(float, p.split(','))) for p in node.get("points").split()]
            if fc: draw.polygon(points, fill=fc)
            if sc: draw.line(points+[points[0]], fill=sc, width=width)
        elif tag == "path":
            numbers = list(map(float, re.findall(r'-?\d+(?:\.\d+)?', node.get("d"))))
            draw.line(list(zip(numbers[::2], numbers[1::2])), fill=sc, width=width)
        elif tag == "text":
            size = round(float(node.get("font-size", "13")))
            try: font = ImageFont.truetype("C:/Windows/Fonts/segoeui.ttf", size)
            except OSError: font = ImageFont.load_default()
            draw.text((float(node.get("x")), float(node.get("y"))-size), node.text or "", fill=fc, font=font)
    visit(root, canvas)
    canvas.convert("RGB").save(path.with_suffix(".png"))
