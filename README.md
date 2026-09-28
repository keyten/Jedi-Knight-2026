# BetterJK

This mod aims to:
- bring gameplay extensions (new force powers, weapons, etc) while keeping the game identity
- bring new modding abilities
- bring new graphics/physics enhancements into rend2

into Jedi Academy / Outcast. Most changes here are aiming to SP, but there can occasional MP improvements (mostly rend2, I guess) as side effects.

It attempts to keep the full backward compatibility with the game and mods by introducing any changes only under commands. Therefore the game should behaving exactly same as before, until a specific command or a mod enables new functionality.

This is a fork of SomaZ/OpenJK:rend2-unified-wip.

The name is a reference to a BetterJA mod that I once maintained.

## Graphics

### Tone mapper improvement

If the map is rendered in HDR, its pixels have to be translated back into SDR monitor. BetterJK introduces two translators (so, works only with `r_hdr 1`).

- `r_toneMapMode 0` - legacy rend2 tone mapper (make sure you have `r_exposureCompensation 0). Removes parts that are too bright (unlike other tone mappers).
- `r_toneMapMode 1` - ACES fitted, gives some cinematic effect (recommended with `r_exposureCompensation 1`).
- `r_toneMapMode 2` - AgX, similar to legacy but more correct (recommended with `r_exposureCompensation 0.5`).

Also you can try `r_toneMapDebug 0 | 1 | 2 | 3 | 4 | 5` to see the difference.

Doesn't require `vid_restart`.

### Linear lighting (more correct)

- `r_linearLighting 0` - to disable
- `r_linearLighting 1` - to enable

Makes the renderer to calculate light in linear space, not in sRGB (which is more realistic, and more HDR-correct).

Requires `vid_restart`.

### LUTs

You can use LUTs to do color correction on maps, e.g. make Tatooine more yellow-and-bright-ish, and Coruscant more gloomy.

- `r_colorGrading 0` - disable.
- `r_colorGrading 1` - enable.
- `r_colorGradingCompare 1` - split-screen comparison (with `r_colorGrading 1`).
- `r_colorGradingIntensity 0.5` - intensity, value from 0 to 1.
- `r_colorGradingLUT luts/my_lut.cube`

You can download `.cube` LUTs on the internet, it's a common format. Just put them in `base/luts`.

### GTAO and contact shadows

GTAO is improved shadowing mode, adds more shadows under objects, in wall-floor junctions and others.

- `r_aoMode 0` - disabled
- `r_aoMode 1` - legacy (SSAO)
- `r_aoMode 2` - GTAO

Controls:
r_gtaoQuality
r_gtaoRadius
r_gtaoThickness
r_gtaoPower
r_gtaoDenoise

Debugging:
r_aoDebug 0-10
r_aoCompare - split-screen comparison, SSAO/GTAO.

Contact shadows:
r_contactShadows
r_contactShadowLength
r_contactShadowSteps
r_contactShadowThickness
r_contactShadowStrength

### Motion blur

- `r_motionBlur 0` - disable
- `r_motionBlur 1` - enable

r_motionBlurShutterAngle — 180.
r_motionBlurReferenceFps — 60.
r_motionBlurMaxPixels — 32 at 1080p.
r_motionBlurQuality — 1.
r_motionBlurSamples — 0 (from quality).
r_motionBlurViewModelScale — 0.5, weakened for first-person view.
r_motionBlurCutDistance — 256, r_motionBlurCutAngle — 75: thresholds for teleport/camera change.
r_motionBlurDebug — 1-5:

r_motionBlurShutterScale — multiplier for exposition;
r_motionBlurReset — cgame sets 1 when camera changes, renderer resets to 0.

### Screen-space Reflections

Creates true reflections for glassy surfaces.

- r_ssr 0/1
- r_ssrQuality 0–3, r_ssrSteps, r_ssrRefineSteps, r_ssrMaxDistance, r_ssrThickness, r_ssrMaxRoughness, r_ssrEdgeFade, r_ssrHalfRes, r_ssrHiZ, r_ssrTemporal (latch), r_ssrTemporalWeight, r_ssrBlendStrength.
- compare: r_ssrCompare, r_ssrDebug 1–11.
- support lightsaber and effects reflections: r_ssrEmitters 0/1, r_ssrEmitterIntensity, r_ssrEmitterMaxRoughness

## Map-specific scripts

In mods you can bind commands to a map loading/unloading. Examples of usage:
- give some item (saber, weapon, force power) only on specific map to change its gameplay
- update renderer settings, so that specific map gets some colour correction, light settings or anything like that
- add physical objects (cloth, pushable entities, etc) to playable maps

Example:

**give_red_saber_on_t1_rail.pk3** - a mod that gives you Force Deflect exclusively on t1_rail map:
```
maps_configs/t1_rail/give_force_deflect.cfg
maps_configs/t1_rail/give_force_deflect_unload.cfg
```

**give_force_deflect.cfg**:
```
setforcedeflect 1
```

**give_force_deflect_unload.cfg**
```
setforcedeflect 0
```

Please always make sure to unload your changes, so that they don't leak into other maps.

## New physical objects

You can use `get_position` command on the map to get your current in-map coordinates.

### Cloth

Adds a rectangle cloth. It reacts to the player, npcs (including force gripped ones), force push/pull and saber damage.

```
add_cloth x y z width height angle [shader]
```

Example:

```
add_cloth 100 200 300 128 192 90 textures/kejim/metal
```

### Movable entities

Adds an object in front of the player. The object can knockdown NPCs, can be pushed, pulled and moved via Force Telekinesis. 

```
add_object objectname mass
```

Examples:
```
add_object box 80
add_object barrel 40
add_object imperial/crate_02 150
add_object models/map_objects/hoth/crate_snow.md3 120
```

You can also make an existing in-map entity physical by doing:
```
make_entity_physical <entity_id> <mass>
```

You can find out `entity_id` of the entity under your crosshair by `get_entity_id`.

UNSTABLE: you can also make all entities on the map physical by:
```
make_all_entities_physical [mass]
convert_static_models [mass]
```

### UNSTABLE: deformable objects

Same but the objects can be deformed. This is absolutely not working the way I wanted, so I mark it as unstable.

```
add_object_deform <objectname> <mass> [density] [elasticity] [plasticity] [damping] [iterations]
```

Example:
```
add_object_deform box 80
add_object_deform barrel 120 2.0 0.35 0.9 0.94 7
```

BSP surfaces can also be made deformable from explosions / concussions, but that works even worse.

Uses Verlet:
```
make_box_deform x y z width height depth [density] [elasticity] [plasticity] [damping] [iterations]
```

Tries to just create a crater:
```
make_box_deform2 x y z width height depth [depth_scale] [edge_noise]
```

## New force powers

### Force Telekinesis

Same as Force Grip in The Force Unleashed.

Grabs an NPC or an objects and lets you move them in space. Unlike Force Grip, doesn't do damage on the NPC.

- If you release NPC/object while moving it somewhere, it'll be pushed that way (same as in TFU).
- Works with pull and push.
- Physical objects are also movable (added by `add_object` command).
- You cannot move while holding someone or something, as your movement keys are used to move the held object.

```
setForceTelekinesis 3
```

### Force Soar

Gives you more aerial abilities but requires some skill.
- You can sit to the wall (not just push from it).
- Wallruns and wallflips can be done even when you touch the wall mid-air.
- Second jump available after touching the wall (wallruns and wallflips).
- You can freeze for a few attacks in the air before landing.

```
setForceSoar 3
```

### Deflect

Lets you protect yourself from weapons without a saber. Inspired by Kylo Ren's from Ep. 9.

- Level 1: lets you freeze a blaster bolt, it'll disappear after.
- Level 2: you can also move.
- Level 3: deflects the bolt back into enemy.

### Chain Lightning

Works on one NPC for 2 seconds, spreads to the next NPC after 1 second. Activated by _Force Grip 4 + Lightning_, but can be used as a separate FP.

- Level 1: max 2 NPC
- Level 2: max 5 NPC
- Level 3: unlimited.

```
setForceChainLightning 3
bind J +force_chain_lightning
```

## Some force powers are upped to 4-5

No existing force power is changed. Some are overpowered intentionally, so you can use them to play as Vader / Yoda / Starkiller.

### Saber throw

Level 4 added:
- Dual swords are thrown both together (saber throw 3 throws only one sword)
- Staff can be thrown

```
setSaberThrow 4
```

### Force Push, Pull, Jump

Levels 4 and 5 added. Extended range, distance and power.

**Force Pull 5** can disarm a lightsaber from a weak NPC.

```
setForceJump 5
setForcePush 5
setForcePull 5
```

### Force Grip

Levels 4 and 5 added.
- Extended distance (level 4 - 768, level 5 - 1024)
- Works on ungrippable NPCs (such as Desann)
- Level 4: turns off Force Absorb 1-2
- Level 5: turns off Force Absorb 3
- Level 4: grips two NPC (without force) at the same time
- Level 5: grips three NPC

Combination with forces:
- Grip 4/5 + Lightning: works as Chain Lightning
- Grip 4/5 + Drain: pulls the player to the NPC and activates Force Drain
- Grip 4/5 + Attack: does Force Pull + attack, impossible to deflect.

```
setForceGrip 5
```

### Force Drain 4

- Turns off NPCs Force Absorb 1-2, Force Protection 1-2, Force Heal 1-2
- Ignores Force Push 1
- Drains HP & FP faster.

```
setForceDrain 4
```

### Force Heal

Level 4:
- Heals faster.
- Ups maximum hp to 120.

```
setForceHeal 4
```

## How to install



## OpenJK

TAL at the original OpenJK readme [here](https://github.com/JACoders/OpenJK/blob/master/README.md).

OpenJK is licensed under GPLv2 as free software. You are free to use, modify and redistribute OpenJK following the terms in [LICENSE.txt](https://github.com/JACoders/OpenJK/blob/master/LICENSE.txt)
