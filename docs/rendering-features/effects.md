# Smoke and effects

[All rendering settings](../rendering-features.md)

Smoke, dust and explosions from game effects: real 3D volume, lighting, and glowing gas. Everything here builds on [volumetric fog](fog.md#volumetric-fog) (`r_volumetricFog 2`).

## Volumetric smoke

Original smoke is a stack of flat transparent pictures. It doesn't block what is behind it in proportion to its thickness, lamps don't color it, and sun beams don't enter it. With this, smoke from effects also becomes real fog in the volumetric fog grid: it is lit and shadowed like the rest of the fog, hides the background according to its depth, and sabers glow inside it. The original sprites are still drawn on top.

Stock effects have no volume data, so the game recognizes known smoke effects automatically. Mods can also give their effects volume directly.

**Cost:** medium; heavy when large clouds overlap.

- `r_volumetricParticles 0` — 1 on. Without it nothing below is shown.
- `fx_physicalization 0` — automatic volume for stock smoke: **0** off, **1** every recognized effect, **2** only effects in `fx_physicalizationOptIn`, **3** every recognized effect except `fx_physicalizationOptOut`.
- `fx_physicalizationStrength 1` — density of the automatic smoke, 0–16.
- `fx_physicalizationOptIn ""`, `fx_physicalizationOptOut ""` — effect names for modes 2 and 3, separated by spaces, commas or semicolons, e.g. `"volumetric/black_smoke;rocket/explosion"`. A list only picks among recognized effects; it can't turn an unknown effect into smoke.
- `fx_physicalizationComposite 0` — 1: also smoke inside composite explosions.
- `fx_physicalizationEmission 0` — 1: known stock flames and fireballs [glow](#glowing-gas).
- `fx_physicalizationAdaptive 0` — 1: fit size, softness and density of automatic smoke to each effect.

**Compare:** `r_volumetricParticlesCompare` — 1 split screen (flat sprites only left), 2 off. It covers the automatic smoke too.

**Mod authors:** add this to an EFX `Particle` or `OrientedParticle`; it works even with `fx_physicalization 0`:

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

`extinction` sets how much light the smoke blocks, `albedo` its color (dark here), the rest its shape.

**How it works.** Each accepted particle adds a soft 3D blob of density to the fog cells it covers, every frame. Authored data wins; automatic matching is conservative and only accepts effects it recognizes. The most important particles are uploaded first, up to a limit.

- `r_volumetricParticlesMax 128` — particles uploaded per frame.
- `r_volumetricParticlesScale 1` — density multiplier.
- `r_volumetricParticlesHistory 0.3` — how much frame averaging is kept where smoke changes; lower reduces smearing behind moving smoke.
- `fx_physicalizationAggregate 0` — experimental: merge exactly overlapping automatic clouds.

**Debug**

- `r_volumetricFogDebug 26..28` — particle density, history reduction, bounds.
- `r_volumetricParticlesDebug 1` — print culling statistics.
- `r_volparticles` — submission, culling and upload limits.
- `fx_physicalizationDebug 1` — classification and submitted smoke.
- `fxaudit <efx>` — whether an effect qualifies (SP, `developer 1`).
- `fx_physicalizationSources 0` — 1: track which effect each particle belongs to, for `fxsources` (SP/MP, `developer 1`). No visual change.

Design notes: [legacy-fx-physicalization.md](../legacy-fx-physicalization.md).

## Lit smoke sprites

Ordinary smoke sprites keep their painted color everywhere: bright white smoke in a dark room. With this, they take the light around them: dark in the dark, orange next to fire, blue next to a saber. Glowing additive effects (sparks, flames) stay unlit. Works on stock effects.

**Cost:** medium.

- `r_particleLighting 0` *(restart)* — 1 on.
- `r_particleLightingMix 1` — 0 the painted color … 1 fully lit.
- `r_particleLightingScale 1` — brightness of the light; 1 leaves smoke in average map light unchanged.
- `r_particleLightingFloor 0.03` — minimum brightness, so smoke never goes fully black.

**Compare:** `r_particleLightingCompare` — 1 split screen (painted color left), 2 off.

**How it works.** Each sprite samples baked, sun and dynamic light from the volumetric fog's light grid at its position. Only the color changes, not shape or motion.

**Debug**

- `r_particleLightingDebug 1..5` — light field, baked only, sun only, dynamic only, which sprites are lit.

## Glowing gas

Lets fog and smoke glow by themselves: luminous gas, a fireball volume, hot smoke. It glows even with no lamp nearby, and adds to bloom, but doesn't light the surfaces around it. Stock content doesn't glow unless `fx_physicalizationEmission 1`; mods can author it.

**Cost:** light.

- `r_volumetricEmission 0` — global glow strength; 0 off, 1 as authored.

**Compare:** `r_volumetricEmissionCompare` — 1 split screen, 2 off.

**Mod authors:** local fog volumes take `"Emissive": [r,g,b]` and `"EmissiveDensity": value`. In EFX `volumetricMedia` add `emissive 6 3 1.2`, `emissiveDensity 0.02` and optionally `emissiveTint 1`.

**Debug**

- `r_volumetricFogDebug 30..34` — scattered light, glow, both, integrated glow, history.
