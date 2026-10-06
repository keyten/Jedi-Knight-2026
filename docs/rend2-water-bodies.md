# Water bodies and dynamics metadata

`WaterBody` is renderer metadata built during BSP loading. It does not add a
gameplay volume and does not feed the current water shader. The optical medium
still comes from `Liquids` and the existing water/slime/lava settings.

## Identity and grouping

Body IDs start at 1 in BSP surface order on each map load. They are stable for
that map and asset set, but are diagnostic selectors, not durable authoring
keys. The loader groups candidate surfaces in the same BSP model and liquid
class when they touch in space and are nearly coplanar. Surfaces belonging to
the same liquid brush, or to touching liquid brush bounds, join regardless of
shader or orientation. Separate brush models remain separate because they may
move. A shared shader by itself never joins bodies. Unlinked waterfall patches
can therefore remain separate bodies. A brush without a linked rendered surface
is still reported by `r_waterInfo`; the body audit does not invent a visible
surface for it.

The representative plane comes from the first surface. Area is triangle area;
depth is the mean and maximum height of linked brush bounds. These are
approximations for patch surfaces and nonaxial brushes. No reliable indoor or
outdoor classification is inferred from map names or BSP contents. The stock
audit records exposure as unknown.

## Dynamics resolution

The independent dynamics set is `generic_water`, `still_pool`, `calm_water`,
`lake`, `slow_stream`, `fast_stream`, `slime`, `waterfall`,
`heavy_waterfall`, and `no_water_dynamics` for lava. Each carries ambient
wave amplitude, wavelength, choppiness, speed, micro normal, flow,
interaction damping, wake, foam, shoreline, and rain response parameters.
They are metadata for future dynamics work and currently do not affect pixels.

Resolution is deliberately conservative: a matching map `WaterBodies` rule
wins; slime or lava semantics, the stock `water2_still` definition, or a
vertical `h_evil/wfall` patch provide trustworthy stock decisions; everything
else falls back to `generic_water`. Legacy `tcMod scroll` vectors, turb and
stretch waveforms, and vertex wave deforms are retained for diagnostics and
future profile resolution. Texture scroll direction is not treated as a world
space flow direction. In particular `h_evil/lakewater` has three scroll stages
with `(0.03,-0.13)`, `(-0.03,-0.1)`, and `(0,-0.17)`; all three remain visible
in the audit and command output.

## Map authoring

Add an optional `WaterBodies` array to `cubemaps/<map>/env.json`, alongside
`Cubemaps`, `Liquids`, and `LensWaterEmitters`:

```json
{
  "WaterBodies": [
    {
      "Selector": {
        "Shader": "textures/h_evil/lakewater",
        "Point": [128, 256, -64]
      },
      "DynamicsProfile": "lake",
      "Flow": [0.1, 0],
      "WaveMultiplier": 1.2,
      "FoamMultiplier": 0.8,
      "InteractionMultiplier": 1
    }
  ]
}
```

Selectors may contain `Shader`, `ShaderPrefix`, `BodyId`, `Point` or `Origin`,
and `Bounds` with `Mins` and `Maxs`. Fields in one selector are combined.
`BodyId` is best used for a diagnostic session; prefer a point or bounds for
shipped data. Later matching rules win. Flow is a world XY direction. The
three multipliers default to 1. The fields are parsed at map load and do not
alter the optics profile or current rendering.

The project stock overrides are
[`water_body_overrides.json`](../tools/rend2/water_body_overrides.json). They
sets the six separate `t2_rancor` pools to `still_pool` without affecting the
same `water_1` shader on `vjun2`. A point selector marks the large upper
`t3_hevil` lake as `lake`, leaving two distant `lakewater` bodies at other
heights on the fallback. This point lies in the lake used by the existing
water surface runtime test; the decision does not follow the map or shader
name alone. Run
`python tools/rend2/build_water_body_overlay.py <game-base> <output.pk3>` to
build a small overlay PK3. The script copies each existing map `env.json` into
the overlay before adding `WaterBodies`, so its cubemaps and other settings
survive. Install that overlay after the stock PK3s. It does not edit them.

## Commands

- `r_waterBodies`: ID, bounds, area, depth, class, optical and dynamics
  profiles, decision source, shaders, and legacy stage motion.
- `r_waterBodies dump`: JSON Lines records with surface and brush IDs, full
  legacy motion, plane, flow, multipliers, load time and estimated persistent
  memory. Each console line is a complete JSON object.
- `r_waterBodies draw`: toggles colored bounds, the representative plane and
  its normal, an authored flow line, and a camera-facing three-digit body ID
  followed by the numeric dynamics profile code. The command listing maps
  profile codes to names and shows each body's color. The draw is off by default.

## Installed stock audit

[`rend2-water-body-stock-audit.json`](rend2-water-body-stock-audit.json) is the
full machine readable audit of the installed PK3s. It contains every BSP
candidate surface and liquid brush, body membership, bounds, area, orientation,
aspect, approximate depth, contents and surface flags, fog status, every stage
directive from the winning shader definition, and shader provenance. It also
marks shaders shared by physically distinct bodies. The audit script records
indoor/outdoor as unknown rather than guessing from map names. Its group
decisions reproduce the renderer's conservative geometry rule; patch area is
approximate because the script measures control points.

| Map | Bodies | Dynamics decisions |
| --- | ---: | --- |
| `t2_rancor` | 6 | 6 explicit `still_pool` |
| `t2_trip` | 2 | 2 fallback `generic_water` |
| `t3_bounty` | 2 | 2 stock `still_pool` |
| `t3_hevil` | 11 | 8 stock `waterfall`, 1 explicit `lake`, 2 fallback `generic_water` |
| `yavin1` | 5 | 1 stock `waterfall`, 4 fallback `generic_water` |
| `yavin1b` | 7 | 2 stock `waterfall`, 5 fallback `generic_water` |
| `yavin2` | 2 | 2 fallback `generic_water` |
| `vjun1` | 1 | 1 slime |
| `t2_port` | 10 | 10 fallback `generic_water` |
| `vjun2` | 1 | 1 fallback `generic_water` |

The full audit also includes lava maps `kor1`, `kor2`, `taspir1`, `taspir2`,
`mp/duel5`, `mp/ffa5`, and `mp/siege_korriban`. `t1_fatal` has water-like
material names but no qualifying liquid body. The `h_evil/lakewater` shader
forms 3 bodies on `t3_hevil`, 3 on `yavin1`, and 4 on `yavin1b`. One
`t3_hevil` body has the spatial override above; the others remain fallback
until their physical roles are authored. They are not forced into a shared
`lake` profile by name.

The runtime command reports the measured body build cost in milliseconds and
an approximate persistent memory count. The extra grouping compares only
liquid candidate surfaces, so its comparison count is quadratic in the number
of liquid surfaces, not in the total BSP surfaces. The persistent memory is
roughly one body record per connected component plus lists of surface IDs,
brush IDs, shaders and legacy stage motion. The full renderer also retains the
existing water classification records; those are not counted by the new
estimate. In the checked SP renderer, `t2_rancor` reported 3 ms and about
2.8 KiB for six bodies; `t3_hevil` reported 1 ms and about 5.1 KiB for
eleven bodies. Millisecond timing has coarse resolution and depends on the
machine and map cache. Build both SP and MP renderer DLLs after changing this
code.
