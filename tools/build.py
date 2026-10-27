"""Builds the feature cards and modal content of the site.

    python tools/build.py [path/to/rendering-features.md]

Reads the feature reference from the project's docs (by default the sibling
OpenJK-rend2 checkout), writes assets/features.js and replaces the card markup
between the <!-- cards:NAME --> markers in index.html.
"""
import html
import json
import re
import sys
from pathlib import Path

from cards import FORCE, GRAPHICS

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DOC = ROOT.parent / "OpenJK-rend2" / "docs" / "rendering-features.md"
DOCS_URL = "https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/"


def inline(text):
    text = html.escape(text, quote=False)
    text = re.sub(r"`([^`]+)`", r"<code>\1</code>", text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)

    def link(m):
        label, url = m.group(1), m.group(2)
        if not re.match(r"https?://", url):
            url = DOCS_URL + url.lstrip("./")
        return f'<a href="{url}" target="_blank" rel="noopener">{label}</a>'

    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", link, text)
    # bare doc paths become links to the repository
    text = re.sub(r"<code>docs/([\w.-]+\.md)</code>",
                  lambda m: f'<a href="{DOCS_URL}{m.group(1)}" target="_blank" rel="noopener"><code>docs/{m.group(1)}</code></a>', text)
    return text


def markdown(md):
    out, lines, i = [], md.strip("\n").split("\n"), 0
    para, items = [], []

    def flush():
        nonlocal para, items
        if para:
            text = " ".join(para)
            m = re.fullmatch(r"\*\*([^*]+)\*\*", text)
            if m:
                out.append(f"<h4>{html.escape(m.group(1))}</h4>")
            else:
                cls = ' class="req"' if text.startswith("**Requirements.**") else ""
                out.append(f"<p{cls}>{inline(text)}</p>")
        if items:
            out.append("<ul>" + "".join(f"<li>{inline(it)}</li>" for it in items) + "</ul>")
        para, items = [], []

    while i < len(lines):
        line = lines[i]
        fence = re.match(r"^(~~~|```)", line)
        if fence:
            flush()
            body = []
            i += 1
            while i < len(lines) and not lines[i].startswith(fence.group(1)):
                body.append(lines[i])
                i += 1
            out.append("<pre><code>" + html.escape("\n".join(body)) + "</code></pre>")
        elif not line.strip():
            flush()
        elif line.startswith("- "):
            if para:
                flush()
            items.append(line[2:].strip())
        elif items and line.startswith("  "):
            items[-1] += " " + line.strip()
        else:
            if items:
                flush()
            para.append(line.strip())
        i += 1
    flush()
    return "\n".join(out)


def parse_sections(doc):
    sections, title, body = {}, None, []
    for line in doc.read_text(encoding="utf-8").split("\n"):
        if line.startswith("## ") or line.startswith("# "):
            if title:
                sections[title] = "\n".join(body)
            title = line[3:].strip() if line.startswith("## ") else None
            body = []
        elif title:
            body.append(line)
    if title:
        sections[title] = "\n".join(body)
    return sections


def card_html(card, extra_class="", style=""):
    attrs = f' style="{style}"' if style else ""
    return (f'<article class="card{extra_class}" data-feature="{card["id"]}" tabindex="0" role="button" '
            f'aria-haspopup="dialog"{attrs}><h4>{card["title"]}</h4><p>{card["short"]}</p>'
            f'<code>{html.escape(card["cvar"])}</code></article>')


def main():
    doc = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_DOC
    sections = parse_sections(doc)
    features, missing = {}, []

    gfx = []
    for group, cards in GRAPHICS:
        gfx.append(f"      <h3>{group}</h3>\n      <div class=\"grid\">")
        for card in cards:
            parts = []
            for name in card.get("sections", []):
                if name not in sections:
                    missing.append(name)
                    continue
                parts.append({"title": name, "html": markdown(sections[name])})
            if card.get("doc"):
                parts.append({"title": card["title"], "html": markdown(card["doc"])})
            features[card["id"]] = {
                "group": group, "title": card["title"], "cvar": card["cvar"],
                "how": card.get("how", ""), "sections": parts, "media": card.get("media", []),
            }
            gfx.append("        " + card_html(card))
        gfx.append("      </div>\n")

    force = []
    for card in FORCE:
        features[card["id"]] = {
            "group": "Force powers", "title": card["title"], "cvar": card["cvar"],
            "how": card.get("how", ""), "sections": [{"title": card["title"], "html": markdown(card["doc"])}],
            "media": card.get("media", []),
        }
        force.append("        " + card_html(card, " force", f"--c:var(--{card['color']})"))

    if missing:
        sys.exit("sections not found in the doc: " + ", ".join(missing))

    js = "window.FEATURES = " + json.dumps(features, ensure_ascii=False, indent=1) + ";\n"
    (ROOT / "assets" / "features.js").write_text(js, encoding="utf-8")

    index = ROOT / "index.html"
    page = index.read_text(encoding="utf-8")
    for name, body in (("graphics", "\n".join(gfx)), ("force", "\n".join(force))):
        page, n = re.subn(rf"(<!-- cards:{name} -->\n).*?(\s*<!-- /cards:{name} -->)",
                          lambda m: m.group(1) + body + m.group(2), page, flags=re.S)
        if n != 1:
            sys.exit(f"marker cards:{name} not found in index.html")
    index.write_text(page, encoding="utf-8")
    print(f"{len(features)} features, {sum(len(f['sections']) for f in features.values())} doc sections")


if __name__ == "__main__":
    main()
