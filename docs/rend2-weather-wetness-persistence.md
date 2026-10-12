# Persistent world wetness

`r_weatherWetnessPersistence 1` replaces the instantaneous water amount on
static world geometry with a low-frequency temporal state. It does not replace
the existing wet material model: `weatherResponse`, material-class darkening,
roughness, normal flattening, puddles, and runoff still consume the resulting
0..1 water amount.

Moving entities deliberately keep the previous instantaneous rain response.
The first milestone has no moving-character atlas or entity history.

## State and update

Two `RG32F` textures are aligned with the existing weather projection and
ping-ponged by a GL 3.2 fullscreen pass. `R` stores water amount and `G` stores
a copy of weather depth. Packing depth with state lets `lightall` preserve the
existing roof/exposure test without consuming another texture unit.

The update uses fixed simulation ticks derived from renderer time, not one step
per rendered frame:

```
water = clamp(water + rainIntensity * accumulationRate * dt
                    - dryRate * drainage * dt, 0, 1)
```

Weather-depth gradients provide the optional steep-surface drainage proxy.
The static exposure test and geometric facing are applied when the world is
shaded, so covered geometry is not wetted and the same physical amount remains
material-independent. Rain subtype is already available at the update point;
an acid-specific rate scale can be added without changing the stored state.

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
| `r_weatherWetnessSteepDryScale` | `2` | Drying multiplier at steep weather-depth gradients. |
| `r_weatherWetnessPersistenceDebug` | `0` | `1` shows stored/exposed water amount in blue. |

## Manual validation

Enable `r_weatherWetness 1`, `r_weatherWetnessPersistence 1`, restart the
renderer, then use `r_weatherWetnessPersistenceDebug 1` while checking:

- remain under a roof during rain: covered surfaces stay dry;
- wet a courtyard, stop rain, leave and return: water remains and decays;
- compare a steep surface with a flat floor: the steep projection dries faster;
- observe puddles after rain stops: ripples stop immediately, the water does not;
- load another map: the state starts clear;
- compare with the master set to `0`: behavior is the original instantaneous path.
