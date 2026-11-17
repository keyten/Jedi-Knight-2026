# Voronoi stone relief

Enable the material classifier and the OpenGL 4.3 renderer before enabling
the effect. All three settings are latched.

```cfg
seta r_gl43 1
seta r_materialClassification 1
seta r_voronoiFracturing 1
vid_restart
```

`r_voronoiFracturing` defaults to `0`. The live comparison
`r_voronoiFracturingCompare 1` draws the original scene on the left and the
relief on the right. Set `r_compareSplit` to move the divider.
Mode `2` shows the original scene throughout the view. Mode `0` restores
the configured effect.

## Profiles

The effect uses classified rock and masonry with opaque, single-pass lightmap
or vertex-lit BSP shaders and static diffuse UVs. Mixed and conflicting PBR
evidence, tagged marble, silhouette POM and special material stages use their
ordinary rendering paths.

The renderer derives a repeating relief texture from the existing diffuse image
at registration. A groove requires brighter material on both sides, which
reduces the influence of broad painted lighting gradients. Sparse warped Worley
fractures add chips inside the material. The mipmapped texture stores depth and
two UV slopes so lighting can sample all three in one lookup.

| Shader keyword | Relief |
| --- | --- |
| `stoneRelief auto` | Use the material classifier. Masonry selects masonry, the sandstone name selects strata and other rock selects rock. |
| `stoneRelief masonry` | Recess the painted joints with a wider search for the two lips. |
| `stoneRelief strata` | Estimate the dominant layer orientation from texture gradients and weight grooves by continuity along the layer. |
| `stoneRelief rock` | Use narrow, local two-sided grooves in any of four directions. |
| `stoneRelief off` | Use the original shader. |

Add the keyword at shader scope in a `.shader` file. The classification and
eligibility checks still apply to overrides. Restart the renderer after edits.

## Shading and geometry

By default the effect adds bounded tangent-space normal detail and shallow cavity shading in
the material pass. The depth, motion and fog passes use their base programs.
The shader fades relief from 200 to 390 game units and skips the relief lookup
outside that range.

`r_voronoiFracturingGeometry 1` also enables experimental close-range mesh
displacement. It can be toggled live. Render depth and velocity use the same
displacement. The per-triangle constants are evaluated once per patch, and
tessellation falls to level 1 beyond 300 game units. Displacement goes to zero
along original triangle edges and fades over 140–290 units. Projected blob
shadows (`r_shadows 4`) use the original rendering path with this mode.

Photo references include [Weir Farm's dry-stacked fieldstone](https://www.nationalparkstraveler.org/2021/01/building-walls-weir-farm-national-historical-park)
and [the irregular mortar joints at Hessenpark](https://www.publicdomainpictures.net/en/view-image.php?image=251766&picture=irregular-medieval-stone-wall).

