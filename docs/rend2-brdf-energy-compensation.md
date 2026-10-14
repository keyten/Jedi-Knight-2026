# Rend2 GGX multiple-scattering energy compensation

## Scope and control

`r_brdfEnergyCompensation` defaults to `1`. Set it to `0` for the legacy
single-scatter standard BRDF. It is a runtime A/B control and needs no
`vid_restart`.

The feature applies to the standard isotropic GGX metallic/dielectric model.
It does not modify Charlie cloth, anisotropic hair, the eye material, or the
separate water shader. Skin still uses the standard isotropic GGX interface,
so its surface specular is compensated while its existing subsurface model is
left alone.

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
polished lobe, and restores the strongest loss for rough conductors. The DFG
sample, RGB compensation, and compensated directional albedo are computed
once per surface, outside every Forward+ light loop.

This approximation rescales the original lobe rather than reproducing the
broader angular distribution of a full stochastic multi-bounce GGX model. It
is chosen for its low hot-path cost and its direct compatibility with the
existing split-sum IBL and LTC fits.

## Diffuse and transport accounting

The compensated specular directional albedo is approximated by:

```
Ess  = F0 * dfg.r + dfg.g
Ecmp = saturate(Ess * energyCompensation)
```

For the standard dielectric substrate, diffuse is multiplied by `1 - Ecmp`.
This is lobe partitioning, not the multiple-scattering implementation itself:
the compensation restores energy discarded by masking/shadowing inside the
single-scatter specular model, while the partition prevents that restored
interface reflection from being added on top of an unconstrained diffuse
substrate. It does not model transmission, subsurface absorption, clear coat,
or wavelength-dependent complex IOR.

Cubemap radiance and SSR radiance use the same already-compensated IBL weight.
SSR continues to replace the cubemap's incoming radiance contribution; it does
not apply another material factor. AO/specular occlusion remains a transport
visibility term outside the compensation.

The convention-v2 LTC tables fit the single-scatter correlated-Smith GGX lobe.
The chosen approximation is explicitly a multiplicative scale of that lobe,
so the isotropic LTC amplitude is multiplied by the cached material factor.
No matrix refit is needed. Hair's representative-point LTC approximation and
Charlie cloth remain unchanged.

## Validation and performance

`tests/rend2_pbr_contract.cpp` contains two numerical white-furnace paths:

- independent uniform-hemisphere integration of the old GGX BRDF;
- 16,384-sample quasi-Monte-Carlo integration of the LUT's DFG basis followed
  by RGB compensation and diffuse partitioning;
- a numerical Kulla-Conty directional-albedo reference, including its
  cosine-weighted average albedo and Schlick average Fresnel.

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

GPU cost for a standard material is one DFG texture lookup per fragment, one
reciprocal, and small RGB arithmetic shared by all lights. A point, spot, sun,
or Forward+ light only receives a cached RGB multiply. Use
`r_forwardPlusBenchmark` with `r_brdfEnergyCompensation 0/1` on the same camera
and light set for hardware-specific timing; the light-count slope should be
unchanged because no lookup or transcendental work was added to the loop.
