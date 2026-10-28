# X05: non-vacuous forward-lighting acceptance

This is a **forward-capability benchmark**, not an inverse solver. Its private
reference deliberately receives known source parameters. No emitter is recovered,
published or added to the renderer. Stock JA maps remain `UnknownProfile`.

Implementation: [controller](../tools/rend2/evaluation/x05/run.py),
[policy v3](../tools/rend2/evaluation/x05/policy.json),
[machine-readable witness](../tools/rend2/evaluation/x05/results/summary.json),
[frozen manifest](../tools/rend2/evaluation/x05/results/manifest.json),
[tables](../tools/rend2/evaluation/x05/results/README.md).
Uses the existing [evaluation](rend2-lighting-evaluation.md),
[X02](rend2-observation-coverage-x02.md), [X03](rend2-bake-identifiability-x03.md),
[X04](rend2-shadow-footprints-x04.md) and [bake model](rend2-bake-model.md).

## Why black used to pass

The twelve original compiled observation datasets and twenty-two kernel BSPs are
audited independently. The first uses every eligible canonical mapped receiver;
the latter retains the historical interior-floor verification domain. Their
counts have different denominators and are not added as independent evidence.

The old X02 split uses 256-world-unit blocks, origin zero, seed 20261032 and a
one-unit gap plus **three footprint radii** of filter clearance. A floor at z=0
and ceiling at z=256 sit on partition boundaries. They are excluded; held-out
wall strips can be dark even when the full bake is illuminated.

| Original fixture | Full eligible floor mean RGB / maximum code | Old held-out | Old black interval RMSE |
|---|---:|---:|---:|
| skylight | 4.023529 / 111 | 176 dark wall samples | 0 |
| wall_leak | 25.374118 / 70 | 110 dark-side wall samples | 0 |
| panel | 1.546667 / 3 | 176 weak wall samples | 0.845368 |
| sun_sky | 69.146667 / 113 | 176 weak wall samples | 1.461285 |

Thus skylight's bake was not black, padded or missing. Geometry-based partition
selection removed its useful floor. `wall_leak` legitimately measures a negative
control, not attenuation or positive-light accuracy. The panel also really has
weak floor lighting from its small six-face prism. Its ceiling can be brighter;
this does not turn that fixture into a strong diffuse-area-light control.

The old witnesses and source hashes are preserved. Old RMSE uses the historical
nearest-half-byte metric; X05 uses the verified **truncating** output operator.
These are labelled separately, not silently rescored as identical tests.

The complete [census](../tools/rend2/evaluation/x05/results/audit.json) records
eligible count/area, original and new role counts, independent surface and block
counts, normals, per-normal RGB distributions, position bounds, source-distance
quantiles, brightness histograms, channels above the policy floor, saturation,
components tested, representative visible/blocked/unknown pairs and boundary
sensitivity. Visibility census sampling is geometry-only, one receiver per
surface/64-unit cell, capped at 256. It is not all-pairs visibility coverage.

## Admission and acceptance

The manifest separates these categories: positive light, dark/blocked negative,
shadow boundary, sun/sky, area/tube, encoding, profile discrimination, ambiguity,
GI/indirect and invalid/unsupported. Native tube and general bounce operators
remain unsupported; a prism and a measured bounce delta are not substitutes.

For the declared NetRadiant d9338309e, gamma=1, compensate=1, exposure=0 output:

* Positive admission requires at least four receivers, two independent spatial
  blocks containing meaningful signal, at least four receivers with peak code
  >=8, and area-weighted black interval RMSE >=4. Eight codes exceed the existing
  quantization/one-code kernel envelope. This is not a universal physical threshold.
* Required predictions cover **all requested area**, including difficult samples.
  Missing, `Unavailable`, wrong-domain and omitted records reduce coverage;
  their errors are not substituted with zero.
* Interval RMSE and maximum channel interval error must each be <=1 code.
  Positive tests additionally require >=50% relative improvement over black.
  Absolute improvement is also reported on exactly the same targets/area/domain.
* Negative controls require no prediction above code 1. Their zero baseline is
  legitimate. A baseline ratio with denominator zero is `NonInformativeBaseline`,
  with a null ratio, not infinity or a division by zero.
* Unknown geometry requires a typed unknown with a reason. Unknown requested
  physical photometry requires `NotComparable`. Neither accepts numerical black.
  An unsupported GI operator passes only its **capability-reporting** test, never
  a positive GI prediction test.

For ordinary truncating byte b the interval is [b,b+1], capped at the encoded
endpoint 255. The source-locked output operator normalizes by a shared RGB peak
before quantization. Thus encoded 255 is not three independent lower bounds on
physical incident RGB. Tests reject independent channel clamps for coloured
over-range input. Raw/source-relative/scene-equivalent/SI quantities remain
separate; no lux/candela/albedo conversion or gain normalization is introduced.

All aggregate reference/admission checks must pass, including rejection of
black/all-blocked in positive cases, all-visible in occluded cases, wrong finite
source geometry/profile, missing, abstained and partial predictions. Correct
darkness is necessary but cannot satisfy positive-light admission.

## Versioned controls and frozen decisions

X05 uses 128-unit blocks, origin (17,31,47), seed 20261028, gap 1 and the existing
three-radius filter exclusion. Only partition coordinates are shifted; geometry,
RGB, IDs and query positions are unchanged. Original zero-origin output remains
compatible. New floor receivers use actual source UVs, half-texel centres and
affine UV-derived area/radius, at |x|,|y|<=352 (32-unit wall clearance). Historical
kernel comparisons retain their default 272-unit domain. No RGB selects the split.

Eight new own-content BSPs are compiled with the pinned executable:

| Required control | Meaning | Held-out black interval RMSE |
|---|---|---:|
| sky aperture v1 | Actual diffuse sky through an opening; no direct sun | 6.386835 |
| sealed sky v1 | Same sky declaration hidden behind an opaque ceiling | 0 |
| wall pair v1 | Illuminated and blocked receivers in one scene | 18.942897 / 0 |
| doorway pair v3 | Positive transmission and blocked receivers | 37.335727 / 0 |
| panel positive v1 | Ceiling polygon, authored surface parameter 200 | 59.951022 |
| sun positive v1 | Zenith sun parameter 120, no diffuse skylight | 120 |

These are encoded relative parameters, not calibrated SI intensities. All required
reference predictions have zero interval error. All-visible has nonzero blocked
error and fails the maximum-channel/leak gate; black fails every positive test.
Constant ambient is fitted on training only. It legitimately passes a uniform
sun floor and dark sealed floor, demonstrating that these individual cases do not
identify sunlight or source location. The directional boundary test is independent.

Policy v1/v2 are retained. Their numerical gates and split were never weakened.
Doorway v1 originally lacked independent positive support; v2's additional
apertures still provided weak distant signal. Both remain failed-admission
diagnostics. Policy v3 adds a separately named, explicitly authored 1200-relative-
unit doorway primary and the full interior receiver domain. Its shadowed receivers
remain zero. No original dark bake or frozen witness was brightened in place.

The manifest freezes compiler/map/shader/recipe hashes, source lock, input/private
member hashes, policy/split, private target hashes, required/diagnostic roles,
categories, controls, limits and controller code hashes before future solver
tuning. A fresh directory is mandatory. Full compiler argv/logs stay local with
the authoring recipe. The versioned compact report contains no stock game assets.

## Shadow, angular and profile contracts

All 35 X04 boundary texels / 105 channels are retained. The original analytic
maximum discrepancy 28 is reproduced; the restricted source-locked opaque/sky
trace-tree replay has zero error. The old centre-analytic prediction is rejected
on the boundary subset itself, not hidden under global RMSE or an ignore band.

An independent analytic moving-wall-edge footprint checks visibility integration
and source-motion differences. At source x=-0.1 and 0 the integrated derivative
is 10/3, while fixed 2x2 binary samples give zero. At x=0.1 the edge reaches the
footprint endpoint: the one-sided slopes are 10/3 and 0; central differences tend
to 5/3. It is explicitly **nondifferentiable**, not a successful unique gradient.

The existing compiled spot has a separate cone-edge fold: opposite Y half-spaces,
three-radius gap, two X-side held-out blocks. Twenty held-out angular-edge receivers
have black RMSE 24.024987. Correct cone prediction has zero interval error; removing
the cone gives RMSE 23.137681. Geometry rays are clear: this is an angular edge,
not an opaque caster edge.

Profile tests use fixed source geometry/colour and one non-negative scalar in
[0,3000] per alternative, with identical deterministic work. Training fits the
scalar, validation ranks, held-out confirms. Cached visibility is reused only at
fixed source geometry; shifted-source controls trace again.

| Multiple-normal controlled data | Validation RMSE | Held-out RMSE |
|---|---:|---:|
| Correct inverse square | 0.207790 | 0.046360 |
| Linear alternative | 30.150915 | 22.943279 |
| No-angle alternative | 14.232873 | 10.704386 |
| Spot alternative | 95.111947 | 75.333923 |

The broad fold contains 22/8/6 train/validation/held-out receivers with multiple
normals; the original broad floor adds 66/57/33. The geometry-selected equal-
distance annulus uses separated quadrants (2/2/4 receivers). Both inverse-square
and linear predictions attain zero validation/held-out interval error there:
**Ambiguous** is required. Viable regret is one code; a discriminating case needs
two-code separation on both validation and held-out and correct-profile error <=1.
No normalized ranking is called a posterior or Raven compiler identification.

The original linear and no-angle BSPs additionally provide explicit **kernel
conformance** requests, including their dark cutoff samples. The linear held-out
positive signal occupies only one block, so this is not labelled a positive-light
admission or an independently discriminating scene. Its nonzero black error and
correct formula are checked; broad multi-normal comparison supplies the separate
profile-discrimination evidence.

## Blind boundary and external scoring

The optimizer receives only four members under `inputs/`: manifest, training
JSONL, geometry archive and grid JSONL (empty for new surface-only controls).
Validation/held-out RGB, raw images, vertex RGB/HDR, recipe, sources and labels
remain outside this directory. `quality scan` sanitizes the geometry; C++
`public-check` verifies it. Merely being geometry-only is insufficient.

Training fields use the existing strict allowlist. Private positive/negative
labels, source visibility and admission groups are stripped. The evaluator's
held-out permission is unavailable during validation. Tests attempt wrong-role,
cross-map, hidden-field, neighbouring-footprint, duplicate, target-override,
missing-case and early held-out access. These are artifact/API tests, not an OS
sandbox: mount only `inputs/` read-only into an untrusted process/account.

`score.py` consumes a frozen prediction packet and verifies its manifest hash and
private target hashes. It independently requires the compiled controls plus spot,
all 35 boundary texels, encoding, linear/no-angle kernels and explicit unknown/GI
statuses. A missing whole case fails. Its result is forward prediction only.
Profile-ranking and derivative self-checks remain separate controller diagnostics;
passing the external RGB protocol is not source recovery or a full inverse solver.

## Reproduction and limitations

Exact commands: [tool README](../tools/rend2/evaluation/x05/README.md).
The checked-in validation file records actual commands, outcomes and omissions.
No production C++, shaders, cvars, consumers, PBR units or renderer output changed;
SP/MP/game/GPU builds are not required or claimed for this Python-only task.

Repeated analysis of **identical frozen BSPs** must produce identical semantic
result bytes and blind members. Recompiling is a distinct experiment: the 22 old
and eight new lightmap lumps are byte-identical, but whole BSP hashes differ.
Brush-side draw-surface reference bytes vary in some files (including invalid
indices); four new partitioned rooms also have different resolved grid records.
The report keeps these differences, does not certify grid reproducibility, and
never reuses old BSP IDs. The visual vertices/triangles, brush plane/material
references and original lightmap bytes reproduce. Further compiler grid/brush-side
diagnosis is separate work. New X05 forward tests consume no lightgrid.

The fixture-only visibility reference supports known opaque/sky planar geometry.
The production BVH and conservative unknown-optics semantics are unchanged. A
degenerate receiving UV chart may be skipped only in the audit's explicit
non-strict mode; its independently valid opaque world triangle is retained.
Strict default behaviour and all old boundary tests remain unchanged.

General material filtering, native finite tubes, bounce replay, historical JA
profiles, stock-map physical accuracy and calibrated absolute scale remain
unverified. A green reference self-check is evidence that this benchmark rejects
the declared bad controls, not evidence that lighting has been reconstructed.
