# Image and camera

[All rendering settings](../rendering-features.md)

How the finished frame is turned into the picture on your screen: brightness, contrast, glow, color, blur and edge smoothing.

## Tone mapping

The renderer computes light far brighter than a monitor can show: sky, lamps, saber blades. The tone mapper squeezes that range onto your screen and decides how contrasty the image looks and what happens to very bright colors. Pick the look you like; every curve costs the same. Needs `r_hdr 1` and `r_toneMap 1`.

- `r_toneMapMode 0` — **0** Rend2's original filmic curve. **1** ACES, the film look of many modern games: stronger contrast; very bright colored lights can shift hue. **2** AgX-like: gentler contrast; bright colored lights fade to white without changing hue.

**Compare:** `r_toneMapCompare` — 1 split screen (original curve left, `r_toneMapMode` right), 2 original curve, 3 three strips: original / ACES / AgX.

**Debug**

- `r_toneMapDebug 1..3` — raw HDR image, image after exposure, exposure in false color.

Design notes: [rend2-tonemapping.md](../rend2-tonemapping.md).

## Brightness and exposure

Like your eyes, the renderer adapts to the scene: dark rooms are brightened, sunny exteriors darkened. This is on by default. Everything in this section needs `r_hdr 1` and `r_toneMap 1`; without them brightness is fixed.

- `r_autoExposure 1` — 0 fixed brightness, 1 adapt to the scene.
- `r_exposureCompensation 0` — the simplest brightness control, in stops: `1` is twice as bright, `-1` half. Works in every mode below.

### Physical camera (optional)

Instead of the original adaptation you can use real camera settings, mostly useful for screenshots and fixed shots.

- `r_cameraMode 0` — **0** original exposure (above). **1** manual camera: brightness comes from ISO, aperture and shutter time. **2** automatic camera: meters the scene and adapts within the limits below.
- `r_cameraISO 100`, `r_cameraAperture 16`, `r_cameraShutterSeconds 0.008` — mode 1. Higher ISO, a smaller f-number or a longer shutter make the image brighter. The aperture does not blur anything: there is no depth of field yet.
- `r_cameraAutoEVMin -6` — mode 2: the darkest scene it fully adapts to. Raise it to keep dark areas dark.
- `r_cameraAutoEVMax 20` — mode 2: the brightest scene it fully adapts to. Lower it to keep bright areas bright.
- `r_motionBlurPhysicalShutter 0` — 1: in modes 1–2, [motion blur](#motion-blur) length follows `r_cameraShutterSeconds` instead of the shutter angle.

**Compare:** `r_cameraCompare 2` — the original exposure. No split screen: the scene is metered once for the whole view.

**How it works.** Manual mode converts ISO, aperture and shutter into an exposure value (EV100), as a light meter would. Automatic mode measures the average scene brightness and moves toward it, clamped to the EV range. Exposure compensation is applied on top.

- `r_cameraExposure 0` — mode 0 only: a fixed gain in stops.

**Debug**

- `r_cameraDebug 1` — print the camera and meter state once per second.

Design notes: [rend2-physical-camera.md](../rend2-physical-camera.md).

## Bloom

A soft halo around sabers, lamps and glowing panels. The original JKA "dynamic glow" blurs only surfaces whose shader is marked as glowing, with the same strength whatever their brightness. HDR bloom works from actual brightness: a brighter light glows more, and the halo is wider and smoother.

**Cost:** light.

- `r_bloom -1` — **-1** original dynamic glow (switched by the original `r_dynamicGlow`). **0** off. **1** HDR bloom; needs `r_hdr 1` and `r_toneMap 1`.

HDR bloom (`r_bloom 1`):

- `r_bloomIntensity 0.15` — strength of the halo.
- `r_bloomScatter 0.7` — how wide it spreads.
- `r_bloomSceneIntensity 0` — by default only glowing materials bloom. Raise it to let any very bright pixel bloom too: sun glints, bright sky, lit fog.

Original glow (`r_bloom -1`):

- `r_dynamicGlowBloom 0.0` — also let bright parts of the scene glow, not only glow-marked surfaces.

**Compare:** `r_bloomCompare` — 1 split screen (original glow left, HDR bloom right), 2 the original glow. The original glow is what `r_dynamicGlow` sets; with `r_dynamicGlow 0` the original side has none.

**How it works.** HDR bloom takes the image before tone mapping, blurs progressively smaller copies of its bright parts and adds them back. Glowing materials always contribute; other pixels contribute only above a brightness threshold, scaled by `r_bloomSceneIntensity`.

- `r_bloomThreshold 2.0` — brightness where scene bloom starts.
- `r_bloomKnee 0.5` — how softly it fades in around the threshold.

Design notes: [rend2-bloom.md](../rend2-bloom.md).

## Color grading

Applies a color filter to the whole image — warmer, colder, faded, more contrast — from a LUT file (`.cube`), the format photo and video editors export. Stock maps have no LUT, so nothing changes until you provide one. Needs `r_hdr 1` and `r_toneMap 1`.

**Cost:** light.

- `r_colorGrading 0` — 1 on; does nothing without a LUT.
- `r_colorGradingLUT ""` — a `.cube` file in the game data, for example `luts/warm.cube` in `base/` or a pk3. Empty uses the map's own LUT if it has one; `*identity` is neutral.
- `r_colorGradingIntensity 1` — 0 original colors … 1 the full LUT.

**Compare:** `r_colorGradingCompare` — 1 split screen (ungraded left), 2 ungraded.

**Mod authors:** put `maps/<map>.cube` next to the `.bsp`; it loads with the map unless the player sets `r_colorGradingLUT`.

**How it works.** A 3D LUT is a table that gives an output color for every input color. The renderer looks up each pixel of the final image in it and blends the result with the original by `r_colorGradingIntensity`.

## Motion blur

Fast camera turns and moving characters or sabers leave short streaks, like film. The HUD and menus stay sharp. The original game has no motion blur. Needs `r_hdr 1`.

**Cost:** medium; grows with resolution and sample count.

- `r_motionBlur 0` *(restart)* — 1 on.
- `r_motionBlurShutterAngle 180` — streak length. 180° blurs half a frame of motion (the film standard); 360 is longer, 90 shorter.
- `r_motionBlurReferenceFps 60` — the frame rate the shutter angle refers to, so the blur looks the same at 60 and 144 fps. 0 uses your real frame time: blur gets shorter as fps rises.
- `r_motionBlurMaxPixels 32` — longest streak, in pixels at 1080p.
- `r_motionBlurQuality 1` — 0 low, 1 medium, 2 high. Higher removes graininess in long streaks.
- `r_motionBlurViewModelScale 0.5` — blur on your own hands, weapon and saber in first person.

**Compare:** `r_motionBlurCompare` — 1 split screen (sharp left), 2 off.

**How it works.** Every pixel stores how far its surface moved since the last frame. The blur samples along that movement. A camera cut or teleport would produce a huge false streak, so a jump bigger than the limits below resets the history instead.

- `r_motionBlurSamples 0` — exact samples per pixel; 0 takes them from the quality.
- `r_motionBlurCutDistance 256`, `r_motionBlurCutAngle 75` — camera movement (units) or rotation (degrees) in one frame treated as a cut.
- `r_motionBlurShutterScale 1`, `r_motionBlurReset 0` — set by the game (longer exposure during Force Speed, reset after cuts). Don't change them.

**Debug**

- `r_motionBlurDebug 1..5` — velocity, camera velocity, object velocity, sample count, blur contribution.

Design notes: [rend2-motion-blur.md](../rend2-motion-blur.md).

## Anti-aliasing (SMAA)

Smooths jagged geometry edges.

**Cost:** light.

- `r_smaa 0` *(restart)* — **1** SMAA 1x. **2** SMAA T2x: also smooths over two frames, with a slight camera jitter.
- `r_smaaQuality 2` *(restart)* — 0 low, 1 medium, 2 high, 3 ultra.

**Compare:** `r_smaaCompare` — 1 split screen (no anti-aliasing left; `r_smaa 1` only, T2x jitters the whole view), 2 off.

**How it works.** SMAA finds edges in the finished image and blends pixels along them. It does not fix shimmering of very thin moving details, such as distant fences or grass.

## Linear lighting (experimental)

Every stock map stores its lighting in screen color values, and the game mixes textures and light in those values. Linear lighting converts everything to real light amounts first, as on HDR-lit maps. Overlapping lights, dynamic lights on lightmapped walls and soft gradients add up more naturally, but stock maps can look noticeably different.

Known side effects: sabers, blaster bolts, explosions and other effects drawn by game code look brighter and paler; the original dynamic glow gets weaker; 3D models in menus look darker.

- `r_linearLighting 0` *(restart)* — 1 on. Needs `r_hdr 1`, `r_toneMap 1` and `r_overBrightBits 0`; otherwise it is ignored.

No compare cvar: lightmaps and textures are converted when they load, so only `vid_restart` switches it.

Design notes: [rend2-tonemapping.md](../rend2-tonemapping.md#linear-lighting-r_linearlighting-experimental).

## Distortion color fringes

Screen-distortion effects (Force push, some weapon shots, refractive shaders) split red, green and blue slightly, like a cheap lens. Rend2 always drew fixed fringes; this makes them adjustable.

- `r_refractionChromaticAberration -1` — **-1** Rend2's original fixed fringes. **0** none. Above 0 the fringe width (0.05 subtle).

**Compare:** `r_refractionChromaticAberrationCompare 2` — Rend2's original fringes. No split screen: the refraction is computed per vertex.
