# Canonical reconstructed static lighting, schema 1

`shared/rd-rend2/tr_lightrecon_types.h/.cpp` defines the single canonical
`Rend2LightRecon::ReconstructedLightingAsset`. It is a C++11, standard-library-only
CPU model. Its location is shared with Rend2 for build convenience; it includes
no engine, BSP or graphics types. The implementation reuses the dependency-free
colour constants and normalization in `tr_pbr_contract.h`.

This change implements a data contract, validation and conversion helpers.
There is no inverse solver, observation importer or lighting-rendering adapter.
The separate [versioned disk format and cache adapter](rend2-reconstruction-disk.md)
serialize validated assets and optional chunks; complete source hashing and
automatic map-load cache integration remain unavailable.
The [read-only CPU access API](rend2-reconstructed-lighting-api.md) supplies immutable
published snapshots and explicit availability; it does not evaluate missing data.
The [standalone CPU core](rend2-reconstruction-core.md) adds streaming inputs and
an explicit builder/session boundary; it does not install an inverse solver.
It does not populate the asset from stock maps. Existing `vrOutput`, `vrProxy`,
`vrStaticLight`, fog promotion, entity lighting and legacy rendering still run
as before. Those experimental records are not this canonical architecture.

## Boundaries and components

The intended boundary is observations -> future estimation -> canonical asset ->
consumer adapters. Observations (BSP lightmaps, lightgrid, traces, materials and
authored metadata) remain separate input datasets. Shadows, volumetric fog,
entity/Ghoul2 lighting, Forward+, LTC, indirect irradiance and sun/sky can later
consume the same asset, each through its own adapter.

| Asset record | Meaning |
| --- | --- |
| `PhysicalEmitter` | A hypothesis about a physical emitting object, including directional sources |
| `SunSource` | A role referencing a directional emitter, never a duplicate intensity record |
| `EnvironmentLighting` | Sky/environment radiance metadata, optional mean luminance and external distribution |
| `TransportField` | Indirect irradiance, reflected radiance, aperture transport, unresolved direct irradiance or unattributed incident radiance |
| `GeometryRegion` | Persistent geometry-region identity, optional world bounds and external membership |
| `VisibilityReference` | Segment/sky transmittance or region connectivity dataset, with source/region scope |
| `ContributionReference` | Per-source attribution evidence, with direct/indirect/unresolved component and explicit quantity |
| `Provenance`, `Calibration` | Origin, observation limitations and interpretation of photometric values |
| `Diagnostic` | Structured code/message, optional source/region and finite metric with an explicit unit |

A bright doorway, reflected patch or corridor bend belongs in `TransportField`.
It is not automatically a physical emitter. A speculative emitter is permitted
as a candidate with unknown physical confidence; an estimate of incoming light
alone does not establish it. Transport/environment records cannot claim
`exactLight=Candidate`. Direct contribution records can reference only emitters
or environments, not transport estimates.

Direct and indirect fields/contributions are separate records. Mixed input that
cannot be partitioned remains explicitly unresolved/unattributed. A consumer
must resolve overlapping datasets, explicit-sun coverage and selected hypotheses
before summing energy. The asset never encodes fog promotion weights or instructs
any consumer to subtract an attributed contribution from baked lighting.

## Identity, version and provenance

The stable schema identifier is `org.openjk.reconstructed-lighting`, version 1.
This is an in-memory semantic schema, not a native-struct binary file format.
An incompatible schema change requires a version change; a reader must reject
unsupported versions. `Validate` currently accepts version 1 only.

`lineage` identifies the reconstruction across revisions. Nonempty results need
a lineage and root provenance. The fully empty default asset is valid and means
no reconstruction information, not a proven black world or absent sun.

`Id` is an unsigned 64-bit persistent producer-assigned value; zero means absent.
Emitters, environments and transport records share one source ID namespace.
Provenance, calibration, dataset, region and hypothesis-group IDs each have their
own namespace. IDs are not array positions, BSP indices, runtime light indices,
memory addresses or graphics handles. References survive vector reordering and
unrelated record deletion. Duplicated IDs and dangling references are rejected.

A future producer must persist its identity-to-ID allocation for the lineage,
reuse an ID when refining the same source, allocate a fresh ID for a distinct
source/hypothesis and never recycle deleted IDs. A hash-based allocator must
check collisions against that persisted allocation, not assume hashes unique.
Allocation and matching across solver runs are not implemented here.

Provenance records form a validated acyclic graph. They name input identifiers,
optional algorithm-prefixed content digests, producer/version, estimation method
and parent evidence. No input buffer is borrowed. A record may summarize clipped,
missing, quantized, animated, unsupported-material and other unreliable evidence.
`observationQuality` can reference a separate per-sample mask dataset, preserving
which individual observations have those problems. An aggregate flag does not
claim that every supporting observation has the issue, or quantify its severity.

Hypothesis groups contain mutually exclusive alternative sources (including
multiple possible suns). Each source is candidate, resolved or rejected. At most
one source in a group is resolved. Optional weights lie in `[0,1]`; incomplete
groups need not sum to one. Independent sources use no group. A future solver
must model alternatives that require several sources together using a new
versioned grouping extension; the present groups select individual sources.

## Units and colour

This contract follows [the physical PBR contract](rend2-physical-pbr.md).

| Value | Unit and invariant |
| --- | --- |
| Positions, rectangle half extents, tube half length/radius, range, positional sigma, effective radius | JA world units; physical lengths divide by `worldUnitsPerMeter` |
| `worldUnitsPerMeter` | Finite and positive; default `33.3333333333` (64 world units = 1.92 m) |
| `physicalToScene` | Finite and positive fixed scene scale, default `0.0001`, never exposure |
| Point / spot intensity | Photometric Y in candela / on-axis candela |
| Rectangle / finite tube emission | Photometric Y luminance in cd/m² (nits) |
| Directional / sun emission | Photometric Y illuminance in lux on a plane normal to photon travel |
| Sky mean/distribution or radiance field | Luminance Y / linear RGB luminance in nits |
| Irradiance field | Illuminance in lux, with angular irradiance encoding declared by the dataset |
| Directional disk radius | Radians in `[0, pi/2)` |
| Confidence and hypothesis weight | `[0,1]`, independent dimensionless assessments |

Photometry consists of a scalar Y and known/unknown chromaticity. Known colour
is nonnegative, scene-linear sRGB/Rec.709 D65 with
`0.2126 R + 0.7152 G + 0.0722 B = 1` (tolerance `1e-4`). Components may exceed 1.
Black is represented by zero intensity, not a zero chromaticity. Unknown colour
is permitted. `NormalizeChromaticity` clamps finite negative inputs to zero and
rejects black/near-black (`Y <= 1e-6`), preserving the PBR ingestion convention.
It rejects NaN/Inf before clamping. All conversion helpers leave outputs unchanged
on failure, reject overflow and never insert fallback values for invalid input.
Length conversions accept nonnegative lengths; positions themselves can be signed.

`CalibrationKind` is mandatory for known intensities and populated fields:

- `PhysicallyAuthored`: explicit physical authoring, with authored/external
  provenance. It does not imply that a real-world lamp was measured.
- `LegacySceneEquivalent`: named-unit equivalents produced by a documented
  scene calibration from legacy assets. The method must record its reference,
  assumptions and scale rationale. These are not calibrated measurements of the
  historical scene. LDR clipping, unknown bake settings and material response
  remain limitations even after selecting a scene scale.
- `UncalibratedRelative`: `Relative` units only; no automatic physical conversion
  is allowed. Unknown, wholly uncalibrated photometry may omit calibration.

The first two use `sceneRGB = chromaticity * Y * physicalToScene` when an adapter
requests scene values. `SceneEmissionRGB` implements only this boundary multiply;
the result retains its quantity (e.g. scene-scaled candela is not irradiance).
It rejects unknown colour/intensity and relative estimates. It does not validate
the entire asset: callers must first validate assets received from a producer.
Physical source values stay in their named units in the asset, independent of
HDR packing, pre-exposure, tone mapping or cvar state. Fields/contributions store
linear RGB quantities in the declared named/relative unit, not display RGB or
pre-exposed texture values. An RGB value has the declared photometric quantity
as its Rec.709 Y. Future adapters apply the same fixed scale exactly once.

## Geometry and direction conventions

All coordinates use the JA right-handed world frame with +Z up. Positions are
world-space centres. A known emitter frame has orthonormal right/up/forward axes
with `cross(right, up) = forward` (unit/orthogonality tolerance `1e-4`). They are
local +X/+Y/+Z; they are not view axes. Unknown frames are allowed. All components
must be finite even when marked unknown.

- Point: position, optional effective luminous radius, isotropic or tabulated
  angular intensity. The radius bounds/softens the ideal point singularity; it
  does not redefine candela as luminance.
- Spot: position, effective radius and forward axis pointing **out of the light**,
  in photon travel direction. A smooth cone has full on-axis strength inside
  `cosInner`, cubic smoothstep between the limits and zero outside `cosOuter`;
  `-1 <= cosOuter < cosInner <= 1`. Intensity is on-axis candela. Lumen ingestion
  can later use the existing PBR integrated solid-angle helper.
- Rectangle: centre, right/up frame, strictly positive half width/height. Forward
  is its emitting normal. Lambertian emission has constant emitted luminance;
  projected solid angle supplies the receiver cosine. `twoSided` explicitly
  enables the opposite face at the same luminance.
- Tube: centre +/- right * half length, strictly positive radius and half length,
  emitting the curved lateral surface of a finite open cylinder with Lambertian
  luminance. Caps are not emitters in schema 1. A zero-radius mathematical line
  is deliberately invalid; unknown geometry can remain unknown.
- Directional: forward is photon travel **from the source into the scene**;
  the receiver-to-sun direction is its negative. There is no finite source
  position/range. Optional apparent disk angular radius represents finite sun
  size, independently of position uncertainty.

Field angular directions point **from the receiver towards incoming light**.
An environment direction follows that same convention in world space; its
external encoding must declare its parameterization. Anisotropic tabulated
emitter profiles use local unit directions and dimensionless peak-one weights;
they require a known local frame. Point/spot profiles multiply candela, area
profiles multiply luminance. Payload decoders must verify normalization and
finite values. Cone angle/profile, size and intensity are separate quantities.

Finite ranges are optional culling extents, not intensity parameters or a
claim of physical light cutoff. The asset does not mandate a consumer's range
window. Effective point/spot radius is explicit or unknown; an adapter may
later choose the physical PBR 1 cm fallback as a documented adaptation policy.
It is not silently invented by the data model. Known geometric dimensions are
positive; negative dimensions are rejected even in unused shape fields.

Extension emitter kinds (`>=0x10000`) carry a namespaced, versioned type key and
an external geometry payload. Common identity, photometry, evidence and region
references remain usable without changing unrelated consumers. Consumers must
skip/preserve unsupported types. Generic validation accepts their metadata but
does not certify them as exact-light candidates: a future type decoder must
establish its physical semantics before that status can be trusted.

## Uncertainty and incomplete results

`Estimate` has independent `known` and `bounded` bits. A finite central value,
finite interval, both or neither may be present. A known central value must lie
inside its interval. Intervals need not be symmetric and do not imply a particular
statistical coverage. NaN/Inf is invalid even in inactive fields. Intensity,
lengths and their active bounds are nonnegative. Zero confidence is different
from unknown confidence, and a measured/estimated zero intensity is different
from unknown intensity.

Position's one-sigma uncertainty is in world units. It is separate from
`physicalEmitterConfidence` (is this actually an emitter?), `transportConfidence`
(does the lighting/transport estimate fit evidence?) and intensity uncertainty
(bounds in the intensity's unit). None is inferred from another. Observation
issue masks are separate evidence limitations, not low-confidence substitutes.

`exactLight` and `approximateIrradiance` suitability are independent
unknown/unsuitable/candidate assessments. An exact candidate needs known physical
or calibrated-equivalent intensity/colour, required position/frame/geometry and
a defined angular model. Point/spot effective radius must be known. It still
requires hypothesis selection, supported consumer evaluation and visibility;
candidate status does not allocate a shadow map or change energy. High transport
confidence alone does not make a source exact. No confidence threshold or
renderer light budget is part of the schema.

Empty source vectors, absent sun, missing intensity, unknown colour/geometry,
unknown calibration, unsupported materials and rejected alternatives are valid
where the available metadata permits them. Missing datasets describe incomplete
results, not a black field. Known-geometry zero directions, collinear rectangle
axes, degenerate rectangles/tubes, negative radii, invalid bounds, conflicting
units/calibration and broken references are rejected.

## Large datasets and lifetime

The asset owns its small vectors, strings and metadata by value. It owns no
large sample arrays and borrows no input memory. `Dataset` references a CPU
encoding/version at a locator, optional content digest, logical record count,
layout (dense/sparse/chunked/procedural) and maximum decoded records per chunk.
Chunked datasets require a positive chunk size. A future dataset store owns
decoded chunks or leased views and their lifetime; those leases stay outside
the canonical asset. Descriptors can outlive a map's input and renderer resources.
The backing store must remain available while future consumers request chunks.

Optional regular transport lattices specify world AABB and three dimensions,
never camera froxel coordinates. Either all dimensions are zero (encoding-defined
domain) or all are positive with nondegenerate bounds. With dimensions, logical
sample count must match the dataset; overflow is rejected. Nodes include both
bounds on axes with dimension >1; a dimension of 1 samples that axis midpoint.
Sparse element count describes the logical lattice, not resident nonzero count.

The payload encoding is extensible and must define stable sample IDs/locations,
angular basis (e.g. a specified SH normalization), channels, byte order, signed
basis coefficients where applicable, chunk lookup and decoding. No native C++
struct layout or GPU format is implied. The disk module provides bounded opaque
chunk I/O; no production numerical field encoding/evaluator is implemented.
Metadata validation does not pretend to inspect
external data, sample ranges, visibility truth or numerical reconstruction quality.

Contribution records are keyed by `(sample ID, source ID, component)` within a
referenced stable sample domain. Their allowed source list must be nonempty.
Values are linear RGB irradiance/radiance in explicit units or dimensionless
attribution fractions in `[0,1]`. Each reference describes one component and
quantity; a decoder must reject payload source/sample IDs outside its declared
sets, duplicates, non-finite values and out-of-range fractions. Sparse or
unloaded entries always mean unknown; explicit zero is a distinct stored record.
Different components, quantities or hypothesis alternatives do not implicitly
normalize into one partition. A dense sample-by-source matrix is never required.

Visibility uses source/sample/region IDs with `[0,1]` transmittance or explicit
connectivity values. Unlisted/unloaded entries are unknown, never assumed clear
or blocked. Region connectivity (including a future BSP PVS import) is coarse
evidence and cannot stand in for exact segment visibility. Per-observation
quality records pair stable sample IDs with `ObservationIssue` masks in a separate
dataset; actual BSP observation values remain outside the reconstruction.

## Implemented and provisional work

Implemented by the data-model module: the metadata records above, explicit incomplete/uncertain
states, calibration provenance, ID/reference and provenance-DAG validation,
geometry/frame/unit/range validation, strict finite conversion helpers, and CPU
contract tests. Both SP/MP compile the module. The separate CPU access API validates
and publishes manually supplied assets; no runtime solver populates them yet.

Provisional choices: isotropic positional sigma rather than covariance; scalar
intensity intervals rather than a distribution; individual-source alternative
groups; uncapped Lambertian tubes; references to versioned numerical payload
encodings whose decoders remain unimplemented. Adding more uncertainty models,
material-aware transport or complex source groups will require a versioned
extension/schema decision. This intentionally avoids permanent dense matrices
and consumer-owned light limits, at the cost of a future loader/decoder layer.

Future algorithms must estimate sources/geometry, classification, sun/sky,
direct-versus-indirect attribution, visibility, regions, photometry/calibration,
uncertainty, diagnostic residuals and suitability from real observations. No
claim is made that those inverse problems are solved or that LDR stock maps
contain enough information for unique calibrated physical reconstruction.

## CPU verification

Standalone (no Boost, engine, OpenGL or external libraries):

```sh
cmake -S tools/lightrecon_test -B build/lightrecon-test
cmake --build build/lightrecon-test --config Release
ctest --test-dir build/lightrecon-test -C Release --output-on-failure
```

Alternatively enable `-DBuildLightReconTests=ON` in the root build and run the
`lightrecon_contract` CTest. Tests cover empty/incomplete assets, all five
emitter types and extension metadata, strict unit and colour boundaries,
independent uncertainty, non-finite rejection, identity/reference stability,
degenerate geometry, sun alternatives, calibration honesty, separate transport,
sparse contribution descriptors and field dimensions. Renderer checks build
`rdsp-rend2` and `rd-rend2` in a normal SP/MP configuration.
