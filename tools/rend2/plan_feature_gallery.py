"""Build the first reviewed, map-grouped gallery capture batch."""
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'build/feature-gallery'

def main():
    settings=dict(r_ssgi=0,r_ssr=1,r_cubeMapping=1,r_colorGrading=1,r_bloom=0,
        r_autoPBR=2,r_autoEmissive=1,r_aoMode=2,r_smaa=1,r_smaaQuality=3,
        r_phongTessellation=1,r_phongTessGameplay=1,r_phongTessDebug=0,
        r_volumetricFog=2,r_volumetricFogQuality=2,r_volumetricFogHeight=1,
        r_volumetricFogHeightBase=512,r_volumetricFogHeightFalloff=512,
        r_volumetricFogHeightOpaqueDistance=2500,r_volumetricFogTemporal=0,
        r_waterSurface=1,r_waterfall=1,r_waterfallGeometry=1,r_waterfallQuality=2,
        r_detailTextureSynthesis=0,r_materialClassification=1,
        r_sunShadowMode=1,r_sunlightMode=1,r_forceSun=0)
    commands=['wait 120','r_colorGradingCompare 2','r_phongTessellationCompare 2',
        'r_volumetricFogCompare 2','r_volumetricFogHeightCompare 2',
        'r_ssrCompare 2','r_aoCompare 2','r_smaaCompare 0']
    plan=[]
    def scene(map_name,pose,exposure=0,extra=None):
        commands.extend(['devmap '+map_name,'wait 240','exitview','cam_disable','lock_disable',
            'god','notarget','noclip','give all','weapon 3','wait 60','cg_thirdPerson 0',
            'r_drawentities 0',f'r_exposureCompensation {exposure}','setviewpos '+pose])
        if extra:commands.extend(extra)
        commands.extend(['wait 60','uimenu gallery_freeze','fx_freeze 1','wait 30'])
    def pair(fid,map_name,pose,why,off,on,extra=None):
        name=fid.split('/')[-1]
        if extra:commands.extend(extra)
        commands.extend(['setviewpos '+pose]+off+['wait 90','screenshot_tga '+name+'-before','wait 15']+
            on+['wait 180','screenshot_tga '+name+'-after','wait 15']+
            off+['wait 90','screenshot_tga '+name+'-repeat','wait 15'])
        plan.append(dict(id=fid,map=map_name,pose=pose,reason=why,before=off,after=on,status='capture-pending'))
    scene('mp/ffa3','671 -769 -70 290')
    pair('lighting/ambient-occlusion-gtao','mp/ffa3','671 -769 -70 290',
        'Column/floor junctions and recessed door expose contact occlusion.', ['r_aoCompare 2'],['r_aoCompare 0'])
    pair('reflections/screen-space-reflections-ssr','mp/ffa3','671 -769 -105 290',
        'Low camera faces the lit door across the reflective floor.', ['r_ssrCompare 2'],['r_ssrCompare 0'])
    pair('post-processing/anti-aliasing-smaa','mp/ffa3','671 -769 -70 290',
        'Door frame, curved column and fine diagonal floor texture expose edge aliasing.', ['r_smaaCompare 2'],['r_smaaCompare 0'])
    commands.extend(['uimenu gallery_resume','r_drawentities 1','setviewpos 737 -949 -70 110',
        'npc spawn kyle','wait 60','setviewpos 737 -949 -105 110','zoom','wait 60','uimenu gallery_freeze'])
    pair('characters/smoother-cinematic-characters','mp/ffa3','737 -949 -105 110',
        'A controlled, front-lit Kyle close-up shows head/shoulder silhouette smoothing; gameplay override is explicit.',
        ['r_phongTessellationCompare 2'],['r_phongTessellationCompare 0'])
    scene('hoth3','3464 700 40 90',exposure=1)
    pair('post-processing/brightness-and-exposure','hoth3','3464 700 40 90',
        'Dark clinic and bright display show a fixed exposure change without auto-metering drift.',
        ['r_exposureCompensation 0'],['r_exposureCompensation 1'])
    scene('academy1','-1408 272 772 270',exposure=1)
    pair('post-processing/tone-mapping','academy1','-1408 272 772 270',
        'Bright barred window and sunlit floor against a dark stone hall show highlight/contrast handling.',
        ['r_toneMapCompare 2'],['r_toneMapCompare 0'])
    scene('yavin1b','-80 -4240 900 205',exposure=1)
    pair('post-processing/color-grading','yavin1b','-80 -4240 900 205',
        'Foliage, neutral stone and water expose a documented warm example LUT.',
        ['r_colorGradingCompare 2'],['r_colorGradingCompare 0'])
    commands.append('r_volumetricFogCompare 0')
    pair('fog/ground-haze','yavin1b','-80 -4240 900 205',
        'Elevated courtyard view looks across several depth layers toward the river and trees.',
        ['r_volumetricFogHeightCompare 2'],['r_volumetricFogHeightCompare 0'])
    commands.extend(['r_volumetricFogCompare 2','r_volumetricFogHeightCompare 2'])
    scene('t1_sour','8852 -4423 412 256',exposure=.5)
    name='fine-material-detail'
    commands.extend(['wait 90',f'screenshot_tga {name}-before','wait 15',
        'r_detailTextureSynthesis 1','vid_restart','wait 240',
        'setviewpos 8852 -4423 412 256','uimenu gallery_freeze','fx_freeze 1',
        'wait 120',f'screenshot_tga {name}-after','wait 15'])
    plan.append(dict(id='materials/fine-material-detail',map='t1_sour',pose='8852 -4423 412 256',
        reason='Close side-lit plaster/stone corridor reveals pores and normal/roughness detail.',
        before=['r_detailTextureSynthesis 0'],after=['r_detailTextureSynthesis 1','vid_restart'],
        repeat_policy='No third restart solely for a repeat; inspect matched camera and changed material detail.',status='capture-pending'))
    scene('t3_hevil','224 -2800 -700 90 20',exposure=1)
    pair('water/waterfalls-and-rapids','t3_hevil','224 -2800 -700 90 20',
        'Stand back from the tall spillways to show several sheets and their full drop.',
        ['r_waterfallCompare 2'],['r_waterfallCompare 0'])
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'batch-01-plan.json').write_text(json.dumps(plan,indent=2))
    (OUT/'batch-01-spec.json').write_text(json.dumps(dict(settings=settings,commands=commands),indent=2))
    print('Planned 10 feature comparisons, grouped into 6 map visits; one required detail restart.')

if __name__=='__main__':main()
