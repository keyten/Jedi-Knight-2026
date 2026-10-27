#!/usr/bin/env python3
"""Read-only source research setup. No RBDOOM installation or build integration.
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
    p.add_argument('--output',type=Path,default=Path('build/rbdoom-research/reference'))
    p.add_argument('--download',action='store_true')
    args=p.parse_args(); root=args.output.resolve(); m=json.loads((HERE/'sources.json').read_text())
    if len(m['files'])>64: raise ValueError('Source file-count budget')
    def valid(data,e):
        git=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
        return len(data)==e['bytes'] and hashlib.sha256(data).hexdigest()==e['sha256'] and git==e['git_blob_sha1']
    for e in m['files']:
        path=(root/e['path']).resolve()
        if not path.is_relative_to(root): raise ValueError('Source path escapes output root')
        data=path.read_bytes() if path.is_file() else b''
        if not valid(data,e):
            if not args.download: raise ValueError('Missing/mismatched source '+e['path'])
            request=urllib.request.Request(e['url'],headers={'User-Agent':'OpenJK-lighting-research'})
            with urllib.request.urlopen(request,timeout=45) as stream: data=stream.read(4*1024*1024+1)
            if len(data)>4*1024*1024 or not valid(data,e): raise ValueError('Downloaded source hash/size mismatch')
            path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(data)
    print('PASS:',len(m['files']),'source/doc hashes and pinned Git blob hashes; commit',m['commit'])


if __name__=='__main__': main()
