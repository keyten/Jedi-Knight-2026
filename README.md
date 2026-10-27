# Jedi Academy 2026 — website

Static page for the project, served by GitHub Pages from the root of this `gh-pages` branch.

## Feature cards

The graphics and Force power cards and their modal windows are generated:

- `tools/cards.py` — card text, the technical paragraph, the media list, and which sections of
  `docs/rendering-features.md` each card shows.
- `tools/build.py` — reads `docs/rendering-features.md` from the project checkout, writes
  `assets/features.js` and fills the cards into `index.html` between the `<!-- cards:... -->` markers.

After changing either of them or the docs, rebuild:

```bash
python tools/build.py ../OpenJK-rend2/docs/rendering-features.md
```

Screenshots and videos go to `assets/media/<feature id>/` and are listed in the card's `media` field
(see the top of `tools/cards.py`). Cards without media show empty slots.

A card can be linked directly: `index.html#f/ssr`.

## Local preview

```bash
python -m http.server 8000
```
