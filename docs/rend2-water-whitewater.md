# Rapids and stream whitewater

`r_waterWhitewater 1` adds advected bulk aeration to modern local water bodies.
It shares the body-local field and resolved flow used by persistent foam, but
keeps a separate concentration: whitewater changes the water's scattering,
transmission, roughness and normal variance; persistent foam remains a lit
surface layer on top. Neither contribution is emissive.

The source combines conservative signals already available to the renderer:

- the `slow_stream`, `fast_stream`, `waterfall`, and `heavy_waterfall` dynamics profiles;
- resolved flow speed, average shallow depth, and surface slope;
- the rasterised body boundary in the downstream direction;
- object/intersection, impact-pool, and waterfall source channels;
- interaction-field disturbance energy.

The default threshold leaves `slow_stream` effectively clear. `fast_stream`
forms moderate, broken-up aeration when flow/shallow conditions support it,
while waterfall profiles and explicit impact sources are strong. Fast-stream
profile aeration does not continuously paint persistent foam over the whole
body: foam still needs its existing local sources or the separately controlled
whitewater-to-foam injection.

Both concentrations use the same semi-Lagrangian body-UV advection, mild
diffusion and mask. They therefore form near a source, stretch downstream,
disperse and decay in water space instead of remaining fixed on screen. The
implementation uses procedural breakup and does not add or modify PK3 assets.

## Controls

| Cvar | Default | Meaning |
| --- | ---: | --- |
| `r_waterWhitewater` | 0 | Master switch; latched, requires `r_waterSurface 1` and `vid_restart` |
| `r_waterWhitewaterThreshold` | 0.45 | Formation threshold applied to the combined source |
| `r_waterWhitewaterStrength` | 1 | Bulk optical/normal strength |
| `r_waterWhitewaterFoam` | 0.35 | Extra persistent foam injected by formed whitewater |
| `r_waterWhitewaterDecay` | 1.1 | Bulk-aeration decay per second |
| `r_waterWhitewaterDebug` | 0 | `1` flow, `2` turbulence source, `3` final whitewater, `4` foam sources |

The field uses `r_waterFoamResolution`, `r_waterFoamAdvection`, and
`r_waterFoamDiffusion`. Enabling whitewater allocates the field even when
`r_waterFoamField` is off; in that case its persistent-foam channel is not
rendered. Source mode 2 shows the newly evaluated turbulence source, while
mode 4 shows the independent foam-source texture.
