# AR-42 — Stop the current oblique-housing pilot

**2026-11-11 (Australia/Sydney). Task Done. Decision: Stop-Current-Approach.**
No bounded repair is authorized. The exact-plane arithmetic is useful, but a
complete modern-only contract across the existing projection, brush, visual,
clearance and identity interfaces has not been justified within a four-hour M
repair. A frozen required native input is also missing. This is a terminal
decision for `duel10-oblique-housings-v1`, not a new research dependency or a
lighting-model rejection. The coherent-region product gate remains **Unknown**;
the pilot is not accepted.

This decision is versioned separately as **AR-42 v1**. It does not change
`viability-v1.json`, the earlier decisions, any frozen failure or any production
contract. [Evidence](../../tools/rend2/aesthetic_relighting/results/ar42-v1/evidence.json)
and [manifest](../../tools/rend2/aesthetic_relighting/results/ar42-v1/manifest.json)
pin inputs, arithmetic, scope, timing and the decision. Licensed coordinates,
full diagnostic tables and scripts stay in ignored `build/ar42-v1/`.

## Checkout and input audit

Initial HEAD was `242c7ce17bcd9aa2c74acc843f78dcb6958c75ad`, branch
`rend2-unified-wip`, origin `git@github.com:keyten/BetterJK.git`. The initial plan
was modified; AR-36's two documents and results directory were untracked. The
index was clean. No applicable ancestor/repository/scoped `AGENTS.md` was found.
During inspection, external commits `15df6a396`, `d1ad0cb2f` and merge
`3026839639331ec7715353d2191ec93e2b011053` arrived. They committed the existing
AR-36 work and added shader-authored surface storage. The later inspection and
decision use that actual revision. AR-42 did not create those commits, stage
files, revert them or alter their production code. The original ASR25 work is
retained.

All five required relighting documents, viability-v1, AR-34/35/41/36 receipts
and manifests, the four named AR-34 geometry packets, AR-41 measurements/audit
and required stdout, ASR24/25 reports/results and exact original source geometry
were inspected. The initial audit made **404 file-hash comparisons / 325 distinct
existing paths**, plus three matching BSP/image/parsed-shader member identities.
It records **one missing path and two inherited hash mismatches**, not a passing
all-input freeze:

* Missing frozen capture:
  `C:\Users\Keyten\AppData\Local\Temp\jk-lightrecon-all-stock-20261031\capture\mp_duel10\home\rend2\lightrecon\v1\observations\mp_duel10_all.jkobs`,
  expected SHA-256 `ccbb97fa534f5be8c1a318d09b52b481284d63468eb95661ab5a642d4b2b9242`.
  It is pinned by `pilot-scope.json` and the inherited input audit. Both local
  `mp_duel10.public.jkobs` copies have SHA-256
  `f511b2c1e3b1046a7a4d2dbf75b3e2514a4da90783553fcc69b6101d7579de2c`;
  they are not hash-identical replacements. No capture was regenerated or
  synthesized. Fresh frozen full-native admission is **Blocked-Inputs**.
* ASR25's inherited hashes for `producer.cpp/.h` differ at current HEAD. Their
  committed shader-storage changes explain the differences; 148/150 inherited
  implementation files and all six frozen native tools match. The newer work
  was inspected and preserved. Old binaries are not evidence of testing those
  newer source changes. No new code/executable freeze is declared.
* Required `build/ar41-v1/contact-diagnostic.stdout` is a retained failed run,
  ending at the start of its controls array. It is not a complete JSON result.
  The completed AR-41 measurements explicitly use
  `contact-diagnostic-v3.stdout`, SHA-256
  `0a3a0e300dced4539e4e9e92cf0d650acfc7dcfc50f3ba0c6a0fecba285a0b31`.
  That newer, receipt-pinned result was reused and independently compared to
  original BSP geometry; the incomplete file was neither repaired nor discarded.

The immutable viability-v1 SHA-256 remains
`96da96c0ccd896bca3751f539b2d8860f4e1d0580f5d947c60c906b5a84a28d0`.
Original source BSP, glow image and parsed shader body identities match. Pilot
row gzip bytes were hashed only; no pilot final/reserve RGB was decompressed,
indexed, displayed, fitted or scored. No native admission or diagnostic executable
was run in AR-42, so no archive RGB buffers were deserialized by a native loader
in this task. Reading the BSP member for hashing and source fields is not a claim
that its entire byte buffer excludes lightmap data; the lightmap lump was not
indexed. No new source-image or receiver-appearance review was performed.

## Complete live consumer map at the inspected revision

Repository-wide symbol/field searches, followed by inspection of the callers,
found the following live operations. Archived benchmark source snapshots are
historical evidence, not additional live implementations. Line numbers refer
to the inspected revision. The machine receipt pins the source hashes.

| Consumer | Plane/support use and boundary |
|---|---|
| `emitter_support.h:12–59`, `.cpp:249–319` — `Key`, `Validate` | Contract schema 1 has **float** anchor/outward, map/geometry/fixture/surface identity, stand-off/front clearance, provenance and optional original triangle lease/mapping. Key includes numerical interpretation **4**, schema/version, every listed field and triangle coordinates/normals. Validate checks bounds, leases, finite values, normals and IDs; it does not establish an exact oblique plane witness. |
| `source_orientation.h:12–65`; `emitter_support.cpp:810–910` — `FromSurfaces` | Orientation interpretation 2 resolves the sign of the geometric cross product using captured face basis, vertex normals and cull evidence. Exact original index triples become a connected, bounded triangle union. Relative double coplanarity precedes the float outward cast. No winding-only authority or bounding-panel substitution. |
| `emitter_support.cpp:420–494` — side/stand-off and mount collection | Builds the absolute-dot float plane; checks point/polygon side, rectangle/spot direction, local query bounds including anchor, opaque/error-zero named triangles and orientation. The measured AR-41 refusal is here. |
| `emitter_support.cpp:501–638`; `emission_geometry.cpp:355–398` | OriginalSurface lease matching; overlap/intersection area with float normal and double clipping; point/polygon orthogonal projection rounded to Vec3; barycentric or area-union coverage. Cylinder body low point, tangent line, radius/axis and stand-off use the same plane. Barycentrics alone do not certify coplanarity of a rounded projected point. |
| `emitter_support.cpp:137–179,640–733` | `inspect` passes float contact normal and double distance to the retained domain. Back-side two-sided classification reverses the plane. Visual named-contact exemption repeats the plane predicate. Independent visual hull SAT / conservative cylinder AABB remain separate. `BackContact` exempts only exact opaque/error-zero **axial** rear boundary contact, one-sided and wholly front support; no coplanar overlay, penetration or unknown optical exemption. |
| `regions.h:30–40`; `regions_support.cpp:14–278` | SpatialSupport has float `contactNormal`, double `contactDistance`. Hull/cylinder ranges, BSP traversal, all retained world brush sides, clipping and deep-penetration checks remain independent. Contact requires `lo >= 0`, numerical coincidence with the **retained BSP plane**, and compatible normal. Its plane is not automatically the exact visual triangle plane. Default/custom box classifiers may return Unknown. |
| `emitter_support.cpp:43–88,180–218,735–797` | Outward-rounded bounds and `FrontSupport` enclose float-translated vertices. Both sided extrusions use the plane direction. Domain, front local extraction, repeated named contact/BackContact/SAT and final independent bake occupancy all participate. Completeness, cumulative work/rays, Unknown and conditional occupancy remain binding. |
| `emitter_shape.cpp:151,204–217,565–577,661–709,1204–1209` | Support enters emitter/prepared-block keys; OriginalSurface quadrature normals; exact-contact point halfspace response; source endpoint identity/tolerance; refinement admission. HistoricalBake reversed-winding and exact-contact restrictions are separate and must retain their current behavior. `Shape::Prepare` unit coverage alone does not run the stronger source check. |
| `shape_selection.cpp:63–96,146–187,359–456,529`; `producer.cpp:770,982,1226–1255` | Derive/rank support; hood/frame/rectangle orientation and offset; keys and provenance; full Check. Point/spot conversion can **clear the original triangle lease**. A new witness cannot rely on that lease always surviving. Current producer 10 also retains authored shader sources separately; that is not pilot admission. |
| `joint.cpp:153,749,2146`; `emitter_shape.cpp:1206` | Static candidate/refinement Check; fixed motion semantics; source/support identities and recorded assumptions. General Joint/historical users cannot silently inherit a modern policy through a global replacement plane. |
| `shadow_cache.cpp:13–46,213–295,362–364,477,525–533`; `shadow_cache_internal.h:42–105`; `shadow_cache_codec.cpp:311–331` | SourceKey and separate supportKey feed ItemKey, reuse and serialized content validation. Full static source Check precedes nodes; plane-distance endpoint assignment and on-plane angular exclusion consume float support. Codec reconstructs emitter SourceKey with empty support and checks stored supportKey via ItemKey; it does not reconstruct a new plane witness. Old records cannot acquire a new certificate merely on load. |
| `field_builder.cpp:313–318,525–529`; `diagnostics.cpp:305–306`; `diagnostics_capture.cpp:41,117` | Field source map/geometry binding, support-key provenance and on-plane angular exclusion; diagnostic anchor/normal arrow; placement/prediction dedup identities. These are additional consumers, even though no runtime/field work is authorized here. |
| `aesthetic_fit.cpp:130–174,361–369`; `lightrecon_aesthetic_tool.cpp:277–282,451–457` | Full Check before basis preparation, then availability; fixed support-key equality across fixture variants. CLI FromSurfaces → MountedPoint, maximum stand-off 8, retained original pose; output normal/support diagnostics. Coverage is a weaker path, not admission. |
| `lightrecon_blind_basis_tool.cpp:108–110`; `lightrecon_joint_tool.cpp:568–580`; `mount_audit.h:66–99`; `mount_diagnosis.h:18–51,132–199` | Other builders/admission callers, source frame construction, diagnostic clone of contact plane, offset probes/rays and lease reporting. Existing emitter-support/shape/selection/aesthetic/sparse/occupancy/receiver-support/shadow-cache tests are controls, not alternate plane authorities. |
| `point_refine.h:184`; `point_refine.cpp:898–926,1026` | Separate MountPolicy float anchor/normal, offset bound and request key. No SourceSupport plane is copied into it in the inspected live callers; a shared field-name search must not conflate the policies. Existing placement domain/geometry/policy keys and occupancy wrappers still constrain SourceSupport callers. |

`visibility.cpp:214–232,593–617,751–759` supplies original surface ID ordinal+1
and primitive ID `(surfaceID << 32) | localTriangleIndexPlusOne`, plus exact
positions, optical class and geometric error. Its certification key includes
map, geometry/extraction options, material/cull facts, positions and indices;
it does **not** hash captured vertex shading normals or face basis. An exact-side
witness must bind its orientation evidence separately, not assume that key alone
certifies unchanged side records.

`GeometryKey` explicitly serializes U64/double values, chains hashes in bounded
4096-byte storage, and normalizes signed zero; it is a semantic dependency key,
not a file SHA-256. Support keys propagate into Shape, ShapeSelection, Joint,
Producer provider fingerprints, ShadowCache and diagnostic deduplication. Field
provenance records the support digest. A contract change must be represented in
those dependency identities, not only in prose or an unused version field.

## Rejected candidate witness and its unclosed obligations

The examined candidate is **design-only `ModernNamedTrianglePlane/1`**, not a
production schema or accepted repair freeze. Its authority would be the exact
dyadic values of the immutable named original binary32 triangle vertices,
original surface/primitive/index identities and source-side evidence. Neither
the rounded `Contract::outward` nor an adjusted anchor would define that plane.
Both original triangles per housing must match the pinned union; a nearby plane
or a different triangle with identical coordinates cannot replace identity.

For first triangle `(a,b,c)`, define `N = sign * ((b-a) cross (c-a))`,
`S(x) = N dot (x-a)`. Choose `sign` only from unchanged
`SourceOrientation/2` evidence; contradictory/absent orientation is unavailable.
Positive S is the emitted side. Exact coplanarity is `S(v)=0`, not
`abs(S(v)) < epsilon`. Stand-off is `S(p)/sqrt(N dot N)`; exact projection is
`q = p - N*S(p)/(N dot N)`. A displaced anchor must be rejected by its named
vertex binding even when its displacement is tangential to the plane.

Python rational arithmetic proves these statements for the inspected dyadic
inputs with **zero arithmetic error**. That is not a portable bounded C++
implementation: binary32 exponents span a much wider domain, exact determinants
can need more than fixed 64-bit intermediates, and normalization, metric
distances, clipping, cylinders and float conversion introduce different errors.
No tested finite scale/translation/conditioning domain, overflow/subnormal
policy or charged bounded implementation for all consumers has been frozen.
Uncertain signs must remain unavailable; no epsilon can silently become contact.

A modern opt-in would have to keep the current float contract's authority and
key bytes for old/historical callers, bind map/geometry/fixture plus every named
primitive and original coordinate/orientation record, distinguish witness
version/domain/algorithm in its key, and reject missing/stale/unknown versions.
It must invalidate derived prepared/reuse identities whenever witness data or
semantics changes, including mutations with unchanged float outward. Historical
callers must retain their old path, not automatically consume the witness.
Current `Check` has no model-law parameter, so this routing also needs an explicit
policy boundary. **These are requirements, not a completed design.** No new
contract fields, semantic version, portable numerical error proof, complete
stale-cache migration rule or production-file allowlist is approved.

In particular, an exact visual-plane witness must not rewrite a rounded retained
BSP brush plane, enlarge the brush, clamp negative distances or exclude a world
brush. The existing float SpatialSupport contact channel does not transmit the
candidate authority. Proving a conservative bridge, or proving that an unchanged
bridge is sufficient for the exact scoped use, is uncompleted. The small initial
normal residual supplies no such proof. For these fixed stand-off points, retained
independent Free queries are useful prior evidence, not certification of every
later operation under a changed contract.

## Fresh independent arithmetic and adversarial exit matrix

Reproduce from repository root with `python build/ar42-v1/arithmetic.py`.
It uses the Python standard library, reads only the two BSP source surface,
vertex, normal and index records, verifies them against completed AR-41 geometry,
and writes `build/ar42-v1/arithmetic.json`. No fitter or replacement admission
executable is built. The AR-41 causal native diagnostic is not rerun.

Both original unions have six exactly zero determinant residuals. Exact
projection lies in the original named union; signed stand-off is
**4.000018310402552 WU** for both. Rounding that exact projection to binary32
produces signed distance **-2.257676522442956e-5 WU** from the exact witness.
For nearest rounding, let `h_i` be half the larger spacing to the two neighbors
of rounded coordinate i. Since `S(q)=0`,
`abs(S(round32(q))) <= sum(abs(N_i)*h_i)`. Dividing by `length(N)` yields
**3.135661836726328e-5 WU** here, which encloses the measured residual.
This is a bound on a representation conversion, **not an admission tolerance**.
The negative projection residual does not itself prove occupied-source
penetration: the occupied source is the unchanged stand-off point.

| Case | Fresh AR-42 arithmetic / retained evidence | Full native requirement, **not performed here** |
|---|---|---|
| Axial and equal-component oblique positives | Owned exact determinants zero; completed AR-41 native positives retained | Preserve full certified-side admission, existing response and old scores, not only coplanarity. |
| Unequal-component oblique and both originals | Owned unequal-component exact determinant zero; both BSP unions/indices match completed AR-41 originals, positive side and in-union projection | Both original singleton fixtures and their joint configuration must fully admit under unchanged work/memory/ray/unknown gates; no plane-only exit. |
| Anchor one binary32 ULP each direction | Six moves per original: X/Z moves change exact plane; Y moves leave plane equal but all six break pinned anchor identity. Owned axial/equal/unequal normal-coordinate moves give nonzero determinant | Reject shifted declarations, including tangential identity changes; retain old native ULP refusals. Do not rederive a replacement anchor and hide the mutation. |
| True nonplanarity | One ULP on the second original triangle's non-shared vertex gives a nonzero exact determinant in both housings | Refuse full union; no nearby-plane clustering or dropped triangle. |
| Wrong side | Exact signed scalar reverses under opposite side; AR-41 native wrong-side refusals retained | Contradictory side metadata and behind-plane occupied support refuse; no source relocation. |
| Absent/wrong surface, primitive, map or geometry identity | Actual identity mapping and BSP equality checked; arbitrary ID changes are not geometric evidence. No new native identity controls run | Reject absent or wrong identity even with coincident geometry; stale map/generation/lease/version must fail. |
| Opaque exact rear edge; penetration; nearby gap; coplanar overlay | Seven owned halfspace controls: exact rear boundary alone satisfies necessary conditions; tiny positive penetration, negative gap, overlay, unknown optics, error envelope and two-sided case do not | Retain intersections/brush penetration refusals and conservative unknowns. A gap may be disjoint, but cannot be called contact. Test actual SAT/front/domain interactions. |
| Unknown geometry/optics | Retained local issue mask 16 remains; exact signs do not remove alpha-tested triangles. Halfspace unknown-optics control gets no exemption | Preserve incomplete extraction, approximation, dynamic/bake occupancy Unknown/Conditional and unavailable responses; no unknown-as-empty or unavailable-as-black. |
| Scale/translation | Five power-of-two scales retain exact planarity; three float translations tested. At 2^30 the original edge displacement changes despite zero determinant | Nondegeneracy/planarity alone is insufficient. Freeze and test finite domain, orientation conditioning, extremes, cancellation, overflow/subnormals and conversion enclosures before accepting a general rule. No scaled stock scene is a new ROI or fit. |
| Projection/front/brush/keys | Exact-to-float residual and conversion enclosure measured; consumer/key map inspected | No future full-native result or cache mutation/regression pass exists. These unclosed obligations prevent a repair freeze. |

The script contains **16 labelled mathematical records, eight transformations,
12 original anchor shifts, two nonplanarity perturbations and exact original
projection calculations**. These are not that many independent stock successes.
No general native-admission predicate is implemented by the script. Necessary
halfspace conditions are not SAT, BSP, occupancy or optical certificates.

ASR18's old axial stock proof is retained: 620 candidates, 328 newly admitted
original sheets explained by 3,374 exact opaque rear contacts, 168 unchanged
Unknown, all 59 Solid and 17 Mount refusals retained. Those recorded results were
read and hash-checked, not rerun. ASR24/25's two old conditional configurations
retain 201 unique fitting rows including 65 held-out; duel10's duplicated report
is credited once. Retained on MSE 95.075591 / 1000.383821, removal MSE
7837.108618 / 3923.111111, and duel7 maximum residual 177.592888 codes are
regression evidence, not fresh AR-42 scores. Any hypothetical repair would also
need the existing native emitter-support, aesthetic, sparse, shape, selection,
occupancy, receiver-support and cache/historical controls, plus the entire
matrix above and unchanged saved-parameter old-axial replay. No such run occurred.

## Authorized consequence and limits

**Production-file allowlist: empty. Repair prompt: none authorized.**
Only this decision, its versioned evidence/manifest, private mathematical audit
and the living plan/log changes are in scope. Missing frozen capture and the
unclosed numeric-domain/interface/key proof are explicit failures of the positive
exit. There is no demonstrated complete safe four-hour repair. This does not
claim that a future independently scoped engineering effort is mathematically
impossible, or that the available engineering budget was exhausted.

Stop this pilot now. AR-35 remains Blocked; no serial research ticket, replacement
admission executable, implementation, original-target fit, new ROI, source
relocation, added model capacity, appearance pivot or corpus task follows.
The broader roadmap remains Deferred. The existing total prospective ceilings
of one M integration/eight hours and one pre-target M repair/four hours are
unchanged and unspent by AR-42; they are not renewed or authorized by this Stop.
Native fit/sampling work caps 300,000,000 / 16,000,000, all existing memory/ray
and per-check gates, timeout/cancellation semantics and unlike-work accounting
remain unchanged.

A changed response/support policy would require an explicit separate freeze
before any original-target exposure. Any final-informed change requires fresh
adequate independent final evidence; the v1 reserve lacks housing-988 controls
and can never authorize that repair fit. This rule is preserved, not an approved
next task. No standalone positive M repair prompt is issued because its required
proof and input gates failed.

Both fixtures remain visible **A candidates with D/original-data fallback**.
There are zero new accepted sources, rows, area or maps, no B lamp and no C asset.
Fold counts 40/41/34/26 and control/unknown/coverage counts remain unchanged;
18.78% unavailable trim sample rows is not a world-area fraction. Historical
type/XYZ/power/exactLight is not required for aesthetic eligibility. RGB8 remains
encoded appearance, not calibrated irradiance. Historical behavior, canonical
meaning/storage, BSP/lightmaps and runtime are untouched by AR-42. No double
lighting work or historical-truth promotion occurred.

Codex was the sole operator. Assistance was source/evidence inspection and
private exact arithmetic; no subagents, independent reviewer, new visual ratings,
accepted product output or measured engineering-duration estimate is claimed.
The receipt records the measured decision interval against the 7,200-second cap,
the private script timings, integrity failures and failed scratch attempts.
