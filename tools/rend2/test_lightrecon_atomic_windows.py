#!/usr/bin/env python3
"""Controlled reproduction of legacy ReplaceFile semantics, not a retry workaround."""
import argparse
import ctypes as C
from ctypes import wintypes as W
import json
from pathlib import Path
import threading


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, required=True)
    args = p.parse_args()
    folder = args.output.resolve()
    folder.mkdir(parents=True, exist_ok=False)
    k = C.WinDLL('kernel32', use_last_error=True)
    k.CreateFileW.argtypes = [W.LPCWSTR, W.DWORD, W.DWORD, C.c_void_p, W.DWORD, W.DWORD, W.HANDLE]
    k.CreateFileW.restype = W.HANDLE
    k.CloseHandle.argtypes = [W.HANDLE]
    k.ReplaceFileW.argtypes = [W.LPCWSTR, W.LPCWSTR, W.LPCWSTR, W.DWORD, C.c_void_p, C.c_void_p]
    k.SetFileAttributesW.argtypes = [W.LPCWSTR, W.DWORD]
    target = folder / 'controlled.bin'
    target.write_bytes(b'previous-valid-file')
    invalid = W.HANDLE(-1).value
    def reader():
        return k.CreateFileW(str(target), 0x80000000, 1 | 4, None, 3, 0x80, None)
    def replace(index):
        tmp = folder / f'temporary-{index}.bin'
        with tmp.open('xb') as f:
            f.write(b'next-file-' + str(index).encode())
            f.flush()
        ok = k.ReplaceFileW(str(target), str(tmp), None, 0, None, None)
        error = 0 if ok else C.get_last_error()
        if tmp.exists():
            tmp.unlink()
        return error
    result = {}
    for name, retained, concurrent in [('no-reader',False,False),('retained-reader',True,False),('concurrent-readers',True,True)]:
        handles = []
        errors = {}
        opened = reader() if retained else invalid
        stop = threading.Event()
        def read_loop():
            while not stop.is_set():
                h = reader()
                if h != invalid:
                    handles.append(h)
                    if len(handles) > 32:
                        k.CloseHandle(handles.pop(0))
        thread = threading.Thread(target=read_loop) if concurrent else None
        if thread:
            thread.start()
        try:
            for i in range(500):
                e = replace(f'{name}-{i}')
                errors[str(e)] = errors.get(str(e),0)+1
        finally:
            stop.set()
            if thread:
                thread.join()
            for h in handles:
                k.CloseHandle(h)
            if opened != invalid:
                k.CloseHandle(opened)
        result[name] = errors
    errors = {}
    lock = threading.Lock()
    barrier = threading.Barrier(4)
    def writer(index):
        barrier.wait()
        for j in range(125):
            e = replace(f'writer-{index}-{j}')
            with lock:
                errors[str(e)] = errors.get(str(e),0)+1
    writers = [threading.Thread(target=writer, args=(i,)) for i in range(4)]
    for t in writers:
        t.start()
    for t in writers:
        t.join()
    result['concurrent-writers'] = errors
    before = target.read_bytes()
    if not k.SetFileAttributesW(str(target),1):
        raise C.WinError(C.get_last_error())
    try:
        result['read-only-destination'] = dict(error=replace('read-only'), previous_preserved=target.read_bytes()==before)
    finally:
        k.SetFileAttributesW(str(target),0x80)
    (folder / 'result.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(result,indent=2))


if __name__ == '__main__':
    main()
