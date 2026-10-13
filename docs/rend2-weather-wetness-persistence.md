# Persistent world wetness

`r_weatherWetnessPersistence 1` replaces the instantaneous water amount on
static world geometry with a low-frequency temporal state. It does not replace
the existing wet material model: `weatherResponse`, material-class darkening,
roughness, normal flattening, puddles, and runoff still consume the resulting
0..1 water amount.

Moving entities deliberately keep the previous instantaneous rain response.
The first milestone has no moving-character atlas or entity history.

## State and update

Two `RGBA32F` textures are aligned with the existing weather projection and
ping-ponged by a GL 3.2 fullscreen pass. `R/G` store retained water for flat
and steep surfaces, `B` stores a copy of weather depth, and `A` optionally
retains recent world runoff activity. Packing depth and runoff history with
state lets `lightall` preserve the existing roof/exposure test and persistent
trails without consuming another texture unit. The real geometric normal of
each shaded surface blends the flat/steep histories, so drainage is not
inferred from a weather-depth gradient.

The update uses fixed simulation ticks derived from renderer time, not one step
per rendered frame:

```
water = clamp(water + rainIntensity * accumulationRate * dt
                    - dryRate * drainage * dt, 0, 1)
```

The static exposure test and geometric facing are applied when the world is
shaded, so covered geometry is not wetted and the same physical amount remains
material-independent. Acid rain applies its own accumulation and drying
multipliers; the last active acid profile remains selected while that water
dries after rain stops.

State is cleared on map load, renderer restart, backwards time, and long time
jumps. Rain stopping only removes deposition; drying continues. Puddle ripples
stop with current rain while the puddle itself persists until its water dries.

## Cvars

| Cvar | Default | Meaning |
|---|---:|---|
| `r_weatherWetnessPersistence` | `0` | Master switch; latched. At `0`, no state textures, update pass, or persistence shader define exist. |
| `r_weatherWetnessAccumulationRate` | `0.20` | Water deposited per second at normalized full rain. |
| `r_weatherWetnessDryRate` | `0.02` | Base water removed per second (about 50 seconds from full to dry). |
| `r_weatherWetnessUpdateHz` | `10` | Simulation frequency, clamped to 1–60 Hz. |
| `r_weatherWetnessResolution` | `256` | Square state resolution, clamped to 64–1024; latched. |
| `r_weatherWetnessSteepDryScale` | `2` | Drying multiplier for steep surfaces, selected using the actual geometric normal. |
| `r_weatherWetnessAcidAccumulationScale` | `1` | Acid-rain accumulation multiplier. |
| `r_weatherWetnessAcidDryScale` | `1` | Acid-rain drying multiplier. |
| `r_weatherWetnessPersistenceDebug` | `0` | `1` shows stored/exposed water amount in blue. |
| `r_weatherRunoffTrailPersistence` | `0` | Seconds world runoff trails remain after water passes; `0` disables them. |
| `r_weatherRunoffTrailDebug` | `0` | `1` shows retained history; `2` shows the final geometry-gated trail mask. |

## Manual validation

Enable `r_weatherWetness 1`, `r_weatherWetnessPersistence 1`, restart the
renderer, then use `r_weatherWetnessPersistenceDebug 1` while checking:

- remain under a roof during rain: covered surfaces stay dry;
- wet a courtyard, stop rain, leave and return: water remains and decays;
- compare a steep surface with a flat floor: the steep projection dries faster;
- observe puddles after rain stops: ripples stop immediately, the water does not;
- load another map: the state starts clear;
- compare with the master set to `0`: behavior is the original instantaneous path.

## Runtime validation and defaults

The automated real-map harness is `tools/rend2/test_weather_wetness_runtime.py`.
It was run on `yavin1b` with the Intel OpenGL driver and the Release SP renderer.
Captured debug/shaded frames verified the covered gorge stayed dry, wetness
remained after `r_we clear`, survived leaving and returning to the second
courtyard, selected steep surfaces from geometry normals, continued to gate
puddles, and cleared after `devmap yavin1b`.

GPU timestamp medians for one simulation update were 0.095 ms at 128, 0.197 ms
at 256, and 0.310 ms at 512 (tested at 5/10/15 Hz respectively). This is about
0.00048/0.00197/0.00464 ms per rendered frame when amortized at 60 FPS. The
128 texture showed noticeably coarser transitions around overhang edges in the
debug view; 256 did not show objectionable boundaries in the shaded view.
Therefore the defaults remain 256 and 10 Hz.
