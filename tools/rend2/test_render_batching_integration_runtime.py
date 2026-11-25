"""One bounded combined Yavin runtime: initialized sun/velocity, native grass,
rendered MD3 repeats, Ghoul2 queries, compact world lists and character culling.
All binaries/home/entities stay private; installed assets are read only.
"""
import argparse,json,re,subprocess
from pathlib import Path
from types import SimpleNamespace
from test_character_cull_runtime import foreign_games
from test_sprite_cull_runtime import prepare as prepare_grass
from test_depth_prepass_runtime import tga_pixels
ROOT=Path(__file__).resolve().parents[2]

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--installation',type=Path,required=True)
 p.add_argument('--binaries',type=Path,default=ROOT/'build/msvc/Release')
 p.add_argument('--renderer',type=Path,default=ROOT/'build/msvc/Release')
 p.add_argument('--label',default='combined-yavin')
 p.add_argument('--run',action='store_true');p.add_argument('--analyze-only',action='store_true')
 a=p.parse_args()
 config=SimpleNamespace(**vars(a),map='yavin1',pose='1024 -4700 210 90',gl43=1,cards=3,
                        shadows=True,bend_distance=350,card_distance=300)
 run,home,launch=prepare_grass(config)
 entities=['{ "classname" "worldspawn" }','{ "classname" "info_player_start" "origin" "1024 -4700 210" "angle" "90" }']
 for i in range(40):
  x=800+i%8*64;y=-4400+i//8*80
  entities.append('{ "classname" "misc_model_static" "model" "models/map_objects/factory/barrel.md3" '
                  f'"origin" "{x} {y} 144" "angles" "0 {i%4*30} 0" "modelscale" "{1+i%3*.125}"'+' }')
 (home/'maps').mkdir(exist_ok=True);(home/'maps/yavin1.ent').write_text('\n'.join(entities)+'\n')
 cfg=home/'autoexec_sp.cfg';settings=cfg.read_text().split('wait 60',1)[0]
 settings+='set r_characterCull 1\nset r_md3Instancing 0\nset r_md3InstancingStats 1\nset r_modelOcclusionDebug 1\nset r_worldSurfaceList 2\n'
 commands=['wait 60','devmap yavin1','wait 180','exitview','cam_disable','lock_disable','god','notarget','noclip','cg_thirdPerson 0']
 for i in range(6):
  commands += [f'setviewpos {824+i*80} -4400 210 90','wait 5','npc spawn stormtrooper','wait 15']
 commands+=['setviewpos 1024 -4700 210 90','wait 30','uimenu sprite_freeze','fx_freeze 1','wait 30']
 def phase(name,changes):
  return changes+[f'echo BATCH_BEGIN_{name}','wait 120',f'screenshot_tga batch_{name}',
                  'r_speeds 1','wait 2','r_speeds 0',f'echo BATCH_END_{name}']
 commands+=phase('reference',['r_worldSurfaceList 0','r_modelOcclusion 0','r_md3Instancing 0','r_surfaceSpritesCompute 0'])
 commands+=phase('compact',['r_worldSurfaceList 2'])
 commands+=phase('instances',['r_md3Instancing 1'])
 commands+=phase('grass',['r_surfaceSpritesCompute 1'])
 commands+=phase('query_reference',['r_modelOcclusion 2'])
 commands+=phase('conditional',['r_modelOcclusion 1'])
 commands+=phase('repeat',['r_worldSurfaceList 0','r_modelOcclusion 0','r_md3Instancing 0','r_surfaceSpritesCompute 0'])
 commands+=['uimenu sprite_resume','wait 5','setviewpos 1024 -4380 210 90','wait 20','uimenu sprite_freeze','wait 20']
 commands+=phase('near_query',['r_worldSurfaceList 1','r_md3Instancing 1','r_surfaceSpritesCompute 1','r_modelOcclusion 2'])
 commands+=phase('near_conditional',['r_modelOcclusion 1'])
 commands+=phase('lod',['r_surfaceSpritesCompute 2'])
 commands+=['echo BATCH_INTEGRATION_DONE','wait 5','quit']
 cfg.write_text(settings+'\n'.join(commands)+'\n')
 if a.run:
  foreign=foreign_games();assert not foreign,foreign
  (run/'run-meta.json').write_text(json.dumps({'foreign_before':foreign,'launch':launch,'sun_shadows_initialized':True,'velocity_initialized':True,'character_cull':1},indent=2))
  si=subprocess.STARTUPINFO();si.dwFlags|=subprocess.STARTF_USESHOWWINDOW;si.wShowWindow=0
  with (run/'launch.log').open('w') as out:
   proc=subprocess.Popen(launch,cwd=run,startupinfo=si,stdout=out,stderr=out)
   try:code=proc.wait(timeout=600)
   except subprocess.TimeoutExpired:proc.terminate();raise
  assert code==0,code
 elif not a.analyze_only:
  print('Prepared',run);return
 log='\n'.join(p.read_text(errors='replace') for p in home.glob('*.log'))
 assert 'BATCH_INTEGRATION_DONE' in log,'incomplete run'
 assert not re.search(r'GL_INVALID|GL_OUT_OF_MEMORY|FBO.*incomplete|GLSL.*(?:failed|error)|surface list mismatch|sprite compute.*failed',log,re.I)
 pairs=[('reference','compact'),('compact','instances'),('instances','grass'),('query_reference','conditional'),('reference','repeat'),('near_query','near_conditional')]
 differences={}
 for left,right in pairs:
  x=tga_pixels(home/f'screenshots/batch_{left}.tga');y=tga_pixels(home/f'screenshots/batch_{right}.tga')
  assert len(x)==len(y)
  differences[left+'/'+right]=sum(a!=b for a,b in zip(x,y))
 assert all(v==0 for v in differences.values()),differences
 phases={name:body for name,body in re.findall(r'BATCH_BEGIN_(\w+)(.*?)BATCH_END_\1',log,re.S)}
 md3=[tuple(map(int,r)) for r in re.findall(r'md3Instancing: tagged (\d+), fallback (\d+), groups (\d+), instances (\d+), saved (\d+) draws, overflow (\d+), upload (\d+) bytes',phases['instances'])]
 sprites=[tuple(map(int,r)) for r in re.findall(r'Surface sprites GPU: candidates (\d+) submitted (\d+) culled (\d+) cards (\d+) verts (\d+) dispatches (\d+) reused (\d+)',phases['grass'])]
 assert any(r[4]>0 for r in md3),('no MD3 groups',md3)
 assert any(r[2]>0 and r[6]>0 for r in sprites),('no native grass culling/reuse',sprites)
 model_pattern=r'model occlusion: resolved=(\d+) hidden=(\d+) guardedDraws=(\d+) skippedDraws=(\d+) skippedIndices=(\d+)'
 far_models=[tuple(map(int,r)) for r in re.findall(model_pattern,phases['conditional'])]
 near_models=[tuple(map(int,r)) for r in re.findall(model_pattern,phases['near_conditional'])]
 assert any(r[0]>0 and r[1]>0 and r[2]>0 for r in far_models),('query path untested',far_models)
 assert near_models and all(r[0]==0 and r[2]==0 for r in near_models),('near bypass failed',near_models)
 report={'capture_channel_differences':differences,'md3_sampled_frames':md3,'sprite_sampled_frames':sprites,'far_model_queries':far_models,'near_model_queries':near_models,'initialized':['sun cascades','fused velocity'],'character_cull':1,'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT).decode().strip()}
 (run/'results.json').write_text(json.dumps(report,indent=2));print(json.dumps(report,indent=2))
if __name__=='__main__':main()
