# Coherent acid-rain optical profile

`acidrain` remains a rendering-only weather subtype. It does not add or alter
gameplay damage. `RE_WorldEffectCommand` still supplies the legacy weather
colour, particle count and fall parameters; Rend2 resolves those inputs once
per scene into `precipitationState_t::optical`.

The resolved profile owns:

- particle/splash tint (in the existing `precipitationState_t::tint`);
- absorption/scattering tint used by lens water;
- wet-film, puddle and runoff optical tints;
- the froxel atmospheric-medium tint;
- the persistent-wetness lifetime multiplier.

Material tints are intentionally near neutral. They multiply transmitted
substrate colour and a very small part of the dielectric specular response;
they do not replace albedo. The legacy puddle path uses an even smaller tint
weight. The layered water-film path can use the full resolved (already mild)
optical tint.

## Controls

| Cvar | Default | Meaning |
| --- | --- | --- |
| `r_acidRainVisuals` | `0` | Enables the coherent profile. Off preserves legacy particles, splashes, lens profile and the previous absence of acid rain haze. |
| `r_acidRainTint` | `0.78 1.00 0.62` | Linear RGB optical tint. An invalid value falls back to the normalized legacy weather colour. |
| `r_acidRainStrength` | `1` | Profile strength, clamped to `0..2`. |

Existing `r_weatherWetnessAcidAccumulationScale` and
`r_weatherWetnessAcidDryScale` remain low-level persistence controls. The
resolved profile lifetime multiplies them rather than replacing them.

Use `precipitation_info` after a main view has rendered to inspect the subtype,
consumer mask and every resolved optical channel. Normal wetness, puddle,
runoff, splash, lens and volumetric debug views continue to show their local
masks/contributions.

## Stock asset audit

The official Jedi Academy Steam `assets0.pk3` through `assets3.pk3` were
audited read-only on 2026-10-13. Weather cannot be found by searching for the
renderer token alone: the game converts an `fx_rain` entity's ACID spawnflag
(`8`) into `acidrain`. Parsing the RBSP entity lumps found one stock use:
`maps/vjun1.bsp`, with count `2000` and spawnflags `8`. The other stock rain
entities are `t1_rail` (heavy plus outside shake) and `yavin2` (light).

`shaders/vjun.shader` contains no acid, rain, fog, steam or smoke response. The
only acid-specific stock visual is `effects/world/acid_fizz.efx`: a small,
short-lived `gfx/misc/steam` particle attached by existing game code to local
acid impacts. It is evidence for localized reaction vapour, not a reason to
fill the map with fog. The restrained optical defaults were therefore tuned
for Vjun's dark metal/stone palette, and this change adds no fog or FX emitter
assets.

If a mod map needs visible reaction vapour, author it as a local map FX/fog
emitter in an `env.json` overlay PK3. Do not add a global toxic fog response and
do not modify original asset packs.
