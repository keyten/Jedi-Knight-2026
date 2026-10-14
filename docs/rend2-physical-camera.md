# Rend2 physical camera and exposure

The physical camera is an opt-in exposure model layered onto the existing HDR,
bloom, ACES/AgX, color-grading, and auto-exposure pipeline. It does not replace
any of those systems.

## Modes and controls

`r_cameraMode` selects the camera:

| Value | Mode | Exposure source |
| --- | --- | --- |
| 0 | legacy (default) | Existing `r_autoExposure`, `r_cameraExposure`, map tone levels, and `overbrightBits`; behavior is unchanged |
| 1 | physical manual | `r_cameraISO`, `r_cameraAperture`, and `r_cameraShutterSeconds` |
| 2 | physical auto | Adapted log-average scene luminance produces EV100, bounded by `r_cameraAutoEVMin/Max` |

The manual defaults are ISO 100, f/16, and 0.008 seconds (1/125 second), the
classic daylight EV15 combination. Physical auto is the gameplay/default-map
path: users do not need to manipulate three camera controls. The manual values
remain available for cinematics and are reported by camera debug even in auto.

`r_exposureCompensation` is the sole artistic bias in both physical modes. It
is measured in stops: +1 doubles scene exposure and -1 halves it.
`r_cameraExposure` retains its exact legacy meaning in mode 0 and is ignored in
physical modes, with a one-time warning when it is nonzero. Migration is:

```
legacy r_cameraExposure X  ->  physical r_exposureCompensation X
```

Do not migrate a value already present in `r_exposureCompensation`, since that
would double the bias.

## EV100 convention

Manual EV100 is exactly:

```
EV100 = log2(N^2 / t * 100 / ISO)
```

where `N` is the f-number and `t` is shutter time in seconds. Doubling ISO or
shutter duration adds one stop of exposure; opening the aperture by sqrt(2)
adds one stop.

Physical exposure uses the ISO 12232 saturation-based speed convention used by
the Frostbite/Filament family of real-time camera models:

```
physicalExposure = 1 / (1.2 * 2^EV100)
sceneExposure = physicalExposure / r_pbrPhysicalToScene
finalSceneExposure = sceneExposure * 2^r_exposureCompensation
```

The 1.2 is not a fitted map constant. ISO 12232 gives a saturation-based speed
constant of 78; using a representative lens transmittance `q = 0.65` produces
`78 / (100 * q) = 1.2`. The renderer's only physical-to-storage calibration is
the already documented `r_pbrPhysicalToScene` (default 0.0001 scene units per
cd/m2-like scene luminance). This inverse is applied once in the camera.

Physical auto uses the ISO 2720 reflected-light meter constant `K = 12.5`:

```
targetEV100 = clamp(log2(L * 100 / 12.5), minEV100, maxEV100)
```

`L` is adapted log-average scene luminance in cd/m2 after undoing
`r_pbrPhysicalToScene`. At the metered EV, a reference card luminance is
`L = 12.5 * 2^EV100 / 100`; the exposed linear value is therefore exactly
`12.5 / (100 * 1.2) = 0.1041667`. This is the numeric grey-card calibration;
ACES or AgX then maps that scene-linear value to the display. An 18% diffuse
reflectance card is the test surface, while its actual luminance is determined
by the calibrated incident light (`L = 0.18 * E / pi` for a Lambertian card).
For example, 1745.33 lux produces 100 cd/m2, EV100 9.6439, and the same
0.1041667 exposed value.

The levels texture retains the existing temporal adaptation. Physical meters
encode +/-24 log2 stops rather than the legacy +/-10 range, which covers dark
night scenes and very bright emitters. Legacy metering keeps the original
range and encoding exactly. The physical EV bounds are global camera limits,
not per-map brightness metadata.

## Pipeline and buffer encoding

The order is:

```
scene-linear HDR -> motion blur -> bloom extraction/blur -> bloom composite
-> photographic exposure -> ACES/AgX/legacy curve -> display encoding
-> color grading
```

In the HDR bloom path (`r_bloom 1`), the dedicated emissive bloom and optional
scene bloom are extracted before exposure and composed with the scene once,
then receive the same camera exposure. `r_bloomThreshold` therefore remains a
physical scene-relative threshold (in stored scene units), not an
exposure-relative display threshold. Exposure is never applied separately to
bloom or emissive. The old `r_bloom -1` dynamic-glow path deliberately remains
a display-space compatibility composite after tone mapping; it is not claimed
to be a physical optical model. Use `r_bloom 1` for calibrated camera tests.

`overbrightBits` is historical buffer encoding. The renderer still decodes it
so old maps remain compatible, but records that correction separately from
EV100. It is not a camera control and never appears in the photographic
equations. The main HDR target is RGBA16F. With the default scene scale its
65,504 maximum represents about 655 million physical units, so fixed scaling
has adequate headroom and pre-exposure is not currently justified. Camera
parameters and scene scaling are separate, allowing pre-exposure to be added
later without changing ISO/aperture/shutter semantics.

## Motion blur and depth of field

`r_motionBlurPhysicalShutter 1` optionally uses
`r_cameraShutterSeconds` as the velocity-buffer exposure duration in either
physical camera mode. At instantaneous frame rate `fps`, the equivalent angle
is:

```
shutterAngle = shutterSeconds * fps * 360
```

The implementation uses duration divided by the measured frame interval, so
the resulting screen-space blur is not frame-rate dependent. Durations longer
than one frame use the available one-frame velocity approximation and retain
the existing 4x safety cap. With coupling off, the artistic
`r_motionBlurShutterAngle` and reference-FPS behavior is unchanged.

The current bokeh feature is an authored whole-frame blur (`blurFactor`), not a
depth-aware lens model. Aperture is intentionally not connected to it. The
f-number cvar is a stable input for a future physical DOF implementation.

## Debug and calibration validation

`r_cameraDebug 1` prints once per second: mode, ISO, f-number, shutter seconds,
manual EV100, exposure compensation, measured luminance, auto target EV100,
and the final scene exposure multiplier. This debug readback intentionally
stalls the GPU and should be disabled for performance captures.

`r_toneMapDebug 5` is the exposure-zone/false-color view. In physical modes its
zero zone is the calibrated 0.1041667 reference value; other colors are stops
relative to it, and magenta indicates display white/clipping. Debug modes 1/2
continue to compare the legacy curve, ACES, and AgX under the same exposure.

Visual validation scene checklist:

1. Place an 18% linear-grey Lambert card beside a white card and calibrated
   point and area lights. Confirm the console luminance and zero-zone result.
2. Check a daylight setup near EV15 (ISO 100, f/16, 1/125 second), a typical
   indoor setup near EV8-10, and a night setup below EV5 in manual mode.
3. In physical auto, walk from a dark indoor room into daylight. Confirm a
   smooth EV transition within `r_cameraAutoEVMin/Max`, without a brightness
   discontinuity in bloom or emissive surfaces.
4. Repeat with `r_toneMapMode 1` (ACES) and `2` (AgX), then use mode 0 for the
   legacy A/B. Stock maps require no camera metadata; physical auto meters
   their calibrated/migrated scene values directly.

The unit tests in `tests/rend2_camera_tests.cpp` cover EV100, ISO, aperture,
shutter duration, compensation stops, and the grey-card calibration.
