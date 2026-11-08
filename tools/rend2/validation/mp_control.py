"""Pure MP capture protocol/validation. No process, network or firewall operations."""
import re

CAMERAS = {'mp/duel4': (-2880, 2036, 50, 90)}
LABELS = ('AA0', 'AA1', 'A', 'B', 'REPEAT')


def menu_list(stock):
    # UI_LoadMenus stops at the stock list's closing }. Appending is ineffective.
    return 'loadmenu { "ui/recon-freeze.menu" }\n' + stock


def commands(name, consumers):
    x, y, z, yaw = CAMERAS[name]
    out = ['wait 30', f'devmap {name}', 'wait 180', 'team free', 'wait 45',
           'god', 'notarget', 'noclip', '-forward', '-back', '-moveleft', '-moveright',
           '-left', '-right', '-lookup', '-lookdown',
           f'setviewpos {x} {y} {z} {yaw}', 'wait 30',
           'ui_openmenu recon_freeze', 'wait 30']
    for label in LABELS:
        master, request = (1, 0) if label == 'B' else (0, 1)
        out += [f'r_lightReconUse {master}'] + [f'{c} {request}' for c in consumers]
        out += ['wait 12', f'echo X19_{label}_BEGIN', 'r_lightReconInfo', 'viewpos',
                'cl_paused', 'sv_paused', 'cg_fov', 'r_speeds 1', 'wait 3',
                f'screenshot_tga x19-{label.lower()}', 'wait 3', 'r_speeds 0',
                'glslusage all', f'echo X19_{label}_END', 'wait 3']
    return out + ['echo X19_MP_DONE', 'wait 5', 'quit']


def validate_sections(text, name, validate_consumers):
    """Nonflat menu screenshots alone cannot satisfy this world-scene control."""
    records = []
    for label in LABELS:
        matches = re.findall(f'X19_{label}_BEGIN(.*?)X19_{label}_END', text, re.S)
        if len(matches) != 1:
            raise ValueError('Missing/duplicate MP capture block: ' + label)
        section = matches[0]
        validate_consumers(section, 'B' if label == 'B' else 'A', 0)
        for cvar in ('cl_paused', 'sv_paused'):
            values = re.findall(r'\b' + cvar + r'\s*=\s*"([^"\s]+)', section)
            if values != ['1']:
                raise ValueError('MP scene is not frozen: ' + label + '/' + cvar)
        camera = re.findall(r'maps/' + re.escape(name) +
                            r'\.bsp\s+\((-?\d+) (-?\d+) (-?\d+)\)\s*:\s*(-?\d+)', section)
        if len(camera) != 1:
            raise ValueError('Missing/duplicate actual world camera: ' + label)
        fov = re.findall(r'\bcg_fov\s*=\s*"([^"\s]+)', section)
        if len(fov) != 1:
            raise ValueError('Missing actual field of view: ' + label)
        # These are live renderer counters from the capture window, not a
        # historical glslusage inventory or a screenshot's colour variance.
        draws = re.findall(r'(\d+)/(\d+)/(\d+) shaders/batches/surfs (\d+) leafs (\d+) verts', section)
        supported = [list(map(int, row)) for row in draws
                     if all(int(row[k]) > 0 for k in (2, 3, 4))]
        if not supported:
            raise ValueError('No visible world geometry in capture: ' + label)
        records.append(dict(label=label, camera=list(map(int, camera[0])),
                            fov=float(fov[0]), world_frames=len(supported)))
    if len({tuple(r['camera']) for r in records}) != 1 or len({r['fov'] for r in records}) != 1:
        raise ValueError('MP camera changed between captures')
    return records


def validate_images(shots, compare, tolerance):
    from PIL import Image
    import numpy as np
    for label in LABELS:
        pixels = np.asarray(Image.open(shots / ('x19-' + label.lower() + '.tga')).convert('RGB'))
        if pixels.std() < 2 or len(np.unique(pixels.reshape(-1, 3), axis=0)) < 64:
            raise ValueError('Empty/flat MP frame: ' + label)
    aa = compare(shots / 'x19-aa0.tga', shots / 'x19-aa1.tga', **tolerance)
    if not aa['passed']:
        raise ValueError('Invalid MP A/A control; do not attribute motion/menu drift to lighting')
    return dict(aa=aa,
                ab=compare(shots / 'x19-a.tga', shots / 'x19-b.tga', **tolerance),
                repeat=compare(shots / 'x19-a.tga', shots / 'x19-repeat.tga', **tolerance))
