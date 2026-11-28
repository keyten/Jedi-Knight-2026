"""Queue one map group into an existing monitored hidden gallery session."""
import argparse
import json
import re
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('runtime',type=Path)
    p.add_argument('label')
    p.add_argument('spec',type=Path)
    a=p.parse_args()
    assert re.fullmatch(r'[a-zA-Z0-9_-]+',a.label)
    home=a.runtime/'home/OpenJK'
    queue=home/'gallery-queue.cfg'
    assert queue.exists() and not queue.read_text().strip(),'Previous queue has not been acknowledged'
    spec=json.loads(a.spec.read_text(encoding='utf-8'))
    commands=spec['commands']
    if spec.get('grounded'):
        for command in commands:
            assert command.split()[0] not in ('noclip','setviewpos','fxcam','cam_enable'), 'Grounded captures use player movement only: '+command
    map_name=spec.get('map')
    from gallery_bsp import World
    worlds={}
    installation=Path(__file__).resolve().parents[3]/'OpenJK/build-rend2'
    camera_checks=[]
    screenshots=[]
    noclip=False
    for command in commands:
        parts=command.split()
        if parts and parts[0]=='devmap':noclip=False
        if parts and parts[0]=='noclip':noclip=not noclip
        if spec.get('landed'):
            assert not parts or parts[0] not in ('fxcam','cam_enable'), 'Final views use the player camera'
        if parts and parts[0] in ('screenshot_tga','screenshot_png','screenshotJPEG'):
            assert not spec.get('landed') or not noclip, 'Disable noclip and land before capturing'
            assert len(parts)==2
            assert not (home/'screenshots'/(parts[1]+'.tga')).exists(),'Use fresh screenshot names: '+parts[1]
            screenshots.append(parts[1])
        if parts and parts[0]=='devmap':map_name=parts[1]
        if parts and parts[0] in ('setviewpos','fxcam'):
            if parts[0]=='setviewpos':
                assert len(parts) in (5,6),'setviewpos accepts x y z yaw [pitch] in the capture build'
            else:
                assert len(parts) in (5,6),'fxcam accepts x y z yaw [pitch]'
            assert map_name,'Declare the current map in the step spec'
            if map_name not in worlds:worlds[map_name]=World(installation,map_name)
            result=worlds[map_name].camera(list(map(float,parts[1:4])))
            assert result['free'],f'Camera intersects BSP geometry: {command}: {result}'
            camera_checks.append(dict(command=command,map=map_name,**result))
    record=a.runtime/('step-'+a.label+'.json')
    assert not record.exists(),'Use a unique step label'
    record.write_text(json.dumps(dict(label=a.label,map=map_name,commands=commands,camera_checks=camera_checks,
        grounded=bool(spec.get('grounded')),landed=bool(spec.get('landed')),screenshots=screenshots),indent=2))
    checkpoint=['save gallery_checkpoint','wait 30'] if spec.get('checkpoint',True) else []
    queue.write_text('\n'.join(['echo GALLERY_ACK_'+a.label,'r_norefresh 0']+commands+
        ['wait 15']+checkpoint+['r_norefresh 1','echo GALLERY_DONE_'+a.label])+'\n')
    print('Queued',a.label,'for map review; next map requires another explicit queue.')

if __name__=='__main__':main()
