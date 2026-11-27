"""Prepare a grounded map step; all camera movement uses ordinary player controls."""
import argparse
import json
from pathlib import Path

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--map',required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--label',required=True)
    p.add_argument('--forward',type=int,default=0)
    p.add_argument('--back',type=int,default=0)
    p.add_argument('--left',type=int,default=0,help='Turn left for this many wait ticks at 60 degrees/second')
    p.add_argument('--right',type=int,default=0)
    p.add_argument('--look-down',type=int,default=0)
    p.add_argument('--look-up',type=int,default=0)
    p.add_argument('--compare',action='append',default=[],help='slug=cvar for a before/after pair at the resulting position')
    p.add_argument('--ssgi-debug',action='store_true')
    a=p.parse_args()
    cmd=['uimenu gallery_resume','cl_yawspeed 60','cl_pitchspeed 60','r_ssgiDebug 0']
    for option,button in [('left','left'),('right','right'),('look_down','lookdown'),('look_up','lookup'),('forward','forward'),('back','back')]:
        ticks=getattr(a,option)
        assert 0<=ticks<=600
        if ticks:cmd += ['+'+button,'wait '+str(ticks),'-'+button,'wait 15']
    cmd+=['wait 60','viewpos','screenshot_tga '+a.label+'-location','wait 20']
    if a.ssgi_debug:
        cmd+=['developer 1','r_ssgiDebug 8','wait 60','screenshot_tga '+a.label+'-emissive','wait 20',
            'r_ssgiDebug 9','wait 120','screenshot_tga '+a.label+'-indirect','wait 20','r_ssgiDebug 0']
    if a.compare:
        cmd+=['uimenu gallery_freeze','fx_freeze 1']
    for pair in a.compare:
        name,cvar=pair.split('=',1)
        for suffix,value in [('before',2),('after',0),('repeat',2)]:
            cmd += [cvar+' '+str(value),'wait 120','viewpos','screenshot_tga '+name+'-'+suffix,'wait 20']
        cmd += [cvar+' 0']
    a.output.write_text(json.dumps(dict(map=a.map,grounded=True,commands=cmd),indent=2))
    print('Prepared grounded step',a.label)

if __name__=='__main__':main()
