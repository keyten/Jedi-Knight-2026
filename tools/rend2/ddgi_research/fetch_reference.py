#!/usr/bin/env python3
"""Verify or explicitly download the pinned research sources into ignored build/.
Never installs RTXGI, modifies build lists or vendors third-party source.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.request

HERE=Path(__file__).resolve().parent


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,default=Path('build/ddgi-research/reference'))
    p.add_argument('--download',action='store_true',help='Explicitly permit HTTPS reads of source files missing or failing verification')
    args=p.parse_args(); manifest=json.loads((HERE/'sources.json').read_text()); root=args.output.resolve()
    if len(manifest['files'])>32: raise ValueError('Reference file-count budget')
    for entry in manifest['files']:
        path=(root/entry['path']).resolve()
        if not path.is_relative_to(root): raise ValueError('Invalid reference path')
        data=path.read_bytes() if path.exists() else b''
        if hashlib.sha256(data).hexdigest()!=entry['sha256']:
            if not args.download: raise ValueError('Missing/mismatched reference: '+entry['path'])
            request=urllib.request.Request(entry['url'],headers={'User-Agent':'OpenJK-lighting-research'})
            with urllib.request.urlopen(request,timeout=45) as response: data=response.read(4*1024*1024+1)
            if len(data)>4*1024*1024 or hashlib.sha256(data).hexdigest()!=entry['sha256']:
                raise ValueError('Source size/hash mismatch: '+entry['path'])
            path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(data)
    print('PASS:',len(manifest['files']),'pinned source/doc files, commit',manifest['commit'])


if __name__=='__main__': main()
