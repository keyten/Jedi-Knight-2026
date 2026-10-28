"""Trusted-worker Python I/O allowlist. Not a hostile native-code OS sandbox.

An optimizer process receives only inputs/, work/ and a trusted C++ adapter.
The audit hook denies ordinary Python attempts to read private/evaluator paths,
spawn arbitrary commands or load ctypes. A native malicious solver still needs
an external container/user sandbox, as documented by the existing TrainingView.
"""
import os
from pathlib import Path
import sys
import subprocess

def install(inputs,work,exe,code_files):
    inputs,work,exe=Path(inputs).resolve(),Path(work).resolve(),Path(exe).resolve()
    code={Path(x).resolve() for x in code_files}
    standard=Path(os.__file__).resolve().parent
    def within(p,root):return p==root or root in p.parents
    def path(value):
        if isinstance(value,int):return None
        return Path(os.fsdecode(value)).resolve()
    def audit(event,args):
        if event=='open':
            p=path(args[0])
            if p is None:return
            mode=args[1];flags=args[2] if len(args)>2 else 0
            writing=(isinstance(mode,str) and any(x in mode for x in 'wax+')) or (isinstance(flags,int) and flags&(os.O_WRONLY|os.O_RDWR|os.O_CREAT|os.O_TRUNC))
            if writing:
                if not within(p,work):raise PermissionError('Worker write outside work directory')
            elif not (within(p,inputs) or within(p,work) or p in code or within(p,standard)):
                raise PermissionError('Private/held-out/answer path denied')
        elif event in ('os.listdir','os.scandir'):
            p=path(args[0])
            if p is not None and not (within(p,inputs) or within(p,work) or within(p,standard)):raise PermissionError('Directory enumeration denied')
        elif event=='subprocess.Popen':
            argv=args[1];expected=[str(exe),'--server',str(inputs/'request.txt')]
            valid=argv==expected if isinstance(argv,(list,tuple)) else argv==subprocess.list2cmdline(expected)
            if (args[0] is not None and path(args[0])!=exe) or not valid:
                raise PermissionError('Only the trusted geometry-only C++ adapter may be launched')
        elif event in ('os.system','os.posix_spawn','ctypes.dlopen','ctypes.dlsym','socket.connect'):
            raise PermissionError('Worker native/process/network escape denied')
    sys.addaudithook(audit)
