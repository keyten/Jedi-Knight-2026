#!/usr/bin/env python3
"""Inventory official local JA archives with last-archive-wins precedence.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import zipfile
from pathlib import Path


def inventory(base, commit):
    maps, archives = {}, []
    for path in sorted(base.glob('assets[0-9].pk3')):
        with zipfile.ZipFile(path) as archive:
            members = sorted(n for n in archive.namelist()
                             if n.lower().startswith('maps/') and n.lower().endswith('.bsp'))
            archives.append(dict(path=str(path.resolve()), bytes=path.stat().st_size, maps=len(members)))
            for name in members:
                content = archive.read(name)
                mapname = name[5:-4].lower()
                maps[mapname] = dict(map=mapname, archive=str(path.resolve()), member=name,
                                     bytes=len(content), sha256=hashlib.sha256(content).hexdigest(),
                                     crc32=f'{archive.getinfo(name).CRC:08x}',
                                     mode='mp' if mapname.startswith('mp/') else 'sp')
    if not maps:
        raise RuntimeError('No stock BSPs found in official assets[0-9].pk3')
    return dict(schema='org.openjk.all-stock-inventory.v1', base_commit=commit,
                archives=archives, maps=[maps[k] for k in sorted(maps)])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--commit', default='27119e46e')
    args = parser.parse_args()
    result = inventory(args.base, args.commit)
    if args.output.exists() and json.loads(args.output.read_text()) != result:
        raise RuntimeError('Refusing to replace a different frozen inventory')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(len(result['maps']), 'unique stock maps')
