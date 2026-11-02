# Smoke and effect authoring

[All rendering features](../rendering-features.md)

Controls marked `vid_restart` need a renderer restart. Debug views use 0 to return to normal unless stated otherwise. Performance labels are rough relative costs, not benchmark guarantees.

SP means single-player; MP means multiplayer.

## Smoke and particle volumes

*Requires `r_volumetricFog 2`, `r_volumetricParticles 1` and matching updated game/client modules, because particles must submit their 3D density to the renderer.*

Gives smoke real depth: lamps and sabers color it, beams enter it, and the background is hidden by the thickness along the view. Original smoke is mostly a flat transparent sprite. Medium to heavy performance cost when large clouds overlap.

**Main controls**

- `r_volumetricParticles 0 | 1` — disable / submit authored volumetric FX media.
- `r_volumetricParticlesMax <count>` — maximum important particles uploaded per frame.
- `r_volumetricParticlesScale <value>` — extinction multiplier.
- `r_volumetricParticlesHistory <value>` — previous-frame retention where particle density changes.
- `fx_physicalization` — 0 off, 1 all classified effects, 2 exact opt-in list, 3 all except opt-out list. Default `0`.
- `fx_physicalizationAdaptive` — Adapt the size, softness and density of accepted automatic smoke. Default `0`.
- `fx_physicalizationAggregate` — Experimental: combine exactly overlapping automatic non-glowing clouds. Default `0`.
- `fx_physicalizationComposite` — Allow classified smoke within composite explosions. Default `0`.
- `fx_physicalizationEmission` — Enable emission for exact stock flame/fireball profiles. Default `0`.
- `fx_physicalizationOptIn` — Exact normalized EFX names for mode 2, separated by spaces, commas or semicolons. Default `""`.
- `fx_physicalizationOptOut` — Exact normalized EFX names excluded by mode 3. Default `""`.
- `fx_physicalizationSources` — Enable the independently gated automatic physical-source submissions. Default `0`.
- `fx_physicalizationStrength` — Strength of automatically added smoke density, 0..16. Default `1`.

**Debug**

- `r_volumetricParticlesDebug 0 | 1` — print culling statistics periodically.
- `r_volumetricFogDebug 26..28` — particle density, temporal-history reduction and proxy bounds.
- `fx_physicalizationDebug` — Print classification and submitted automatic-media diagnostics. Default `0`.
- `fxaudit` — fxaudit <efx>: inspect physicalization eligibility; developer 1, SP.
- `fxsources` — Inspect the last automatic FX source submissions; developer 1, SP/MP.
- `r_volparticles` — Print particle-media submission, culling and upload limits.

### [Autodetectable] Existing smoke effects

Choose `fx_physicalization 1` for all classified effects, `2` for an exact opt-in list, or `3` for all except an opt-out list. Mode 0 disables automatic additions. The lists select eligible effects; they do not force an unknown effect to become smoke.

Composite smoke, automatic emission and additional light-source submissions have separate `fx_physicalizationComposite`, `fx_physicalizationEmission` and `fx_physicalizationSources` switches. They are opt-ins, not a replacement of every stock effect.

### [For mod authors] Authored particle media

Add this to an EFX `Particle` or `OrientedParticle`:

~~~text
volumetricMedia
{
    extinction 0.04 0.06
    albedo 0.07 0.07 0.07
    radiusScale 1
    aspect 1 1 0.8
    softness 0.7
}
~~~

`extinction` sets light blocking; `albedo` the scattered-light color, and the remaining fields shape the cloud. Authored media can work even when automatic physicalization is off.

Each accepted particle contributes a small soft 3D density shape to the fog cells. Authored shapes take priority; conservative automatic matching can supplement old sprites without rewriting every effect. [Details](../legacy-fx-physicalization.md).

## Lit smoke sprites

*Requires `r_volumetricFog 2`, because sprites sample the same spatial light field as fog.*

Makes ordinary smoke sprites dark in a dark room and colored beside a saber or lamp. It improves existing alpha-blended smoke without needing an effect rewrite; additive glows remain unlit. Medium performance cost when the light field must be maintained.

**Main controls**

- `r_particleLighting 0 | 1` — authored sprite color / volumetric-light-field lighting. Requires `vid_restart`.
- `r_particleLightingMix 0..1` — blend between authored and lit color.
- `r_particleLightingScale <value>` — light-field gain.
- `r_particleLightingFloor <value>` — minimum light factor.

**Debug**

- `r_particleLightingDebug 1..5` — field, baked-only, sun-only, dynamic-only and sprite classification.

The sprite samples local baked, sun and dynamic light from the fog lighting field, then mixes it with its authored color. This changes lighting, not the particle’s shape or movement.

## [For mod authors] Glowing gas and particle clouds

*Requires spatial fog, plus particle media for EFX clouds, because the glow comes from the medium itself.*

Lets a mod create luminous gas, a glowing fireball volume or bright smoke that contributes to bloom. Turning emission on alone does not make every stock cloud glow. Light added cost on top of the active volume.

**Main controls**

- `r_volumetricEmission <value>` — global volumetric emission scale; `0` disables it.

**Debug**

- `r_volumetricFogDebug 30..34` — scattering source, emissive source, combined source, integrated emission and history/emission separation.

Local `env.json` fog volumes accept `"Emissive": [r,g,b]` and `"EmissiveDensity": value`. For EFX add `emissive 6 3 1.2`, `emissiveDensity 0.02` and optional `emissiveTint 1` inside `volumetricMedia`. Automatic stock-flame emission is the separate opt-in described above.

Emission adds light along the camera ray independently of scattered light. A volume can therefore glow even without an external lamp; this is separate from adding a light that illuminates other surfaces.
