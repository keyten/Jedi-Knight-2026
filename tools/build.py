"""Builds the feature cards and modal content of the site.

    python tools/build.py [path/to/OpenJK-rend2/docs]

Reads docs/rendering-features.md and docs/rendering-features/*.md from the
project checkout (by default the sibling OpenJK-rend2 folder), writes
assets/features.js and replaces the markup between the <!-- cards:NAME -->
markers in index.html.
"""
import html
import json
import posixpath
import re
import sys
from pathlib import Path

from cards import FORCE, MEDIA, PAGES, SKIP

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_DOCS = ROOT.parent / "OpenJK-rend2" / "docs"
DOCS_URL = "https://github.com/keyten/BetterJK/blob/rend2-unified-wip/docs/"


def slug(title):
    """GitHub heading anchor."""
    s = re.sub(r"[^\w\- ]", "", title.lower())
    return s.replace(" ", "-")


class Renderer:
    def __init__(self, features):
        self.features = features  # ids of sections shown on the site
        self.page = ""

    def link(self, url):
        if re.match(r"https?://", url):
            return url, True
        path, _, anchor = url.partition("#")
        page = self.page
        if path:
            base = "" if self.page == "index" else "rendering-features"
            full = posixpath.normpath(posixpath.join(base, path))
            m = re.fullmatch(r"rendering-features/([\w-]+)\.md", full)
            if not m:
                return DOCS_URL + full + (f"#{anchor}" if anchor else ""), True
            page = m.group(1)
        fid = f"{page}/{anchor}" if anchor else None
        if fid in self.features:
            return f"#f/{fid}", False
        if not anchor and any(f.startswith(page + "/") for f in self.features):
            return f"#topic-{page}", False
        return DOCS_URL + f"rendering-features/{page}.md" + (f"#{anchor}" if anchor else ""), True

    def inline(self, text):
        text = html.escape(text, quote=False)
        text = re.sub(r"`([^`]+)`", r"<code>\1</code>", text)
        text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
        text = re.sub(r"(?<![\w*])\*([^*\n]+)\*(?![\w*])", r"<em>\1</em>", text)

        def link(m):
            href, external = self.link(html.unescape(m.group(2)))
            ext = ' target="_blank" rel="noopener"' if external else ""
            return f'<a href="{html.escape(href)}"{ext}>{m.group(1)}</a>'

        return re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", link, text)

    def paragraph(self, text):
        m = re.fullmatch(r"\*\*([^*]+)\*\*", text)
        if m:
            return f"<h4>{html.escape(m.group(1))}</h4>"
        if text.startswith("**How it works.**"):
            rest = self.inline(text[len("**How it works.**"):].strip())
            return f'<div class="how"><h3>How it works</h3><p>{rest}</p></div>'
        cls = ""
        if text.startswith("**Cost:**"):
            cls = "cost"
        elif text.startswith("**Mod authors:**"):
            cls = "mod"
        elif text.startswith("Design notes:"):
            cls = "notes"
        elif text.startswith("Needs ") or text.startswith("**Stock maps"):
            cls = "req"
        attr = f' class="{cls}"' if cls else ""
        return f"<p{attr}>{self.inline(text)}</p>"

    def markdown(self, md):
        out, lines, i = [], md.strip("\n").split("\n"), 0
        para, items = [], []

        def flush():
            nonlocal para, items
            if para:
                out.append(self.paragraph(" ".join(para)))
            if items:
                out.append("<ul>" + "".join(f"<li>{self.inline(it)}</li>" for it in items) + "</ul>")
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
            elif line.startswith("### "):
                flush()
                out.append(f'<h3 class="sub">{self.inline(line[4:].strip())}</h3>')
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


def read_page(path):
    text = path.read_text(encoding="utf-8")
    head, *parts = re.split(r"^## ", text, flags=re.M)
    title = re.search(r"^# (.+)$", head, flags=re.M).group(1).strip()
    intro = [p.strip() for p in head.split("\n\n")[1:]
             if p.strip() and not p.strip().startswith("[All rendering settings]")]
    sections = {}
    for part in parts:
        name, _, body = part.partition("\n")
        sections[name.strip()] = body
    return title, intro, sections


def card_html(fid, title, short, cvar, extra_class="", style=""):
    attrs = f' style="{style}"' if style else ""
    label = f"<code>{html.escape(cvar)}</code>" if cvar else ""
    return (f'<article class="card{extra_class}" data-feature="{fid}" tabindex="0" role="button" '
            f'aria-haspopup="dialog"{attrs}><h4>{title}</h4><p>{short}</p>{label}</article>')


def main():
    docs = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_DOCS
    pages = {name: read_page(docs / "rendering-features" / f"{name}.md") for name, _ in PAGES}

    errors, unlisted = [], []
    ids = set()
    for name, cards in PAGES:
        sections = pages[name][2]
        listed = [c[0] for c in cards]
        for title in listed:
            if title in sections:
                ids.add(f"{name}/{slug(title)}")
            else:
                errors.append(f"{name}: '{title}' is not in the doc")
        unlisted += [f"{name}: '{t}'" for t in sections if t not in listed and t not in SKIP.get(name, [])]
    if errors:
        sys.exit("\n".join(errors))

    r = Renderer(ids)
    features = {}

    # Before you start, from the index page
    index_md = (docs / "rendering-features.md").read_text(encoding="utf-8")
    m = re.search(r"^## Before you start\n(.*?)(?=^## )", index_md, flags=re.M | re.S)
    r.page = "index"
    before = r.markdown(m.group(1)) if m else ""

    gfx = ['      <nav class="topics" aria-label="Graphics topics">']
    gfx += [f'        <a href="#topic-{name}">{html.escape(pages[name][0])}</a>' for name, _ in PAGES]
    gfx.append("      </nav>")
    if before:
        gfx.append(f'      <div class="before">\n        <h3>Before you start</h3>\n        {before}\n      </div>')

    for name, cards in PAGES:
        title, intro, sections = pages[name]
        r.page = name
        gfx.append(f'      <div class="topic" id="topic-{name}">')
        gfx.append(f"        <h3>{html.escape(title)}</h3>")
        if intro:
            gfx.append(f'        <p class="topic-intro">{r.inline(intro[0])}</p>')
        gfx.append('        <div class="grid">')
        for sec_title, short, cvar in cards:
            fid = f"{name}/{slug(sec_title)}"
            features[fid] = {
                "group": title, "title": sec_title, "cvar": cvar or "",
                "sections": [{"title": sec_title, "html": r.markdown(sections[sec_title])}],
                "media": MEDIA.get(fid, []),
            }
            gfx.append("          " + card_html(fid, html.escape(sec_title), short, cvar))
        gfx.append("        </div>\n      </div>")

    force = []
    for card in FORCE:
        features[card["id"]] = {
            "group": "Force powers", "title": card["title"], "cvar": card["cvar"],
            "sections": [{"title": card["title"], "html": r.markdown(card["doc"])}],
            "media": MEDIA.get(card["id"], []),
        }
        force.append("        " + card_html(card["id"], card["title"], card["short"], card["cvar"],
                                            " force", f"--c:var(--{card['color']})"))

    js = "window.FEATURES = " + json.dumps(features, ensure_ascii=False, indent=1) + ";\n"
    (ROOT / "assets" / "features.js").write_text(js, encoding="utf-8")

    index = ROOT / "index.html"
    page = index.read_text(encoding="utf-8")
    for marker, body in (("graphics", "\n".join(gfx)), ("force", "\n".join(force))):
        page, n = re.subn(rf"(<!-- cards:{marker} -->\n).*?(\s*<!-- /cards:{marker} -->)",
                          lambda mm: mm.group(1) + body + mm.group(2), page, flags=re.S)
        if n != 1:
            sys.exit(f"marker cards:{marker} not found in index.html")
    index.write_text(page, encoding="utf-8")

    print(f"{len(features)} cards")
    if unlisted:
        print("doc sections not on the site (add them to tools/cards.py or SKIP):")
        print("\n".join("  " + u for u in unlisted))


if __name__ == "__main__":
    main()
