#!/usr/bin/env python3
"""SP main-world lifecycle smoke test using the existing stock-only runtime harness.

No movement, camera/FX manipulation or image assertions: only map registration,
pause, policy/use changes, vid_restart and disconnect. Missing inputs fail rather
than claiming a runtime pass. Actual cache hits/build completion are CPU-tested
with synthetic assets; production complete input hashing is still unavailable.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import struct
import time
import zipfile
from test_lightrecon_legacy_runtime import ROOT, CONSUMERS, prepare_runtime


def commands():
    def info(label):
        return [f'echo RECON_LIFECYCLE_{label}_BEGIN', 'r_lightReconInfo images',
                f'echo RECON_LIFECYCLE_{label}_END']
    result = ['wait 30'] + info('UNLOADED')
    result += ['r_lightReconUse 1'] + [f'{c} 1' for c in CONSUMERS]
    # Client world registration follows server map setup asynchronously across
    # frames. Match the existing harness' settled registration interval before
    # querying or issuing another map/restart command; no player actions occur.
    result += ['r_lightReconBuild 0', 'map hoth3', 'wait 180', 'uimenu recon_freeze'] + info('DISABLED')
    result += ['r_lightReconBuild 3'] + info('POLICY_UNAPPLIED')
    result += ['map hoth2', 'wait 180', 'uimenu recon_freeze'] + info('FORCE')
    result += ['r_lightReconBuild 1', 'map hoth3', 'wait 180', 'uimenu recon_freeze'] + info('CACHEONLY')
    result += ['r_lightReconBuild 2', 'vid_restart', 'wait 180', 'uimenu recon_freeze'] + info('RESTART')
    result += ['disconnect', 'wait 30'] + info('DISCONNECTED')
    return result + ['echo RECON_LIFECYCLE_DONE', 'quit']


def parse(text):
    states = {}
    for label in ('UNLOADED', 'DISABLED', 'POLICY_UNAPPLIED', 'FORCE', 'CACHEONLY', 'RESTART', 'DISCONNECTED'):
        block = re.search(f'RECON_LIFECYCLE_{label}_BEGIN(.*?)RECON_LIFECYCLE_{label}_END', text, re.S)
        if not block:
            raise AssertionError('Missing lifecycle record ' + label)
        section = block[1]
        match = re.search(r'lifecycle=(\w+) world=(\S+) ticket=(\d+) generation=(\d+) identity=(\S+)', section)
        cache = re.search(r'cache=(\w+) key-complete=(\d+).*metadata-estimate=(\d+) temporary=([^\r\n]+)', section)
        rows = re.findall(r'\b(r_lightRecon\w+) requested=(\d+) effective=(\d+) capability=(\w+) feature=(\d+)', section)
        if not match or not cache or len(rows) != len(CONSUMERS) or {r[0] for r in rows} != set(CONSUMERS):
            raise AssertionError('Truncated/invalid lifecycle diagnostic ' + label)
        if any(r[2] != '0' or r[4] != '0' for r in rows):
            raise AssertionError('Consumer activated during lifecycle test')
        master = re.search(r'lightRecon: [^\n]*use=(\d+)', section)
        if not master or master[1] != ('0' if label == 'UNLOADED' else '1') or any(r[1] != '1' for r in rows):
            raise AssertionError('Master/consumer requests were not actually exercised')
        if (cache[2], cache[3]) != ('0', '0') or cache[4] != '0 bytes':
            raise AssertionError('Unexpected resident data or complete fingerprint')
        if re.search(r'session=[1-9]|steps=[1-9]|cache-called=[1-9]', section):
            raise AssertionError('Unexpected expensive/loading work with incomplete source hashing')
        states[label] = dict(state=match[1], world=match[2], ticket=int(match[3]), generation=int(match[4]),
                             identity=match[5], cache=cache[1])
        observation = re.search(r'observations=(\w+) images=(\d+) missing=(\d+) RGB8=(\d+) RGBA8=(\d+) HDR32F=(\d+) deluxe=(\d+) pixels=(\d+) bytes=(\d+) accounted=(\d+)', section)
        if not observation:
            raise AssertionError('Missing original-image diagnostic ' + label)
        wanted = 'Captured' if label in ('FORCE', 'RESTART') else 'NotRequested'
        if observation[1] != wanted or (wanted == 'NotRequested' and any(int(v) for v in observation.groups()[1:])):
            raise AssertionError('Unexpected observation retention ' + label + ': ' + observation[0])
        states[label]['observations'] = dict(zip(('state', 'images', 'missing', 'rgb8', 'rgba8', 'hdr', 'deluxe', 'pixels', 'bytes', 'accounted'), observation.groups()))
        grid = re.search(r'grid-observations=(\w+) cells=(\d+) records=(\d+) indices=(\d+) HDR=(\d+) bytes=(\d+) missing=(\d+) invalid-indices=(\d+) no-style=(\d+) style-gaps=(\d+) nonfinite-HDR=(\d+) negative-HDR=(\d+) issues=(\d+)', section)
        if not grid or grid[1] != wanted or (wanted == 'NotRequested' and any(int(v) for v in grid.groups()[1:])):
            raise AssertionError('Unexpected original grid retention ' + label)
        states[label]['lightgrid'] = dict(zip(('state', 'cells', 'records', 'indices', 'hdr_cells', 'bytes', 'missing', 'invalid_indices', 'no_style', 'style_gaps', 'nonfinite_hdr', 'negative_hdr', 'issues'), grid.groups()))
        textures = re.search(r'lightmap-textures=(\d+)', section)
        if not textures:
            raise AssertionError('Missing atlas/texture count ' + label)
        states[label]['lightmap_textures'] = int(textures[1])
        applied = re.search(r'build applied=(\d+) result=(\w+) cache-called=(\d+) build-called=(\d+)', section)
        policies = dict(DISABLED=('0', 'Disabled', '0', '0'), POLICY_UNAPPLIED=('0', 'Disabled', '0', '0'),
                        FORCE=('3', 'Unavailable', '0', '1'), CACHEONLY=('1', 'Unavailable', '0', '0'),
                        RESTART=('2', 'Unavailable', '0', '1'))
        if label in policies:
            if not applied or applied.groups() != policies[label]:
                raise AssertionError('Incorrect applied build/load policy ' + label)
            states[label]['applied'] = applied.groups()
        elif applied:
            raise AssertionError('Unloaded context claimed an applied map policy')
    expected = dict(UNLOADED=('Unloaded', '<none>'), DISABLED=('Disabled', 'maps/hoth3.bsp'),
                    POLICY_UNAPPLIED=('Disabled', 'maps/hoth3.bsp'), FORCE=('BuildRequired', 'maps/hoth2.bsp'),
                    CACHEONLY=('Unavailable', 'maps/hoth3.bsp'), RESTART=('BuildRequired', 'maps/hoth3.bsp'),
                    DISCONNECTED=('Unloaded', '<none>'))
    for label, (state, world) in expected.items():
        if (states[label]['state'], states[label]['world']) != (state, world):
            raise AssertionError(f'{label}: {states[label]} expected {state}/{world}')
    if states['DISABLED']['ticket'] != states['POLICY_UNAPPLIED']['ticket']:
        raise AssertionError('Changing policy mutated current context')
    if len({states[k]['ticket'] for k in ('DISABLED', 'FORCE', 'CACHEONLY')}) != 3:
        raise AssertionError('World load reused a ticket')
    return states


def lightmap_fixtures(home):
    """Tiny original files, only in this invocation's private mod directory."""
    folder = home / 'maps/hoth2'
    folder.mkdir(parents=True)
    (folder / 'lm_0000.hdr').write_bytes(b'#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n' + bytes((128, 64, 32, 129, 16, 32, 64, 136)) * 2)
    # Top-origin RGBA8 TGA, loader expands B,G,R,A to R,G,B,A.
    pixels = bytes((0, 1, 255, 3, 128, 4, 9, 255))
    header = struct.pack('<BBBHHBHHHHBB', 0, 0, 2, 0, 0, 0, 0, 0, 2, 1, 32, 0x28)
    bgra = b''.join(bytes((pixels[i+2], pixels[i+1], pixels[i], pixels[i+3])) for i in (0, 4))
    for name in ('lm_0002.tga', 'dm_0000.tga'):
        (folder / name).write_bytes(header + bgra)
    return dict(hdr=struct.pack('<12f', *((1, .5, .25, 16, 32, 64) * 2)), rgba=pixels)


def validate_lightmaps(text, installation, fixtures=None):
    """Compare exact original samples, independently of GPU atlas/deluxe repair."""
    result = {}
    for label, map_name in (('FORCE', 'hoth2'), ('RESTART', 'hoth3')):
        section = re.search(f'RECON_LIFECYCLE_{label}_BEGIN(.*?)RECON_LIFECYCLE_{label}_END', text, re.S)[1]
        path = f'maps/{map_name}.bsp'
        bsp = None
        for archive in sorted((installation / 'base').glob('assets[0-3].pk3'), reverse=True):
            with zipfile.ZipFile(archive) as pk3:
                if path in pk3.namelist():
                    bsp = pk3.read(path)
                    break
        if bsp is None:
            raise AssertionError('Missing stock source BSP ' + path)
        identity = re.search(r'observation-map=(\S+) sha256=([0-9a-f]{64}) source-bytes=(\d+)', section)
        if not identity or identity.groups() != (path, hashlib.sha256(bsp).hexdigest(), str(len(bsp))):
            raise AssertionError('Incorrect original BSP fingerprint ' + label)
        # JA dheader: ident/version + 18 (offset,length) lump descriptors;
        # LIGHTMAPS is lump 14, each internal image is 128*128*RGB8.
        offset, length = struct.unpack_from('<II', bsp, 8 + 14 * 8)
        stride = 128 * 128 * 3
        if length % stride or offset + length > len(bsp):
            raise AssertionError('Invalid stock lightmap lump')
        rows = re.findall(r'lm-observation index=(\d+) role=(\w+) encoding=([\w-]+) size=(\d+)x(\d+)x(\d+) missing=(\d) offset=(\d+) hash=([0-9a-f]{64}) source=(\S+)', section)
        embedded = [r for r in rows if r[2] == 'BSP-RGB8']
        if len(embedded) != length // stride:
            raise AssertionError('Lost embedded lightmap images')
        hashes = []
        for index, row in enumerate(embedded):
            expected = hashlib.sha256(bsp[offset+index*stride:offset+(index+1)*stride]).hexdigest()
            if (int(row[0]), row[3:9], row[9]) != (index, ('128', '128', '3', '0', str(offset+index*stride), expected), path + '#LUMP_LIGHTMAPS'):
                raise AssertionError('Changed original RGB8 samples/identity: ' + str(row))
            hashes.append(expected)
        if fixtures and label == 'FORCE':
            for source, encoding, logical, role, payload in (
                    ('maps/hoth2/lm_0000.hdr', 'external-RGB32F', '0', 'lightmap', fixtures['hdr']),
                    ('maps/hoth2/lm_0002.tga', 'external-RGBA8', '2', 'lightmap', fixtures['rgba']),
                    ('maps/hoth2/dm_0000.tga', 'external-RGBA8', '0', 'deluxe', fixtures['rgba'])):
                matching = [r for r in rows if r[9] == source]
                is_hdr = encoding.endswith('RGB32F')
                expected = (logical, role, encoding, '2', '2' if is_hdr else '1', '3' if is_hdr else '4', '0', '0', hashlib.sha256(payload).hexdigest(), source)
                if matching != [expected]:
                    raise AssertionError('External fixture not preserved: ' + str(matching))
        elif any(r[6] == '0' and r[2] != 'BSP-RGB8' for r in rows):
            raise AssertionError('Unexpected stock external image')
        result[label] = dict(map=path, sha256=identity[2], source_bytes=len(bsp), internal_images=len(embedded),
                             internal_pixel_bytes=length, embedded_hashes=hashes, fixture_images=3 if fixtures and label == 'FORCE' else 0)
    return result


def stock_bsp(installation, map_name):
    path = f'maps/{map_name}.bsp'
    for archive in sorted((installation / 'base').glob('assets[0-3].pk3'), reverse=True):
        with zipfile.ZipFile(archive) as pk3:
            if path in pk3.namelist():
                return pk3.read(path)
    raise AssertionError('Missing stock BSP ' + path)


def grid_source(installation, map_name):
    bsp = stock_bsp(installation, map_name)
    def lump(index):
        offset, size = struct.unpack_from('<II', bsp, 8 + index * 8)
        if offset + size > len(bsp):
            raise AssertionError('Invalid original grid/model lump')
        return bsp[offset:offset+size]
    worldspawn = lump(0).split(b'}', 1)[0].decode('ascii', errors='replace')
    match = re.search(r'"gridsize"\s+"([^"]+)"', worldspawn)
    spacing = tuple(map(float, match[1].split())) if match else (64., 64., 128.)
    model = struct.unpack_from('<6f', lump(7))
    origin = tuple(s * math.ceil(model[a] / s) for a, s in enumerate(spacing))
    end = tuple(s * math.floor(model[a+3] / s) for a, s in enumerate(spacing))
    bounds = tuple(round((e-o)/s) + 1 for e, o, s in zip(end, origin, spacing))
    return bsp, lump(15), lump(17), bounds, origin, spacing


def lightgrid_fixture(home, installation):
    _, _, _, bounds, _, _ = grid_source(installation, 'hoth2')
    cells = math.prod(bounds)
    data = bytearray(cells * 24)
    for i in range(cells):
        struct.pack_into('<6f', data, i*24, i*.001, .5, 2., 4., 8., 16.)
    path = home / 'maps/hoth2/lightgrid.raw'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)
    return bytes(data)


def validate_lightgrid(text, installation, fixture=None):
    result = {}
    for label, name in (('FORCE', 'hoth2'), ('RESTART', 'hoth3')):
        section = re.search(f'RECON_LIFECYCLE_{label}_BEGIN(.*?)RECON_LIFECYCLE_{label}_END', text, re.S)[1]
        bsp, records, indices, bounds, origin, spacing = grid_source(installation, name)
        expected_hdr = fixture if label == 'FORCE' and fixture is not None else b''
        identity = re.search(r'grid-map=(\S+) sha256=([0-9a-f]{64}) records-sha256=([0-9a-f]{64}) indices-sha256=([0-9a-f]{64}) HDR-sha256=([0-9a-f]{64})', section)
        expected = (f'maps/{name}.bsp', *(hashlib.sha256(b).hexdigest() for b in (bsp, records, indices, expected_hdr)))
        if not identity or identity.groups() != expected:
            raise AssertionError('Original grid bytes/indirection/HDR changed ' + label)
        layout = re.search(r'grid-layout bounds=([\d,]+) origin=([\d.,eE+-]+) spacing=([\d.,eE+-]+) HDR-state=(\d+)', section)
        if not layout or tuple(map(int, layout[1].split(','))) != bounds or tuple(map(float, layout[2].split(','))) != origin or tuple(map(float, layout[3].split(','))) != spacing or int(layout[4]) != (2 if expected_hdr else 1):
            raise AssertionError('Grid coordinate metadata changed ' + label)
        referenced = struct.unpack('<' + 'H'*(len(indices)//2), indices)
        invalid = sum(n >= len(records)//30 for n in referenced)
        no_style = sum(all(b == 255 for b in records[n*30+24:n*30+28]) for n in referenced if n < len(records)//30)
        gaps = sum(any(b != 255 and 255 in records[n*30+24:n*30+24+j] for j, b in enumerate(records[n*30+24:n*30+28])) for n in referenced if n < len(records)//30)
        counts = re.search(r'grid-observations=Captured cells=(\d+) records=(\d+) indices=(\d+) HDR=(\d+) bytes=(\d+) missing=(\d+) invalid-indices=(\d+) no-style=(\d+) style-gaps=(\d+) nonfinite-HDR=(\d+) negative-HDR=(\d+) issues=(\d+)', section)
        expected_counts = (math.prod(bounds), len(records)//30, len(indices)//2, len(expected_hdr)//24,
                           len(records)+len(indices)+len(expected_hdr), invalid, invalid, no_style, gaps, 0, 0, 64 if gaps else 0)
        if not counts or tuple(map(int, counts.groups())) != expected_counts:
            raise AssertionError('Grid counts/quality lost ' + label)
        result[label] = dict(map=expected[0], bounds=bounds, origin=origin, spacing=spacing, cells=math.prod(bounds),
                             records=len(records)//30, indices=len(indices)//2, bytes=expected_counts[4],
                             no_style=no_style, style_gaps=gaps, invalid_indices=invalid, geometry='Unknown for every cell',
                             records_sha256=expected[2], indices_sha256=expected[3], hdr_sha256=expected[4], hdr_cells=len(expected_hdr)//24)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--installation', type=Path, required=True)
    parser.add_argument('--binaries', type=Path, required=True)
    parser.add_argument('--renderer', type=Path, required=True)
    parser.add_argument('--output', type=Path, default=ROOT / 'build/lightrecon-lifecycle-runtime')
    parser.add_argument('--label', default='smoke')
    parser.add_argument('--timeout', type=int, default=600)
    fixtures_group = parser.add_mutually_exclusive_group()
    fixtures_group.add_argument('--lightmap-fixtures', action='store_true', help='Private HDR/TGA override tests (HDR renderer profile), including observation-only deluxe with rendering disabled')
    fixtures_group.add_argument('--lightgrid-fixture', action='store_true', help='Private dense HDR grid with r_hdr=0; observation capture must remain independent')
    parser.add_argument('--lightgrid-hdr', type=int, choices=(0, 1), default=0, help='r_hdr for the private grid fixture: 0 tests observation-only I/O, 1 reuses the renderer read')
    args = parser.parse_args()
    if os.name != 'nt' or not re.fullmatch(r'[A-Za-z0-9_-]+', args.label):
        parser.error('Windows and a simple unique label are required')
    runtime = args.output.resolve() / ('lifecycle-' + args.label)
    config, archives = prepare_runtime(args, runtime, 'legacy', 0)
    home = runtime / 'home/OpenJK'
    fixtures = lightmap_fixtures(home) if args.lightmap_fixtures else None
    grid_fixture = lightgrid_fixture(home, args.installation) if args.lightgrid_fixture else None
    # Force this off before renderer init: observation capture must not enable
    # deluxe rendering even when a dm_XXXX file is available to the solver.
    with (home / 'openjk_sp.cfg').open('a') as cfg:
        config.update(r_deluxeMapping=0, r_floatLightmap=1 if fixtures else 0)
        cfg.write(f'set r_deluxeMapping "0"\nset r_floatLightmap "{config["r_floatLightmap"]}"\n')
        if grid_fixture is not None:
            config['r_hdr'] = args.lightgrid_hdr
            cfg.write(f'set r_hdr "{args.lightgrid_hdr}"\n')
    (home / 'autoexec_sp.cfg').write_text('\n'.join(commands()) + '\n', encoding='ascii')
    command = [str(runtime / 'openjk_sp.x86_64.exe')]
    for key in ('fs_basepath', 'fs_homepath', 'fs_game'):
        command += ['+set', key, config[key]]
    startup = subprocess.STARTUPINFO()
    startup.dwFlags |= subprocess.STARTF_USESHOWWINDOW
    startup.wShowWindow = 0
    with (runtime / 'launch.log').open('w') as log:
        process = subprocess.Popen(command, cwd=runtime, startupinfo=startup, stdout=log, stderr=subprocess.STDOUT)
        deadline = time.monotonic() + args.timeout
        while process.poll() is None and time.monotonic() < deadline:
            time.sleep(.5)
        if process.poll() is None:
            process.terminate() # only this invocation's child
            process.wait(timeout=20)
            raise TimeoutError(runtime)
    text = (home / 'qconsole.log').read_text(errors='replace')
    text = re.sub(r'(?m)^\d{4}-\d\d-\d\d \d\d:\d\d:\d\d ', '', text)
    text = re.sub(r'\^[0-9]', '', text)
    if process.returncode or 'RECON_LIFECYCLE_DONE' not in text:
        raise AssertionError('Incomplete actual-SP lifecycle run: ' + str(runtime))
    if re.search(r'GL_INVALID|GL_OUT_OF_MEMORY|GLSL shader (?:compile|link) error|\bERROR:', text):
        raise AssertionError('Renderer error during map transitions')
    if text.count('RE_Shutdown(') < 3:
        raise AssertionError('Expected renderer teardown/restart did not occur')
    cache_files = list((runtime / 'home/rend2/lightrecon').rglob('*.jklr'))
    if cache_files:
        raise AssertionError('Incomplete production keys wrote canonical cache files')
    source_files = ('shared/rd-rend2/lightrecon/lifecycle.h', 'shared/rd-rend2/lightrecon/lifecycle.cpp',
                    'shared/rd-rend2/lightrecon/lightmaps.h', 'shared/rd-rend2/lightrecon/lightmaps.cpp',
                    'shared/rd-rend2/lightrecon/lightgrid.h', 'shared/rd-rend2/lightrecon/lightgrid.cpp',
                    'shared/rd-rend2/lightrecon/input.h', 'code/rd-common/tr_image_load.cpp', 'codemp/rd-common/tr_image_load.cpp',
                    'shared/rd-rend2/tr_lightrecon.h', 'shared/rd-rend2/tr_lightrecon.cpp',
                    'shared/rd-rend2/tr_lightrecon_api.cpp', 'shared/rd-rend2/tr_lightrecon_manager.h',
                    'shared/rd-rend2/tr_bsp.cpp', 'shared/rd-rend2/tr_bakedlighting.cpp',
                    'code/rd-rend2/tr_init.cpp', 'codemp/rd-rend2/tr_init.cpp')
    states = parse(text)
    lightmaps = validate_lightmaps(text, args.installation, fixtures)
    lightgrid = validate_lightgrid(text, args.installation, grid_fixture)
    if states['FORCE']['lightmap_textures'] != (lightmaps['FORCE']['internal_images'] if fixtures else 1):
        raise AssertionError('Expected external textures/internal atlas path was not exercised')
    result = dict(passed=True, states=states, lightmaps=lightmaps, lightgrid=lightgrid, settings=config, archives=archives, command=command,
                  renderer_sha256=hashlib.sha256(args.renderer.read_bytes()).hexdigest(),
                  runtime=str(runtime), source_base_revision=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  source_files_sha256={p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in source_files},
                  driver=re.findall(r'GL_RENDERER:[^\r\n]*', text),
                  gameplay_actions=False, cache_hit_runtime_tested=False, canonical_cache_files=[])
    (runtime / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
