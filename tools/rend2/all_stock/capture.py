#!/usr/bin/env python3
"""57-map official archive audit/capture. Licensed payloads remain private.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse,hashlib,json,os,shutil,subprocess,sys,time,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'tools/rend2'));sys.path.insert(0,str(ROOT/'tools/rend2/datasets'))
from test_lightrecon_legacy_runtime import settings
from measure import peak_memory

def write(p,x):p.write_text(json.dumps(x,indent=2,allow_nan=False)+'\n')
def sha(p):
 h=hashlib.sha256()
 with Path(p).open('rb') as f:
  for b in iter(lambda:f.read(1024*1024),b''):h.update(b)
 return h.hexdigest()
def reusable(private):
 available={}
 prior=Path('C:/Users/Keyten/AppData/Local/Temp/jk-lightrecon-x06-20261028')
 for name in ('captures.json','captures-prefix.json'):
  p=prior/name
  if p.exists():
   for k,v in json.loads(p.read_text()).items():
    if v.get('dataset') and Path(v['dataset']).exists():available[k]=v
 for root in (ROOT/'build/offline-stock-accepted',ROOT/'build/offline-stock-v2',ROOT/'build/x02/runtime',ROOT/'build/x02/final'):
  for p in root.glob('*.jkobs'):
   available.setdefault(p.stem.replace('_','/'),dict(dataset=str(p),derived_complete=False))
 # Explicit original regression captures; locate only beneath their known private runtime.
 for mapname in ('hoth3','hoth2','mp_ffa3'):
  for root in (ROOT/'build/x02/runtime',):
   for p in root.rglob(mapname+'*.jkobs'):
    available.setdefault(mapname.replace('mp_','mp/'),dict(dataset=str(p),derived_complete=False))
 return available

def capture(private,renderer):
 inv=json.loads((private/'inventory.json').read_text());reuse=reusable(private);result={}
 record=private/'captures.json'
 if record.exists():result=json.loads(record.read_text())
 for item in inv['maps']:
  name=item['map'];slug=name.replace('/','_')
  if name in result:continue
  old=reuse.get(name)
  if old and Path(old['dataset']).is_file():
   # Dataset BSP identity is independently checked by the CPU run, not guessed
   # from filename. Keep capture provenance and partial status.
   result[name]=dict(status='ReusedNeedsIdentityCheck',dataset=old['dataset'],capture=old)
   write(record,result);continue
  folder=private/'capture'/slug;home=folder/'home/OpenJK';home.mkdir(parents=True,exist_ok=True)
  for f in ('openjk_sp.x86_64.exe','rend2_glslc.exe'):shutil.copy2(ROOT/'build/x02/runtime/run'/f,folder/f)
  shutil.copy2(ROOT/'build/x02/runtime/run/home/OpenJK/jagamex86_64.dll',home/'jagamex86_64.dll')
  shutil.copy2(ROOT/'lib/SDL2/bin/x64/SDL2.dll',folder/'SDL2.dll');shutil.copy2(renderer,folder/'rdsp-rend2_x86_64.dll')
  c=settings('legacy',3);c.update(r_customwidth=320,r_customheight=240,com_maxfps=30,com_maxfpsUnfocused=30,com_maxfpsMinimized=30,r_picmip=4,r_dynamiclight=0,r_fastsky=1,r_shadows=0,r_lodbias=2,r_subdivisions=16,r_weather=0,r_flares=0)
  c.update(fs_basepath=str(ROOT/'build/lightrecon-evidence-runtime/final-source/stock-assets'),fs_homepath=str(folder/'home'),fs_game='OpenJK')
  (home/'openjk_sp.cfg').write_text(''.join(f'set {k} "{v}"\n' for k,v in c.items()),encoding='ascii')
  commands=['wait 15',f'devmap {name}','wait 30','cam_disable','exitview','r_lightReconEvidence',f'r_lightReconExport {slug}_all 0','echo ALL_STOCK_CAPTURE_DONE','wait 3','quit']
  (home/'autoexec_sp.cfg').write_text('\n'.join(commands)+'\n',encoding='ascii')
  args=[str(folder/'openjk_sp.x86_64.exe')]
  for k in ('fs_basepath','fs_homepath','fs_game'):args+=['+set',k,c[k]]
  start=time.monotonic();rss=0;limited=None
  startup=subprocess.STARTUPINFO();startup.dwFlags|=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
  with (folder/'launch.log').open('w') as f:
   child=subprocess.Popen(args,cwd=folder,startupinfo=startup,stdout=f,stderr=subprocess.STDOUT)
   while child.poll() is None:
    rss=max(rss,peak_memory(child) or 0)
    if time.monotonic()-start>180:limited='CaptureTimeBudget'
    if rss>2048*1024*1024:limited='CaptureMemoryBudget'
    if limited:child.terminate();child.wait(timeout=15);break
    time.sleep(.25)
   rss=max(rss,peak_memory(child) or 0)
  console=home/'qconsole.log';text=console.read_text(errors='replace') if console.exists() else ''
  files=list((folder/'home').rglob(slug+'_all.jkobs'))
  ok=child.returncode==0 and 'ALL_STOCK_CAPTURE_DONE' in text and len(files)==1
  result[name]=dict(status='Captured' if ok else limited or 'CaptureFailed',dataset=str(files[0]) if ok else None,
    seconds=time.monotonic()-start,peak_process_rss_bytes=rss,exit_code=child.returncode,complete='complete=1' in text,
    renderer_sha256=sha(renderer),settings=c,material_context='SP renderer; MP BSP semantics must remain qualified',console=str(console),
    errors=[line for line in text.splitlines() if 'offline export' in line or 'ERROR' in line][-12:])
  write(record,result);print(name,result[name]['status'],round(result[name]['seconds'],1),rss,flush=True)
 return result
if __name__=='__main__':
 p=argparse.ArgumentParser(__doc__);p.add_argument('--private',type=Path,required=True);p.add_argument('--renderer',type=Path,required=True);a=p.parse_args();capture(a.private,a.renderer.resolve())
