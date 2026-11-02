# Jedi Academy 2026 — website

Static page for the project, served by GitHub Pages from the root of this `gh-pages` branch.

## Feature cards

The graphics and Force power cards and their modal windows are generated. The graphics part follows
the project's `docs/rendering-features/*.md`: one block per page, one card per `##` section, and the
modal shows the section as written in the docs.

- `tools/cards.py` — the order of pages and sections, a one-line summary and the cvar for each card,
  sections left out, screenshots and videos (`MEDIA`), and the Force power cards.
- `tools/build.py` — reads the docs from the project checkout, writes `assets/features.js` and fills
  the cards into `index.html` between the `<!-- cards:... -->` markers. Links between features open
  the matching modal; other doc links go to GitHub.

After the docs change, rebuild:

```bash
python tools/build.py ../OpenJK-rend2/docs
```

It stops if a section listed in `cards.py` no longer exists, and lists doc sections that have no card yet.

Screenshots and videos go to `assets/media/` and are listed in `MEDIA` by card id
(see the top of `tools/cards.py`). Cards without media show empty slots.

A card can be linked directly: `index.html#f/reflections/screen-space-reflections-ssr`.

## Local preview

```bash
python -m http.server 8000
```
