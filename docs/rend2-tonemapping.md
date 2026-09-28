# Rend2: exposure and tone mapping

This describes how rend2 turns the HDR scene into display values when `r_hdr 1` and `r_toneMap 1`.

## Rendering order

1. The scene is rendered into `tr.renderFbo` (`GL_RGBA16F`). Maps with external HDR lightmaps
   (`maps/<map>/lm_XXXX.hdr`) are lit in linear light: textures are decoded from sRGB. All other maps are lit
   in display-encoded ("gamma") space, as rend2 always did.
2. Auto exposure metering (`calclevels4x.glsl`): log2 average luminance of the raw HDR buffer, smoothed over
   time. With `r_autoExposure 0` a fixed level is used instead.
3. Tone map pass (`RB_ToneMap`, `glsl/tonemap.glsl`), SMAA neighborhood blending is done in the same pass
   (`r_smaa 1`). Output goes to the backbuffer.
4. Dynamic glow is composited on top, after tone mapping (unchanged).
5. Refractive/distortion surfaces are drawn on the backbuffer (`glsl/refraction.glsl`). They sample the HDR
   buffer and run the same output transform as step 3.
6. 2D.

Steps 3 and 5 share `glsl/output_transform.glsl`, so both always use the same exposure, operator and display
encoding. The file is inserted into the fragment shaders of both programs when they are built.

## Operators (`r_toneMapMode`)

| Value | Operator | Notes |
|---|---|---|
| 0 (default) | Legacy Rend2 filmic | John Hable's curve, white point at `toneMax - toneMin`. Everything more than about 2.2 stops (non-HDR maps) or 1 stop (HDR lightmapped maps) above the exposure target clips to white. Same math and order of operations as before; with `r_exposureCompensation 0` the image is identical. |
| 1 | ACES fitted | Stephen Hill's fit of the ACES RRT + sRGB 100 nit ODT. It is an approximation of the reference output transform, not the full ACES pipeline. Strong toe: deep shadows get much darker. |
| 2 | AgX-like | Compact approximation of Troy Sobotka's AgX: inset matrix, log2 encoding of middle grey -10/+6.5 stops, polynomial fit of the default contrast sigmoid (Benjamin Wrensch, MIT), inverse inset as outset. No LUT, no looks; not the exact Blender/OCIO AgX. |

Legacy works directly on the HDR buffer. ACES and AgX run this chain:

```
buffer * 2^(r_cameraExposure + r_overBrightBits)
-> scene-linear      (buffers of non-HDR maps are decoded with the sRGB EOTF, extended above 1.0)
-> exposure          (metered average -> toneAvg, limited by autoExposureMinMax, times 2^r_exposureCompensation)
-> operator          (display-linear, [0, 1]; nothing is clamped before this point)
-> sRGB encoding     (the only one)
-> color grading LUT (optional, see below)
```

On non-HDR maps the metering is done on display-encoded values; its ratio is converted to linear light with a
display gamma of 2.2.

Display values for grey buffer values on a non-HDR map with default settings and fixed exposure
(0.5 is where the exposure target lands):

| Buffer value | 0.1 | 0.25 | 0.5 | 1.0 | 2.0 | 4.0 |
|---|---|---|---|---|---|---|
| Legacy | 0.107 | 0.302 | 0.592 | 1.000 | 1.000 | 1.000 |
| ACES fitted | 0.014 | 0.125 | 0.404 | 0.809 | 0.969 | 1.000 |
| AgX-like | 0.098 | 0.284 | 0.528 | 0.787 | 0.948 | 0.998 |

ACES and AgX keep about 6 stops (non-HDR maps) or 5 stops (HDR lightmapped maps) of highlights above the
exposure target before reaching white. To match the middle grey of Legacy, use about
`r_exposureCompensation 1` with ACES and `0.5` with AgX (`1` on HDR lightmapped maps).

## Exposure

- `r_autoExposure 1` (default): auto exposure; `0`: fixed exposure.
- `r_exposureCompensation <EV>` (archived, -4..4, default 0): photographic stops in linear light, for every
  operator. The legacy operator applies it to the buffer, as `2^(EV / 2.2)` on non-HDR maps and `2^EV` on HDR
  lightmapped maps. Only used when tone mapping runs.
- `r_cameraExposure` (cheat): unchanged, multiplies the raw buffer.

Map parameters are unchanged and keep working:

- `q3gl2_tonemap <toneMin> <toneAvg> <toneMax> <autoExposureMin> <autoExposureMax>` in a shader (log2 values),
  worldspawn key `autoExposureMinMax`, cvars `r_forceToneMap*` / `r_forceAutoExposure*`.
- `toneAvg` and the auto exposure range are used by all operators. `toneMin` and `toneMax` are the black and
  white points of the legacy curve; ACES and AgX have their own toe and shoulder and ignore them.

## Debug views (`r_toneMapDebug`, cheat)

| Value | View |
|---|---|
| 1 | Split screen: Legacy on the left, `r_toneMapMode` on the right |
| 2 | Split screen: Legacy, ACES, AgX |
| 3 | Scene-linear input, no exposure, no tone curve |
| 4 | Exposure applied, no tone curve (clamped) |
| 5 | False color, stops relative to the exposure target: black < -6, violet < -4, blue < -2, teal < -0.5, grey +-0.5, green < 2, yellow < 4, orange < 6, red above. Magenta: the selected operator outputs white. |

All of these, `r_toneMapMode`, `r_exposureCompensation` and `r_autoExposure` switch instantly, without
`vid_restart`.

## A/B comparison

```
devmap mp/ffa3
cg_draw2D 0
r_autoExposure 0            // removes time-dependent adaptation
setviewpos <x> <y> <z> <yaw>
r_toneMapDebug 2            // Legacy | ACES | AgX in one frame
r_toneMapDebug 1; r_toneMapMode 2
r_toneMapDebug 0; r_toneMapMode 0; screenshot_png
r_toneMapMode 1; screenshot_png
r_toneMapMode 2; screenshot_png
```

To check that Legacy is unchanged, take the same `screenshot_png` with the previous renderer DLL and with
`r_toneMapMode 0`, and compare the files.

## Color grading (`r_colorGrading`)

An optional 3D LUT is applied by the shared output transform after tone mapping and after the sRGB
encoding, so it works the same for all operators and for refractive surfaces:

```
tone mapped display values (sRGB encoded) -> LUT -> mix(original, graded, intensity) -> screen
```

**LUT color space:** input and output are display-encoded sRGB values (IEC 61966-2-1 transfer function,
Rec.709/sRGB primaries), domain 0..1. This is what color grading tools export for an sRGB display, and it
is the only stage all operators share (Legacy on non-HDR maps has no display-linear stage). Grading is not
applied to the debug views 3 to 5 of `r_toneMapDebug`.

| cvar | default | |
|---|---|---|
| `r_colorGrading` | 1 | 0 = off, 1 = on |
| `r_colorGradingCompare` | 0 | 0 = off, 1 = split screen with the original on the left (requires grading enabled) |
| `r_colorGradingLUT` | "" | LUT to use, overrides the LUT of the map. Empty: use the LUT of the map |
| `r_colorGradingIntensity` | 1 | 0 = no grading, 1 = full LUT |

The LUT in use is `r_colorGradingLUT` when set, otherwise `maps/<map>.cube` next to the `.bsp` (for example
`maps/mp/ffa3.cube`, which works for existing maps without touching the BSP), otherwise none. All three cvars
take effect immediately. Without a LUT nothing is sampled and the image is unchanged.

File format: `.cube` 3D LUTs (`LUT_3D_SIZE` 2 to 128, red changing fastest), loaded from the game file
system (pk3 or base folder). `TITLE` and comments are ignored; `DOMAIN_MIN`/`DOMAIN_MAX` must be 0 and 1;
1D LUTs are not supported. A LUT that can't be loaded prints a warning once and grading stays off.
`r_colorGradingLUT *identity` uses a built-in 33x33x33 identity LUT for testing.

Precision: LUTs are stored as 16 bit per channel 3D textures and sampled at texel centers with trilinear
filtering. An identity LUT changes values by less than 1e-5, so the 8 bit output differs by at most one
step, only for values sitting exactly on a rounding boundary.

`tools/rend2/make_luts.py <dir>` writes `identity.cube` and a deliberately strong `test_look.cube`:

```
python tools/rend2/make_luts.py base/luts
r_colorGradingLUT luts/test_look.cube
r_colorGradingCompare 1                     // original | graded
r_colorGradingIntensity 0.5
r_colorGradingLUT luts/identity.cube // should look unchanged
r_colorGradingLUT ""                 // back to the map LUT, if any
```

## Linear lighting (`r_linearLighting`, experimental)

Maps without HDR lightmaps are lit in display-encoded ("gamma") space: textures, lightmaps and colors are
multiplied and added as sRGB values. `r_linearLighting 1` (archived, latched: `vid_restart` or map reload)
lights them in linear light instead, using the same path as HDR lightmapped maps. Needs `r_hdr 1`,
`r_toneMap 1` and `r_overBrightBits 0`, otherwise it is ignored with a warning. HDR lightmapped maps are
not affected.

Decoded from sRGB when a map is loaded or drawn:

- textures of map and model shaders (sRGB texture formats, like on HDR lightmapped maps), skyboxes, weather
- lightmaps (float lightmaps on the CPU, 8 bit lightmaps through an sRGB texture format)
- vertex colors (also used by surface sprites), flare colors, the volumetric fog light grid
- light grid lighting of entities (after the usual minimum light and clamp)
- `fogParms` colors, `rgbGen const`, `rgbGen wave` and light style colors of map and model shaders
- sun colors and the map light scale of `q3gl2_sun`

The tone parameters of the map (`q3gl2_tonemap`, `autoExposureMinMax`, `r_force*`) and the fixed exposure
level are converted to linear light as well, so exposure stays about the same.

Not covered yet:

- Shaders registered by cgame (`RE_RegisterShader`: sabers, blaster bolts, explosions and other effects, HUD)
  are drawn with their sRGB values as if they were linear, so effects look brighter and less saturated.
- Dynamic light colors (set by cgame) and `rgbGen entity` colors are not decoded.
- Dynamic glow is still composited after tone mapping, so glow gets weaker.
- 3D models drawn by menus (`RDF_NOWORLDMODEL`) after the world use decoded textures without encoding and
  look darker.

## Notes

- Only maps with HDR lightmaps are lit in linear light by default. On other maps the operators decode the
  buffer at the tone map input; lighting itself is still done in gamma space, unless `r_linearLighting` is set.
- With `r_toneMap 0` or `r_hdr 0` nothing of the above runs (unchanged).
- `r_externalGLSL`: `tonemap.glsl` and `refraction.glsl` now need the functions from `output_transform.glsl`.
