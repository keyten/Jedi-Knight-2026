import json
import re
import textwrap
import zipfile

from presentation import BOUNDS, STRING_MODES, enum_for, help_for, label_for, performance


def build_overlay(assets, here, root, item, quote, validate):
    source = (root / 'code/rd-rend2/tr_init.cpp').read_text(encoding='utf8')
    controls = json.loads((here / 'settings.json').read_text())
    controls = [c for c in controls if c['name'] != 'r_volumetricFogReset']
    regs = {m[1]: m[2] for m in re.finditer(r'(?:ri_Cvar_Get_NoComm|ri\.Cvar_Get)\s*\(\s*"(r_\w+)"\s*,\s*"[^"]*"\s*,\s*([^;]+)', source)}
    for c in controls:
        assert c['name'] in regs, 'Renderer setting no longer exists: ' + c['name']
        c['restart'] |= 'CVAR_LATCH' in regs[c['name']]
        # Several runtime toggles require buffers allocated at renderer startup.
        c['restart'] |= c['name'] in ('r_aoMode','r_contactShadows','r_autoPBRRoughness')
        c['label'] = label_for(c['name'])
        c['performance'] = performance(c['section'])
        c['enum'] = enum_for(c)
        if not c['enum'] and c['name'] != 'r_colorGradingLUT':
            c['range'] = c['range'] or BOUNDS.get(c['name'])
            assert c['range'], 'Missing reviewed slider bounds: ' + c['name']
    with zipfile.ZipFile(assets) as z:
        stock = z.read('ui/ingame.menu').decode('cp1252').replace('\r','')
        names = {n.lower():n for n in z.namelist()}
        stock_count = sum(len(re.findall(r'\bmenuDef\b',z.read(names[n.lower()]).decode('cp1252')))
                          for n in re.findall(r'"(ui/[^\"]+\.menu)"',z.read('ui/ingame.txt').decode('cp1252')))
    pages = []
    for cat in dict.fromkeys(c['category'] for c in controls):
        rows = [c for c in controls if c['category'] == cat]
        pages.extend((cat,rows[i:i+11]) for i in range(0,len(rows),11))
    assert stock_count + len(pages) + 2 <= 64
    category_first = {cat:next(i+1 for i,(title,_) in enumerate(pages) if cat==title)
                      for cat in dict.fromkeys(c['category'] for c in controls)}

    def text(name, label, y, scale=0.65):
        return item(name,label,252,y,376,'decoration','ITEM_TYPE_TEXT').replace('textscale 0.8',f'textscale {scale}')

    def frame(name, title, subtitle):
        return f'''menuDef {{ name {name} fullScreen 0 visible 0 rect 0 0 640 480
          style WINDOW_STYLE_EMPTY focusColor 1 0.75 0.3 1
          descX 440 descY 382 descScale 0.55 descColor 0.85 0.9 1 1 descAlignment ITEM_ALIGN_CENTER
          onOpen {{ setcvar cl_paused 0 ; uiScript renderSettingsBegin ; setfocus setting_0 ; }}
          onESC {{ uiScript closeingame ; }}
          itemDef {{ name panel rect 236 10 400 460 style WINDOW_STYLE_FILLED
            backcolor 0.025 0.04 0.07 0.82 visible 1 decoration }}
          {text('title',title,26,0.85)} {text('subtitle',subtitle,50)}\n'''

    def footer(name, previous=None, following=None):
        out = ''
        if previous is not None:
            out += item('previous','<',252,443,26,f'action {{ close all ; open render2026_{previous} ; }}')
        if following is not None:
            out += item('next','>',284,443,26,f'action {{ close all ; open render2026_{following} ; }}')
        out += item('contents','CONTENTS',320,443,90,'action { close all ; open render2026_0 ; }').replace('textscale 0.8','textscale 0.65')
        out += item('apply','APPLY / RESTART',422,443,150,
                    'cvarTest ui_r2026_restartPending enableCvar { "1" } action { uiScript closeingame ; exec "vid_restart" ; }').replace('textscale 0.8','textscale 0.65')
        out += item('close','CLOSE',580,443,52,'action { uiScript closeingame ; }').replace('textscale 0.8','textscale 0.65')
        return out + '}\n'

    generated = frame('render2026_0','RENDER 2026','Live rendering controls | game keeps running')
    for i,(cat,first) in enumerate(category_first.items()):
        generated += item(f'setting_{i}',cat,256,82+i*24,365,f'action {{ close all ; open render2026_{first} ; }}')
    generated += text('help1','Select a category. Drag sliders; use arrows for fine steps.',342)
    generated += text('help2','* marks a setting that needs a renderer restart.',365)
    generated += text('help3','Costs are estimates while the relevant effect is active.',403)
    generated += footer('render2026_0')

    costs = {'zero':0,'light':1,'medium':2,'heavy':3}
    for p,(cat,rows) in enumerate(pages,1):
        cost = max((c['performance'] for c in rows),key=costs.get)
        generated += frame(f'render2026_{p}',cat,f'Page {p}/{len(pages)} | Performance: {cost} | * restart')
        for i,c in enumerate(rows):
            label = c['label'] + (' *' if c['restart'] else '')
            assert len(label) <= 47, label
            desc = f"Performance: {c['performance']}"
            if c['range'] and not c['enum']: desc += f" | Range: {c['range'][0]:g} to {c['range'][1]:g}"
            desc += ' | Restart' if c['restart'] else ' | Live'
            group = 'render2026' + ('_restart' if c['restart'] else '')
            shadow = 'ui_r2026_' + c['name']
            extra = f'group {group} descText {quote(desc)} '
            if c['name'] == 'r_colorGradingLUT':
                extra += f'cvar {quote(shadow)} action {{ close all ; open render2026_luts ; }}'
                kind = 'ITEM_TYPE_BUTTON'
            elif c['enum']:
                field = 'cvarStrList' if c['name'] in STRING_MODES else 'cvarFloatList'
                extra += f'cvar {quote(shadow)} {field} {{ ' + ' '.join(
                    f'{quote(title)} {quote(value) if field=="cvarStrList" else value}' for title,value in c['enum']) + ' }'
                kind = 'ITEM_TYPE_MULTI'
            else:
                lo,hi,integer = c['range']
                if integer: extra = extra.replace(f'group {group}',f'group {group}_integer')
                extra += f'cvarFloat {quote(shadow)} {c["default"]} {lo:g} {hi:g}'
                kind = 'ITEM_TYPE_SLIDER'
            row = item(f'setting_{i}',label,252,82+i*24,378,extra,kind)
            row = row.replace('textscale 0.8','textscale 0.60')
            row = row.replace('textalign ITEM_ALIGN_LEFT textalignx 0','textalign ITEM_ALIGN_RIGHT textalignx 200')
            generated += row
        # Dependencies and useful cautions remain visible without hovering.
        for i,line in enumerate(help_for(cat)):
            generated += text(f'help{i}',line,399+i*17,0.6)
        generated += footer(f'render2026_{p}',(p-2)%len(pages)+1,p%len(pages)+1)

    generated += frame('render2026_luts','COLOR PALETTES','Performance: light | Choose a .cube LUT').replace(
        'uiScript renderSettingsBegin ;', 'uiScript renderSettingsBegin ; uiScript renderLutsRefresh ;')
    generated += '''itemDef { name setting_0 rect 252 82 374 264 type ITEM_TYPE_LISTBOX
      style WINDOW_STYLE_FILLED backcolor 0.04 0.07 0.1 0.65 visible 1 font 4 textscale 0.7
      forecolor 0.8 0.9 1 1 border 1 bordercolor 0.25 0.4 0.5 1
      feeder 112 elementtype 0 elementheight 22 elementwidth 354 columns 1 0 350 52 }\n'''
    generated += item('refresh','RESCAN LUT FOLDER',252,360,250,'action { uiScript renderLutsRefresh ; }')
    generated += text('luthelp','Reads base/luts/*.cube and LUTs in loaded PK3 files.',401)
    generated += text('luthelp2','Automatic uses map LUT; Neutral bypasses its palette.',418)
    generated += footer('render2026_luts')
    # Keep all original stock screens; add the overlay entry in the pause menu.
    last = stock.rfind('}')
    main_end = stock.rfind('}',0,last)
    stock = stock[:main_end] + item('render2026','RENDER 2026',40,395,180,
          'action { close all ; open render2026_0 ; }') + stock[main_end:]
    final = stock[:stock.rfind('}')] + generated + '\n}\n'
    validate(final)
    assert not re.search(r'type\s+ITEM_TYPE_(?:EDITFIELD|NUMERICFIELD)',generated)
    readme = textwrap.dedent('''\
        Jedi Academy 2026 live rendering overlay (SP)

        Install the supplied openjk_sp.x86_64.exe next to the existing game executable.
        Put zzzz_render2026_menu.pk3 in GameData/base (or your active mod). Restart.
        Open with Esc -> RENDER 2026, or console: uimenu render2026_0

        The game remains visible and running; mouse/keyboard control the overlay.
        CLOSE or Escape returns to gameplay. Live controls update immediately.
        * means a renderer restart is required. APPLY / RESTART is enabled only
        while a restart setting differs from its active value; it runs vid_restart.
        Latched controls show the requested value before restart.

        Drag numeric sliders; left/right arrows make fine adjustments. Integer
        controls stay integer. Every feature has an estimated GPU cost on hover;
        each page shows the highest cost among its features. Costs apply when the
        effect is active, not to moving a slider. Map/material requirements still apply.

        LUTs: put .cube files in base/luts, open Choose color palette, click a row.
        RESCAN LUT FOLDER refreshes the list without restarting the game. Automatic
        uses the map LUT; Neutral selects identity. Files inside loaded PK3s also work.
        Vector settings have named presets instead of free-form text.

        This version needs the supplied updated SP engine for LUT browsing, pending
        value display, accurate restart tracking and integer slider rounding.
        Use matching 2026 Rend2 renderer and game modules for the rendering features.
        This overrides ui/ingame.menu; other mods overriding it may conflict.
        Remove the PK3 to remove the overlay. Existing rendering choices are retained.
        ''')
    (here/'README.txt').write_text(readme,encoding='utf8')
    (here/'controls.json').write_text(json.dumps(controls,indent=2),encoding='utf8')
    payload = here/'pk3/ui'; payload.mkdir(parents=True,exist_ok=True)
    (payload/'ingame.menu').write_text(final,encoding='cp1252')
    out = here/'zzzz_render2026_menu.pk3'
    with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('ui/ingame.menu',final.encode('cp1252'))
        z.writestr('render2026-menu-readme.txt',readme)
    with zipfile.ZipFile(out) as z: assert z.testzip() is None
    print(f'{out}\n{len(controls)} reviewed controls; {len(pages)} settings pages + contents + LUT browser; {stock_count+len(pages)+2}/64 menus')
