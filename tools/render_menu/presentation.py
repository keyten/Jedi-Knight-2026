"""Reviewed user-facing controls, bounded sliders, modes and feature costs."""
import re

LABELS = {
 'r_hdr':'High dynamic range', 'r_toneMap':'Compress HDR highlights',
 'r_autoExposure':'Automatic exposure', 'r_depthPrepass':'Depth prepass',
 'r_normalMapping':'Surface normal detail', 'r_specularMapping':'Material reflections',
 'r_parallaxMapping':'Surface height relief', 'r_sunlightMode':'Sun lighting',
 'r_dlightMode':'Dynamic light shadows', 'r_cubeMapping':'Environment reflections',
 'r_toneMapMode':'Tone mapper', 'r_colorGrading':'Color grading',
 'r_colorGradingLUT':'Choose color palette...', 'r_colorGradingIntensity':'Palette intensity',
 'r_linearLighting':'Physically correct light blending', 'r_bloom':'Light glow',
 'r_bloomKnee':'Glow threshold softness', 'r_bloomScatter':'Glow spread',
 'r_bloomSceneIntensity':'Glow from bright surfaces', 'r_motionBlur':'Motion blur',
 'r_motionBlurShutterAngle':'Blur exposure angle (degrees)',
 'r_motionBlurReferenceFps':'Blur reference frame rate', 'r_motionBlurMaxPixels':'Maximum blur length (pixels)',
 'r_motionBlurViewModelScale':'First-person weapon blur', 'r_motionBlurCutDistance':'Camera jump threshold',
 'r_motionBlurSamples':'Blur samples (0 = quality preset)',
 'r_motionBlurCutAngle':'Camera turn threshold (degrees)', 'r_autoPBR':'Legacy material adaptation',
 'r_autoPBRRoughness':'Automatic roughness detail', 'r_autoPBRConvert':'Convert old shiny materials',
 'r_diffuseBRDF':'Diffuse light response', 'r_autoEmissive':'Old glow emits physical light',
 'r_pomAdaptiveSteps':'Adaptive relief sampling', 'r_pomMinSteps':'Relief samples: front view',
 'r_pomMaxSteps':'Relief samples: grazing view', 'r_pomBinarySteps':'Relief hit refinement',
 'r_pomFadeStart':'Relief fade start (units)', 'r_pomFadeEnd':'Relief fade end (units)',
 'r_pomSelfShadow':'Relief self-shadowing', 'r_pomSelfShadowLightMode':'Lights casting relief shadows',
 'r_pomSelfShadowMaxLocalLights':'Relief local light limit', 'r_pomSelfShadowBias':'Relief shadow ray offset',
 'r_pomSilhouette':'Relief changes surface edges', 'r_pomSilhouetteViewDependence':'Extra grazing-angle samples',
 'r_pomSilhouetteShadows':'Displaced edges cast shadows',
 'r_pomSilhouetteContactShadows':'Contact shadows on displaced edges',
 'r_autoPOMSilhouetteMode':'Automatic displaced edges', 'r_aoMode':'Ambient occlusion method',
 'r_aoApply':'Apply occlusion to', 'r_aoMultiBounce':'Preserve bright indirect light',
 'r_aoLightmapFraction':'Baked light treated as indirect', 'r_aoSpecOcclusion':'Reflection occlusion method',
 'r_gtaoHalfRes':'Occlusion render resolution', 'r_gtaoBentNormals':'Directional occlusion strength',
 'r_gtaoPower':'Occlusion contrast', 'r_gtaoThickness':'Thin object compensation',
 'r_contactShadows':'Small sun contact shadows', 'r_contactShadowSoft':'Softer contact shadows',
 'r_forwardPlus':'Clustered dynamic lights', 'r_forwardPlusTileSize':'Lighting tile size (pixels)',
 'r_forwardPlusSlices':'Lighting depth layers', 'r_forwardPlusNearSlice':'Nearest light layer depth',
 'r_forwardPlusMaxLightsPerCluster':'Lights per cluster limit',
 'r_forwardPlusMaxShadowLights':'Dynamic lights with shadows',
 'r_ssgi':'Screen-space bounced light', 'r_ssgiSource':'Bounced light sources',
 'r_ssgiRays':'Bounce rays (0 = quality preset)', 'r_ssgiSteps':'Bounce steps (0 = quality preset)',
 'r_ssgiHalfRes':'Bounced light resolution', 'r_ssgiHiZ':'Bounced light tracing',
 'r_ssgiGlowScale':'Bounce from old glow materials', 'r_diffuseIBL':'Directional environment light',
 'r_diffuseIBLStrength':'Environment light directionality', 'r_ltcAreaLights':'Area-shaped light sources',
 'r_ltcStaticDiffuse':'Add diffuse from static lamps', 'r_ltcMaxLights':'Area light limit',
 'r_ltcAutoAreaLights':'Automatic lamp detection', 'r_ltcSaberAreaLights':'Saber-shaped light sources',
 'r_spotLights':'Spotlight cones', 'r_spotLightShadows':'Spotlight shadows',
 'r_spotLightCookies':'Projected spotlight textures', 'r_sunShadowMode':'Sun shadow method',
 'r_shadowPCSS':'Distance-dependent shadow softness', 'r_sunShadowAlphaCasters':'Leaves cast cutout shadows',
 'r_shadowCascadeBlend':'Shadow cascade transition blend', 'r_shadowDepthBias':'Shadow depth offset',
 'r_shadowNormalBias':'Shadow surface-normal offset', 'r_shadowSlopeBias':'Shadow slope offset',
 'r_shadowReceiverBiasClamp':'Maximum shadow offset', 'r_shadowSunAngularDiameter':'Sun size (degrees)',
 'r_shadowPCSSMaxPenumbra':'Maximum sun shadow softness',
 'r_entityLightGrid':'Character lighting detail', 'r_skinSSS':'Skin light scattering',
 'r_skinSSSMixedHeads':'Allow mixed skin/head materials', 'r_skinSSSFollowSurface':'Scattering follows skin relief',
 'r_skinSSSWrap':'Light wrapping around skin', 'r_skinSSSTransmission':'Light passing through skin',
 'r_shadowCasterLod':'Character shadow detail follows view', 'r_dlightShadowBias':'Dynamic shadow offset method',
 'r_ssr':'Screen-space reflections', 'r_ssrHalfRes':'Reflection render resolution',
 'r_ssrHiZ':'Reflection ray tracing', 'r_ssrHitCache':'Reuse reflection ray hits',
 'r_ssrSteps':'Reflection samples (0 = preset)',
 'r_ssrReceiverCull':'Skip nonreflective surfaces', 'r_ssrEmitters':'Reflect sabers and blaster effects',
 'r_ssrMaxRoughness':'Maximum reflective roughness', 'r_ssrBlendStrength':'Screen reflection blend strength',
 'r_volumetricFog':'Volumetric fog method', 'r_volumetricFogGridScale':'Fog cell size (0 = preset)',
 'r_volumetricFogSlices':'Fog depth layers (0 = preset)', 'r_volumetricFogFar':'Fog draw distance (0 = automatic)',
 'r_volumetricFogAnisotropy':'Light scattering directionality', 'r_volumetricFogSunScale':'Sunlight in fog',
 'r_volumetricFogDlightScale':'Dynamic light in fog', 'r_volumetricFogStaticScale':'Baked light in fog',
 'r_volumetricFogDlightShadows':'Dynamic light shadows in fog', 'r_volumetricFogBloom':'Fog light feeds glow',
 'r_volumetricFogStaticDirectional':'Directional baked light in fog', 'r_volumetricFogHeight':'Height-dependent fog',
 'r_volumetricFogHeightOpaqueDistance':'Fog opacity distance (units)', 'r_volumetricFogHeightBase':'Fog base altitude (units)',
 'r_volumetricFogHeightFalloff':'Fog altitude falloff (units)', 'r_volumetricFogHeightMaxDensity':'Maximum fog density',
 'r_volumetricFogHeightTopHeight':'Fog ceiling (0 = unlimited)', 'r_volumetricFogHeightColor':'Height fog color',
 'r_volumetricFogNoise':'Uneven fog density affects', 'r_volumetricFogNoiseWind':'Fog drift direction',
 'r_volumetricParticles':'3D smoke and dust volumes', 'r_volumetricParticlesMax':'3D particle limit',
 'r_volumetricParticlesHistory':'Particle history persistence', 'r_particleLighting':'Smoke sprite lighting',
 'r_particleLightingMix':'Smoke lighting blend', 'r_particleLightingScale':'Smoke lighting brightness',
 'r_particleLightingFloor':'Minimum smoke brightness',
 'r_volumetricEmission':'Glowing particles light fog', 'r_volumetricSelfShadow':'Fog casts shadows inside itself',
 'r_volumetricSelfShadowOutsideHeightFog':'Fog self-shadows outside height fog',
 'r_volumetricSelfShadowMaxLights':'Self-shadowed fog light limit',
 'r_volumetricMultiScatter':'Repeated fog light scattering', 'r_volumetricMultiScatterOctaves':'Fog scattering layers',
 'r_volumetricMultiScatterPhase':'Scattered light direction retention',
 'r_volumetricMultiScatterShadowFill':'Fill dark fog shadows',
 'r_volumetricFogRGBExtinction':'Colored fog light absorption', 'r_volumetricFogHeightExtinction':'Fog absorption color',
 'r_weatherCull':'Skip invisible weather regions', 'r_rainStreaks':'Rain streak style',
 'r_rainStreakLighting':'Light rain from the world', 'r_weatherWetness':'Rain makes surfaces wet',
 'r_weatherWetnessNormal':'Wet surface detail flattening', 'r_weatherWetnessEntityFacing':'Wetness on vertical model faces',
 'r_weatherWetnessBias':'Rain exposure depth offset', 'r_weatherPuddles':'Procedural puddles',
 'r_weatherPuddleSlope':'Puddles form on', 'r_weatherPuddleUseHeightMap':'Water fills material cracks',
 'r_weatherPuddleWaterLevelBias':'Puddle water level offset', 'r_weatherPuddleRipples':'Rain ripples in puddles',
 'r_weatherRunoff':'Water flowing down walls', 'r_weatherRunoffProbe':'Wall rain exposure probe',
 'r_weatherRunoffEntities':'Water flows on characters/props', 'r_rainSplashes':'Rain impact splashes',
 'r_rainLens':'Rain drops on camera lens', 'r_rainLensSimulation':'Persistent lens water', 'r_rainLensDropSize':'Lens drop size',
 'r_autoFoliage':'Automatic plant detection', 'r_grassCardMode':'Grass geometry',
 'r_grassCardLodDist':'Grass third-card fade distance', 'r_grassCardWidth':'Grass blade width',
 'r_foliageWind':'Coherent plant wind', 'r_foliageWindDirection':'Plant wind direction (degrees)',
 'r_leafFlutter':'Leaf rustling', 'r_leafFlutterNormal':'Leaf lighting wobble',
 'r_plantWind':'Root-anchored plant bending', 'r_foliageInteraction':'Characters push plants aside',
 'r_foliageInteractionRadius':'Character push radius scale', 'r_foliageInteractionMaxInteractors':'Characters affecting plants',
 'r_foliageInteractionNPCs':'Nearby characters also push plants', 'r_foliageBendField':'Plants recover after contact',
 'r_foliageBendFieldSize':'Plant recovery texture size', 'r_foliageBendFieldExtent':'Plant recovery area size',
 'r_foliageBendFieldRecoveryTime':'Plant recovery time (seconds)', 'r_foliageBendFieldDamping':'Plant spring damping',
 'r_foliageBendFieldImpulse':'Extra plant bending momentum', 'r_shaderProgramCache':'Cache compiled shaders',
 'r_shaderProgramCacheMaxMB':'Shader cache size (MB)',
}

PREFIXES = {
 'bloom':'Glow', 'motionBlur':'Motion blur', 'pomSelfShadow':'Relief shadow', 'pomSilhouette':'Edge relief',
 'gtao':'Occlusion', 'contactShadow':'Contact shadow', 'ssgi':'Bounced light', 'ltc':'Area light',
 'shadowPCSS':'Soft sun shadow', 'skinSSS':'Skin scattering', 'ssrEmitter':'Effect reflection', 'ssr':'Reflection',
 'volumetricFogNoise':'Fog noise', 'volumetricFog':'Fog', 'volumetricParticles':'3D particles',
 'volumetricSelfShadow':'Fog self-shadow', 'volumetricMultiScatter':'Fog scattering',
 'rainStreak':'Rain streak', 'weatherWetness':'Wet surface', 'weatherPuddleRipple':'Puddle ripple',
 'weatherPuddle':'Puddle', 'weatherRunoff':'Runoff', 'rainSplash':'Splash', 'rainLens':'Lens rain',
 'foliageWind':'Plant wind', 'leafFlutter':'Leaf rustling', 'plantWind':'Plant bending',
 'foliageInteraction':'Plant interaction', 'foliageBendField':'Plant recovery',
}
SUFFIXES = {
 'Intensity':'brightness', 'IntensityScale':'brightness', 'Strength':'strength', 'Threshold':'brightness threshold',
 'Quality':'quality', 'Width':'width', 'Length':'length', 'Radius':'radius (units)', 'Falloff':'distance falloff',
 'Denoise':'smoothing passes', 'Thickness':'blocker thickness', 'Steps':'ray samples', 'Samples':'samples',
 'Distance':'distance (units)', 'Fade':'fade distance', 'MaxSteps':'grazing-angle samples', 'BinarySteps':'hit refinement',
 'Softness':'softness', 'Temporal':'temporal smoothing', 'HistoryWeight':'history retention',
 'TemporalWeight':'history retention', 'MaxDistance':'ray distance (units)', 'EmissiveScale':'emissive brightness',
 'RefineSteps':'hit refinement (0 = preset)', 'EdgeFade':'edge fade', 'Scale':'pattern size (units)',
 'DetailScale':'fine pattern size', 'Contrast':'contrast', 'DetailContrast':'fine pattern contrast',
 'Opacity':'opacity', 'Roughness':'roughness', 'Darkening':'darkening', 'Coverage':'coverage',
 'HeightSoftness':'waterline softness', 'RippleStrength':'ripple strength', 'RippleScale':'ripple size',
 'RippleRate':'ripple frequency', 'Speed':'speed', 'Size':'size (units)', 'Lifetime':'lifetime (ms)',
 'Density':'density', 'Refraction':'refraction strength', 'Attenuation':'light loss',
 'Contribution':'brightness contribution', 'Phase':'direction retention',
}

ENUMS = {
 'r_sunlightMode': [('Off',0),('Modulated',1),('Full lighting',2)],
 'r_dlightMode': [('Legacy',0),('Per-pixel',1),('Shadowed',2)],
 'r_toneMapMode': [('Legacy',0),('ACES',1),('AgX-like',2)],
 'r_bloom': [('Off',0),('Legacy',-1),('Modern',1)],
 'r_autoPBR': [('Legacy',0),('Dielectric',1),('Material classes',2)],
 'r_diffuseBRDF': [('Lambert',0),('Burley / Disney',1)],
 'r_pomSelfShadowLightMode': [('Sun',0),('Sun + strongest',1),('Sun + limited',2),('All lights',3)],
 'r_aoMode': [('Follow legacy',-1),('Off',0),('Legacy SSAO',1),('GTAO',2)],
 'r_aoApply': [('Automatic',-1),('All lighting',0),('Indirect only',1)],
 'r_aoSpecOcclusion': [('Automatic',-1),('Scalar',0),('Lagarde',1),('Directional cone',2)],
 'r_gtaoHalfRes': [('Full',0),('Half',1)],
 'r_ssgiSource': [('Dynamic + emission',0),('Emission only',1),('Full scene (exp.)',2)],
 'r_ltcAutoAreaLights': [('Off',0),('Confident lamps',1),('Broad detection',2)],
 'r_spotLightCookies': [('Off',0),('Brightness',1),('Colored',2)],
 'r_sunShadowMode': [('Legacy',0),('Modern',1)],
 'r_entityLightGrid': [('Legacy',0),('Three samples',1),('Per-pixel',2)],
 'r_skinSSS': [('Off',0),('Light wrap',1),('Diffusion',2)],
 'r_dlightShadowBias': [('Legacy',0),('Improved',1)],
 'r_volumetricFog': [('Off',0),('Legacy',1),('Modern',2)],
 'r_volumetricSelfShadow': [('Off',0),('Sun only',1),('Sun + local',2)],
 'r_volumetricMultiScatter': [('Off',0),('Approximation',1),('Extended',2)],
 'r_autoFoliage': [('Off',0),('Conservative',1),('Experimental',2)],
 'r_grassCardMode': [('Legacy',0),('Cross',1),('Tuft',2),('Adaptive',3)],
 'r_foliageBendFieldSize': [('64',64),('128',128),('256',256)],
 'r_rainStreaks': [('Legacy',0),('Modern',1)],
 'r_volumetricFogNoise': [('Off',0),('Height fog',1),('Map fog',2),('Height + map',3),
                          ('Global fog',4),('Height + global',5),('Map + global',6),('All fog',7)],
}
STRING_MODES = {
 'r_volumetricFogHeightColor': [('Neutral','0.7 0.75 0.8'),('White','1 1 1'),('Warm','0.9 0.7 0.5'),('Cool','0.5 0.7 0.9')],
 'r_volumetricFogHeightExtinction': [('Neutral','1 1 1'),('Warm','0.6 1 1.4'),('Cool','1.4 1 0.6')],
 'r_volumetricFogNoiseWind': [('Still','0 0 0'),('East','32 0 0'),('West','-32 0 0'),
                              ('North','0 32 0'),('South','0 -32 0')],
 'r_weatherPuddleSlope': [('Flat surfaces','0.90 0.98'),('Very flat','0.97 0.995'),('Gentle slopes','0.75 0.95')],
}
# UI bounds for legacy cvars lacking a Cvar_CheckRange in the renderer.
BOUNDS = {
 'r_forwardPlusMaxShadowLights':(0,32,True), 'r_ltcIntensityScale':(0,16,False), 'r_ltcMaxLights':(0,256,True),
 'r_volumetricFogHeightBase':(-8192,8192,False), 'r_volumetricParticlesMax':(0,128,True),
 'r_weatherWetnessStrength':(0,1,False), 'r_weatherWetnessRoughness':(0,4,False),
 'r_weatherWetnessDarkening':(0,4,False), 'r_weatherWetnessNormal':(0,4,False),
 'r_weatherWetnessEntityFacing':(0,1,False), 'r_weatherWetnessBias':(0,64,False),
 'r_weatherPuddleCoverage':(0,1,False), 'r_weatherPuddleRoughness':(0,1,False),
 'r_weatherPuddleScale':(16,4096,False), 'r_weatherPuddleHeightSoftness':(0.001,1,False),
 'r_weatherPuddleWaterLevelBias':(-1,1,False), 'r_weatherPuddleRippleStrength':(0,4,False),
 'r_weatherPuddleRippleScale':(1,256,False), 'r_weatherPuddleRippleRate':(0,8,False),
 'r_weatherRunoffStrength':(0,4,False), 'r_weatherRunoffSpeed':(0,256,False),
 'r_weatherRunoffScale':(1,512,False), 'r_weatherRunoffProbe':(0,16,False),
 'r_rainLensDensity':(0,1,False), 'r_rainLensRefraction':(0,4,False), 'r_rainLensDropSize':(0.25,4,False),
 'r_foliageInteractionMaxInteractors':(1,16,True),
}

HEAVY = ('POM self-shadowing','Silhouette POM','Automatic silhouette POM','Screen-space global illumination',
         'Screen-space reflections','Froxel volumetric fog','Sun Shadows 2.0','Volumetric self-shadowing')
MEDIUM = ('Velocity motion blur','GTAO','Screen-space contact shadows','LTC area lights',
          'Automatic LTC lamp detection','Saber line area lights','Spot / projected lights',
          'Skin subsurface scattering','Rain impact splashes','Rain on the camera lens',
          'Auto grass geometry','Character / foliage interaction','Volumetric FX particles')
ZERO = ('Tone mapping','Linear lighting','Auto PBR material classification','Automatic roughness variation',
        'Diffuse BRDF','Automatic emissive compatibility','Automatic foliage classification','GLSL program cache',
        'Weather chunk culling','Persistent foliage bend field')


def label_for(name):
    if name in LABELS: return LABELS[name]
    short = name[2:]
    for prefix in sorted(PREFIXES, key=len, reverse=True):
        if short.startswith(prefix):
            suffix = short[len(prefix):]
            return PREFIXES[prefix] + ' ' + SUFFIXES.get(suffix, re.sub(r'(?<=[a-z])(?=[A-Z])',' ',suffix).lower())
    raise ValueError('Unreviewed setting label: ' + name)


def enum_for(c):
    name = c['name']
    if name in ENUMS: return ENUMS[name]
    if name in STRING_MODES: return STRING_MODES[name]
    if name.endswith('HalfRes'): return [('Quality preset',-1),('Full',0),('Half',1)]
    if name.endswith('HiZ'): return [('Quality preset',-1),('Linear',0),('Hierarchical',1)]
    if name.endswith('Quality'):
        maxval = int(c['range'][1])
        labels = ['Low','Medium','High','Ultra']
        if name == 'r_shadowPCSSQuality': labels = ['Low','High','Ultra']
        return [(labels[i],i) for i in range(maxval+1)]
    rng = c['range']
    if rng and rng[2] and rng[:2] == [0.0,1.0]: return [('Off',0),('On',1)]
    if c['spec'].replace(' ','') == '0|1': return [('Off',0),('On',1)]
    if name in ('r_hdr','r_toneMap','r_autoExposure','r_depthPrepass','r_normalMapping',
                'r_specularMapping','r_parallaxMapping','r_cubeMapping'): return [('Off',0),('On',1)]
    return None


def performance(section):
    return 'heavy' if section in HEAVY else 'medium' if section in MEDIUM else 'zero' if section in ZERO else 'light'


def help_for(category):
    category = {'Volumetric lighting & fog':'Fog & particles', 'Rain & wet surfaces':'Weather'}.get(category,category)
    return {
      'Pipeline prerequisites': ['These switches enable the rendering pipeline.', 'HDR + tone mapping are needed by several effects.'],
      'Colors & post-processing': ['HDR is needed for glow, motion blur and linear lighting.', 'LUTs change color; put .cube files in base/luts.'],
      'Materials & surface detail': ['Relief needs normal mapping and material height data.', 'Grazing angles and more shadowed lights cost more.'],
      'Lighting & shadows': ['Occlusion/contact shadows need a depth prepass.', 'Area-shaped lights need clustered lighting.'],
      'Player & model lighting': ['Skin diffusion needs HDR and suitable skin materials.', 'Higher detail affects visible characters and models.'],
      'Reflections': ['Reflections need reflective materials and scene depth.', 'Full resolution and more rays increase GPU cost.'],
      'Fog & particles': ['Modern fog must be selected for fog volume effects.', 'More layers, particles and shadow rays cost more.'],
      'Weather': ['Rain effects require active rain on the current map.', 'Puddles/runoff need wet surfaces; lens rain needs HDR.'],
      'Foliage': ['Leaf/plant effects need automatic plant detection.', 'Recovery needs character interaction and updated cgame.'],
      'Performance & loading': ['Shader caching speeds warm starts, not frame rendering.', 'The driver must support program binary caching.'],
    }.get(category, ['Live settings update the visible game immediately.', 'Effects may require map data or other pipeline switches.'])
