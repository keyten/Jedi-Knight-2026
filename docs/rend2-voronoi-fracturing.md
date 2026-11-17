# Voronoi stone relief

Enable the material classifier and the OpenGL 4.3 renderer before enabling
the effect. All three settings are latched.

```cfg
seta r_gl43 1
seta r_materialClassification 1
seta r_voronoiFracturing 1
vid_restart
```

`r_voronoiFracturing` defaults to `0`. The live
`r_voronoiFracturingCompare 1` draws the original scene on the left and the
relief on the right. Set `r_compareSplit` to move the divider.
Compare mode `2` draws the original scene on both sides and mode `0`
returns to the configured effect.

The effect uses confidently classified rock and masonry with opaque, single-pass
lightmap or vertex-lit BSP shaders and static diffuse UVs. Mixed and conflicting
PBR evidence, tagged marble, silhouette POM, and special material stages stay on
their ordinary rendering paths. Projected blob shadows (`r_shadows 4`) use the
ordinary path because their depth test assumes the original surface.

At registration, each eligible base texture gets a repeating, mipmapped height
field. Low-frequency luminance and local dark grooves guide the block heights
and joints. Warped Worley cells add shallow fractures and per-cell variation.
Two higher-frequency bands roughen and round fracture lips. The original
diffuse UVs index the same field in the tessellation evaluation and lighting
shaders, with lower procedural contrast on masonry.

The tessellation levels fall with camera distance. The relief fades out over
200–390 game units. A shallow inward displacement fades to zero along original
triangle edges to preserve shared edges and T-junctions. The lighting shader
adds surface gradients and subtle cavity shading across these boundaries.
The view depth and velocity passes evaluate the same displacement. Shadow
cameras and collision use the BSP geometry.

Photo references include [Weir Farm's dry-stacked fieldstone](https://www.nationalparkstraveler.org/2021/01/building-walls-weir-farm-national-historical-park)
and [the irregular mortar joints at Hessenpark](https://www.publicdomainpictures.net/en/view-image.php?image=251766&picture=irregular-medieval-stone-wall).

