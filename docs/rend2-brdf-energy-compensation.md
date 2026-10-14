# Rend2 GGX multiple-scattering energy compensation

## Scope and control

`r_brdfEnergyCompensation` defaults to `1`. Set it to `0` for the
single-scatter standard BRDF. The diffuse/specular partition remains active;
the cvar controls only missing-energy restoration. It needs no `vid_restart`.

The feature applies to the standard isotropic GGX metallic/dielectric model.
It does not modify Charlie cloth, anisotropic hair, the eye material, or the
separate water shader. Skin still uses the standard isotropic GGX interface:
its diffuse/scattering input receives the split once before the existing wrap
or screen-space diffusion. Its subsurface transport is otherwise unchanged.

## DFG LUT audit

`R_CreateEnvBrdfLUT` in `shared/rd-rend2/tr_image.cpp` creates a 128 x 128
`GL_RGB16F` texture automatically at renderer startup. It is generated from
source code, retained across soft renderer restarts, and has no user asset.
The versioned internal name is `*envBrdfLUT_GGX_MS1`.

- X axis: perceptual roughness `r` at texel centres; GGX slope alpha is
  `max(r * r, 0.0025)`.
- Y axis: `NoV` at texel centres.
- R: Schlick scale, the integral weighted by `1 - (1 - VoH)^5`.
- G: Schlick bias, the integral weighted by `(1 - VoH)^5`.
- B: the separately integrated Charlie/Neubelt cloth response.

The old single-scatter reconstruction is `F0 * R + G`. Consequently `R + G`
is the directional albedo of the single-scatter GGX lobe with `F0 = 1`. That
is the `r` required by the inexpensive Lagarde/Turquin/Filament compensation,
so no LUT channel or external asset had to be added. The LUT is now generated
even when cubemaps are disabled because direct lights and LTC also consume its
material-side result.

## Approximation choice

Kulla-Conty adds a separate multiple-scatter lobe using directional albedo,
its cosine-weighted average, and average Fresnel. It is a useful reference and
is more expressive, but it needs more tabulated data and more direct-light
work. The Fdez-Aguera / Lagarde-Turquin family observes that the missing energy
can instead scale the existing GGX lobe. Filament's practical form is:

```
energyCompensation = 1 + F0 * (1 / r - 1)
```

Rend2 uses this RGB form. It retains conductor colour, tends to one for a
polished lobe, and restores the strongest loss for rough conductors. The view
DFG sample and RGB compensation are computed once per surface. Direct-light
partition samples the same LUT at `NoL`, because the incident direction—not
the camera direction—determines the energy available to the substrate.

This approximation rescales the original lobe rather than reproducing the
broader angular distribution of a full stochastic multi-bounce GGX model. It
is chosen for its low hot-path cost and its direct compatibility with the
existing split-sum IBL and LTC fits.

## Diffuse and transport accounting

For a discrete light, the compensated incident-direction specular albedo is
approximated by:

```
Ess  = F0 * DFG(NoL).r + DFG(NoL).g
Ecmp = saturate(Ess * energyCompensation)
Tin  = 1 - Ecmp(NoL)
Tout = 1 - Ecmp(NoV)
Eavg = integral[0..1](2 * mu * Ecmp(mu))
kD   = Tin * Tout / (1 - Eavg) * (1 - metallic)
```

This factor wraps both Lambert and Burley. Point, spot and sun lights use their
actual `NoL`. LTC area diffuse uses its integrated cosine form factor and the
closest emitter direction as a stable approximation for the interface share.
Burley's angular modulation is capped at the Lambert envelope; this retains
its grazing softening without allowing the empirical retroreflection fit to
create energy above the shared diffuse budget.

`Eavg` is evaluated once per surface with four-point Gauss-Legendre quadrature
over the same DFG LUT and compensation used by the directional lobe. This is
the directional/average albedo of the chosen GGX approximation, not an
unrelated Fresnel guess. The denominator approximates repeated exchange
between substrate and rough interface; entry and exit are therefore not two
independent darkening multipliers. The analytic cosine-weighted Schlick mean
`F0 + (1 - F0) / 21` remains a CPU-test reference.

Diffuse irradiance, lightmaps and light-grid/entity probes have no single
incident direction. Averaging `Tin` cancels `Tavg`, leaving the correct exit
factor `Tout`. Rend2 BSP lightmaps are lighting/irradiance-like data:
`lightall` applies material response later, so this happens exactly once. A
deluxemap's separated dominant lobe uses the full directional expression; its
residual ambient, reconstructed static lighting and probe irradiance use the
integrated form.

SSGI receiver RGB is explicitly **effective diffuse reflectance**: base color
after metalness, wet/substrate attenuation and average interface transmission.
The trace transports it once per bounce. It is neither raw base color nor an
already-lit result, preventing repeated gain or loss from mixed semantics.

This is lobe partitioning, not the multiple-scattering implementation itself.
The compensation restores energy discarded by masking/shadowing inside the
single-scatter specular model; the partition prevents restored interface
reflection from being added to an unconstrained diffuse substrate. It does not
model full multilayer transmission, absorption, clear coat, or complex IOR.

Cubemap radiance and SSR radiance use the same already-compensated IBL weight.
SSR continues to replace the cubemap's incoming radiance contribution; it does
not apply another material factor. AO/specular occlusion remains a transport
visibility term outside the compensation.

The convention-v2 LTC tables fit the single-scatter correlated-Smith GGX lobe.
The chosen approximation is explicitly a multiplicative scale of that lobe,
so the isotropic LTC amplitude is multiplied by the cached material factor.
No matrix refit is needed. Hair's representative-point LTC approximation and
Charlie cloth remain unchanged.

Wet standard materials are partitioned after wetness changes roughness and
substrate attenuation. The smoother water-like interface therefore changes
the directional specular share and removes that share from base diffuse. SSR
or cubemap selection happens later and cannot subtract diffuse again. This is
a cheap film-transmission approximation, not a full multilayer BSDF.

## Validation and performance

`tests/rend2_pbr_contract.cpp` contains two numerical white-furnace paths:

- independent uniform-hemisphere integration of the old GGX BRDF;
- 16,384-sample quasi-Monte-Carlo integration of the LUT's DFG basis followed
  by RGB compensation and diffuse partitioning;
- a numerical Kulla-Conty directional-albedo reference, including its
  cosine-weighted average albedo and Schlick average Fresnel;
- a layered white-furnace check of the incident/exit/average transmission
  identity across roughness, view angle and F0;
- neutral-light metallic sweeps, colored conductors, and Lambert-versus-Burley
  checks under the same diffuse budget.

The matrix covers `F0 = 0.04`, copper-like `(0.95, 0.64, 0.54)`, gold-like
`(1.0, 0.71, 0.29)`, perceptual roughness `0.05, 0.25, 0.5, 0.75, 1.0`, and
multiple `NoV` values. It checks finite/bounded output, non-negative restored
energy, near-unity polished compensation, and substantial rough-conductor
recovery.

Representative numerical results at `NoV = 1` (old single scatter -> chosen
compensation) are:

| Roughness | F0 = 0.04 | Copper-like red F0 = 0.95 |
|---:|---:|---:|
| 0.05 | 0.0400 -> 0.0400 | 0.9500 -> 0.9500 |
| 0.25 | 0.0398 -> 0.0398 | 0.9459 -> 0.9498 |
| 0.50 | 0.0367 -> 0.0368 | 0.8701 -> 0.9460 |
| 0.75 | 0.0251 -> 0.0257 | 0.5956 -> 0.9323 |
| 1.00 | 0.0123 -> 0.0134 | 0.2915 -> 0.9171 |

The test evaluates each conductor channel separately and also computes the
Kulla-Conty reference rather than treating these RGB materials as a scalar.

GPU cost for a standard material is one view DFG lookup per fragment plus one
incoming-direction lookup for each evaluated direct light (area lights use one
representative lookup), reciprocals and small RGB arithmetic. Use
`r_forwardPlusBenchmark` on target hardware to check the light-count slope.

`r_pbrEnergyDebug` provides the acceptance views: `1` diffuse contribution,
`2` specular contribution, `3` their total, `4` estimated unreflected energy
under an average white furnace, and `5` a centre split with legacy
unpartitioned diffuse on the left and conserved lobes on the right.
