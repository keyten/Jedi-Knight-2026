#!/usr/bin/env python3
"""Build the SP rendering overlay from stock assets and reviewed metadata."""
import argparse
import re
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def quote(s):
    return '"' + s.replace('"', "'").replace('\n', ' ') + '"'


def item(name, text, x, y, w, extra='', kind='ITEM_TYPE_BUTTON'):
    return f'''itemDef {{ name {name} rect {x} {y} {w} 19
      type {kind} style WINDOW_STYLE_EMPTY visible 1 font 4 textscale 0.8
      textalign ITEM_ALIGN_LEFT textalignx 0 textaligny 0
      forecolor 0.72 0.82 1 1 text {quote(text)} {extra} }}\n'''


def validate_sp_menu(text):
    """Check SP symbolic enums and type-dependent fields before packing."""
    parser = (ROOT / 'code/ui/ui_shared.cpp').read_text(encoding='utf-8')
    symbols = set(re.findall(r'"((?:ITEM_TYPE_|ITEM_ALIGN_|WINDOW_STYLE_)\w+)"', parser))
    clean = re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)
    tokens = re.findall(r'"[^"\n]*"|[{}]|[^\s{}]+', clean)
    prefixes = {'type': 'ITEM_TYPE_', 'style': 'WINDOW_STYLE_',
                'textalign': 'ITEM_ALIGN_', 'descAlignment': 'ITEM_ALIGN_'}
    stack = []
    for i, token in enumerate(tokens):
        if token == '{':
            stack.append({'item': i > 0 and tokens[i-1] == 'itemDef', 'type': 'ITEM_TYPE_TEXT'})
        elif token == '}':
            assert stack, 'Unexpected closing brace'
            stack.pop()
        elif token in prefixes:
            value = tokens[i+1]
            assert value in symbols and value.startswith(prefixes[token]), f'Invalid SP {token}: {value}'
            if token == 'type' and stack and stack[-1]['item']: stack[-1]['type'] = value
        elif token in ('cvarFloatList', 'cvarStrList', 'cvarFloat', 'maxChars', 'maxPaintChars'):
            assert stack and stack[-1]['item'], f'{token} outside itemDef'
            allowed = ('ITEM_TYPE_MULTI',) if token.endswith('List') else ('ITEM_TYPE_SLIDER', 'ITEM_TYPE_EDITFIELD', 'ITEM_TYPE_NUMERICFIELD') if token == 'cvarFloat' else ('ITEM_TYPE_EDITFIELD', 'ITEM_TYPE_NUMERICFIELD')
            assert stack[-1]['type'] in allowed, f'{token} incompatible with {stack[-1]["type"]}'
    assert not stack, 'Unclosed menu block'


def build(assets, reference=None):
    from overlay import build_overlay
    build_overlay(assets, HERE, ROOT, item, quote, validate_sp_menu)


def package_release(engine):
    output = HERE / 'render2026_overlay_sp.zip'
    install = '''Installing the Jedi Academy 2026 overlay (single player)

Extract the archive into GameData: openjk_sp.x86_64.exe belongs alongside
the game executable, and base/zzzz_render2026_menu.pk3 belongs in base.
For an active mod, put the PK3 in that mod's folder instead.
Use the matching Jedi Academy 2026 Rend2 renderer.

Open: Esc -> RENDER 2026, or console: uimenu render2026_0
Navigate: Contents -> category. Close: Close / Escape.
Changes without an asterisk take effect immediately. Apply / Restart becomes
active when settings requiring a restart change, and runs vid_restart.

LUTs: put .cube files in base/luts and open Choose color palette.
Rescan LUT Folder refreshes the list. Vector parameters use named presets
selected through switches; the overlay has no free-form numeric input.
'''
    with zipfile.ZipFile(output, 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.write(engine, 'openjk_sp.x86_64.exe')
        archive.write(HERE / 'zzzz_render2026_menu.pk3', 'base/zzzz_render2026_menu.pk3')
        archive.write(HERE / 'README.txt', 'README.txt')
        archive.write(ROOT / 'LICENSE.txt', 'LICENSE.txt')
        archive.writestr('INSTALL.txt', install.encode('utf8'))
    with zipfile.ZipFile(output) as archive:
        assert archive.testzip() is None
    print(output)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--assets', type=Path, required=True)
    parser.add_argument('--reference', type=Path, help='Legacy argument; reviewed settings.json is now used')
    parser.add_argument('--engine', type=Path, help='Include the rebuilt SP engine in an installable ZIP')
    args = parser.parse_args()
    build(args.assets, args.reference)
    if args.engine: package_release(args.engine)
