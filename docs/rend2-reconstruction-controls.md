# Canonical reconstructed-lighting controls

These controls govern the [canonical asset/API](rend2-reconstructed-lighting-api.md),
independently of generating/loading it and independently of each renderer feature.
They do not change the experimental lightgrid pipeline: `r_staticLightReconstruction`,
`r_recoveredVolumetricLights`, entity L1 probes, portal shafts and their existing
shader defines keep their current meanings and defaults.

## Cvars and activation

All **12** new cvars default to **`0`**, use **`CVAR_ARCHIVE`**, and have integer
range checks. `r_lightReconBuild` accepts `0..3`; all others accept `0..1`.
SP and MP include the same registration list, `tr_lightrecon_cvars.inc`, through
`tr_lightrecon_register.inc`. SP's three-argument `Cvar_Get` does not support
descriptions; MP's four-argument API receives the descriptions in that list.

| Cvar | Meaning / minimum data gate |
| --- | --- |
| `r_lightReconBuild` | Build/load policy below, sampled only at main-world map load/reload |
| `r_lightReconUse` | Live master for consumption; never requests build/load |
| `r_lightReconFog` | Prepared `IncomingRadianceSampling` for scattering |
| `r_lightReconEntities` | Prepared `IndirectSampling` for entities/Ghoul2 |
| `r_lightReconSpecular` | Prepared `IncomingRadianceSampling` for specular |
| `r_lightReconShadows` | Usable physical source and prepared `VisibilityQueries` |
| `r_lightReconSun` | Unique resolved explicit sun with usable physical emission |
| `r_lightReconParticles` | Prepared `IncomingRadianceSampling` for particles |
| `r_lightReconIndirect` | Prepared `IndirectSampling` for indirect surface lighting |
| `r_lightReconWorldDiffuse` | Usable physical sources for direct world diffuse |
| `r_lightReconArea` | Usable rectangle/tube source for area/LTC lighting |
| `r_lightReconEnvironment` | Unique resolved environment and prepared `IncomingRadianceSampling` for IBL |

Area/LTC is separate because its source selection, integration and resource
requirements differ from point/spot world diffuse. Environment/IBL is separate
because sky radiance and probe resources differ from local-source specular and
indirect irradiance. Enabling either grants no permission to another consumer.
These are minimum gates for the initial contracts, not a promise that a field
contains every direct/indirect component. Future direct-source adapters can extend
their data requirements centrally; they must explicitly handle attribution and
double counting before mixing with baked lighting.

No new shader permutation, sampler or map-load GPU layout is introduced here,
so none of these cvars needs `CVAR_LATCH` today. Master and consumer requests
are live; disabling one does not change other requests or the shared asset.
Before adding an adapter that requires startup permutations or map resources,
it must arrange their lifetime/latching and report feature availability only
when the actual required configuration is ready. These controls never set
`r_pbrPhysical`, froxel fog, Forward+, LTC, IBL or any other renderer cvar.

Changing `r_lightReconBuild` does **not** build, load, unload or replace the
current asset immediately. Reload/load a map to apply it. `vid_restart` applies
it only if it causes a new main-world load; renderer initialization alone does
not apply the policy. Build policy deliberately is not `CVAR_LATCH`: an ordinary
map reload must see the new value without requiring renderer cvar re-registration.
For immediate consumption shutdown use `r_lightReconUse 0`.

## Build/load policy and failure semantics

`ApplyBuildPolicy(BuildPolicy, Manager&, BuildBackend*)` is a synchronous CPU
policy boundary invoked by the renderer owner at main-world load. It does not
consult master or consumer requests. An offline host may invoke the same policy
with an explicit backend bound to its immutable dataset/revision.

| Value | Behaviour |
| --- | --- |
| `0` Disabled | No backend call; current publication becomes `Disabled` |
| `1` CacheOnly | Call only `LoadCache`; a miss, stale/invalid cache or failure never calls `Build` |
| `2` CacheOrBuild | Try cache; build on missing, unavailable, unsupported or corrupt cache, including failed asset validation |
| `3` ForceRebuild | Skip cache; call `Build` once; a failure never falls back to cached/stale lighting |

`LoadCache` must never invoke reconstruction internally. It must check input
revision, calibration and algorithm compatibility as well as schema validity;
the core cannot infer cache freshness from a BSP or a pathname. There is no
cache writer, implicit retry, background job or lazy build from a query.

`BuildProduct` holds an immutable asset, optional prepared CPU evaluator,
`Completeness` and diagnostic. Only `Ok` with a valid product is published through
the manager's copy/validate boundary. A valid empty product is legal and means
loaded metadata, not usable lighting. Unknown emission, unresolved hypotheses
and missing field evaluators do not become fake sources. Invalid products,
provider contract violations or exceptions report `Corrupt`.

During an enabled policy the owner exposes `Pending`. Final failures clear the
current result (`Corrupt` for corrupt data/provider failure, otherwise `Missing`)
and retain the precise `Status` in `BuildReport`. They never expose a stale
current asset. Existing snapshot leases remain safely pinned. `BuildReport`
records which callbacks were actually called and each status; consumer queries
and the diagnostic command never invoke them. Clearing a world exposes `Unloaded`.

**Current production state:** no canonical cache loader, solver or rendering
adapter is installed. Policy 0 publishes `Disabled`; policies 1–3 report
`Unavailable`, leave the asset `Missing`, and make no backend calls. In particular
policy 1 cannot accidentally run the experimental solver. The old experimental
pipeline still runs under its own existing controls. No improved map lighting,
cache persistence or runtime integration is implemented by this change.

## Single consumption rule

Renderer consumers acquire `R_ReconstructedLighting()` and call
`R_LightReconConsumer(snapshot, Consumer)` on the **same lease** they will read.
It collects the cvar requests and actual adapter support and delegates to the
CPU-only `EvaluateConsumer`:

```text
effective = master AND consumer-request AND loaded-valid-asset
            AND required-capability AND actual-feature/adapter-ready
```

Build policy is intentionally absent from this equation: a loaded result can
exist with all consumers off. `ConsumerState` separately reports `requested`,
`master`, `assetLoaded`, `capabilityPresent`, `featureAvailable`, `effective`,
asset/capability statuses and `GateReason`. `Ready` and `Partial` assets can both
have useful capabilities; a validated empty asset has no lighting capability.

Metadata presence alone is insufficient for sampling. A usable exact source
must be resolved and marked `exactLight=Candidate`; canonical validation already
requires known intensity/colour, required position/frame/size, physical units
and supported geometry/angular distribution. No confidence values are modified
or inferred here. Sun selection refuses ambiguity and unknown emission.
Sampling capability still does not guarantee data at every queried location:
consumers must honor each query's explicit failure status.

The primary fallback reason is selected in this order: invalid consumer,
consumer off, master off, asset unavailable, required capability unavailable,
renderer feature/adapter unavailable. Independent status fields still expose
the other missing prerequisites. Feature support is false for every current
production adapter; enabling requests allocates no lighting/shadow resources.

The core policy/control module includes no renderer globals or GL/engine types.
Effective-state reads allocate no arrays, copy no emitters/volumes and call no
evaluator, builder, disk or GPU service. They inspect metadata through const
leases (physical-source checks scan the emitter cursor), while prepared query
capabilities are cached at publication. Retained historical leases can still
be read after reload; the renderer must acquire the current snapshot for new
work and retain that same lease through use.

## Diagnostic and validation

`r_lightReconInfo` prints requested and last-applied build policy, build/load
status and callback counts, asset state/validity, metadata counts and each
consumer's requested/effective/capability/feature states and fallback reason.
Metadata counts are explicitly not usable-light counts. It does not call an
empty asset “active,” start work, or change any renderer setting.

Exact standalone and SP/MP build commands are in
[the core build instructions](rend2-reconstruction-core.md#build-and-run).
`lightrecon_control_test` adds policy-mode/failure cross products, cache-only
call-count checks, provider/validation errors, master/independent consumers,
empty/missing/corrupt/pending/unloaded assets, absent/unsupported sampling,
unknown/candidate sources, sun ambiguity and retained leases. It compiles the
actual common registration body against both SP and MP `Cvar_Get` signatures
and checks names, defaults, flags, ranges and description handling. Both real
renderer registrations are additionally compiled in the SP/MP target builds.

Validated on Windows x64, 2026-10-22:

| Configuration | Result |
| --- | --- |
| Standalone MSVC 2019 Release | 4/4 CTest programs passed |
| Standalone GCC 16.2 C++11, `-Wall -Wextra -Werror -pedantic` | 4/4 CTest programs passed |
| SP + MP MSVC 2019 Release | `rdsp-rend2_x86_64` and `rd-rend2_x86_64` compiled/linked; 4/4 CTest programs passed |
| Per configuration test counts | Canonical 252, API 1730, core 533, controls 3341: **5856 checks, 0 failures** |

The existing MSVC x64 `/arch:SSE2` ignored-option warnings remain. No in-game
visual test was run; no shader or existing lighting computation was changed.

For the exact local checkout/toolchain used here, from the repository root:

```powershell
$cmake = 'C:\Users\Keyten\Desktop\projects\OpenJK\build\toolchain\w64devkit\bin\cmake.exe'
$ctest = 'C:\Users\Keyten\Desktop\projects\OpenJK\build\toolchain\w64devkit\bin\ctest.exe'
& $cmake --build build/lightrecon-standalone --config Release --parallel 4
& $ctest --test-dir build/lightrecon-standalone -C Release --output-on-failure --timeout 30
& $cmake --build build/lightrecon-core-renderers --config Release --parallel 4 `
  --target rdsp-rend2_x86_64 rd-rend2_x86_64 lightrecon_test lightrecon_api_test lightrecon_core_test lightrecon_control_test
& $ctest --test-dir build/lightrecon-core-renderers -C Release --output-on-failure --timeout 30

$env:PATH = 'C:\Users\Keyten\Desktop\projects\OpenJK\build\toolchain\w64devkit\bin;' + $env:PATH
& $cmake -S tools/lightrecon_test -B build/lightrecon-standalone-gcc -G 'MinGW Makefiles' `
  '-DCMAKE_CXX_COMPILER=C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin/g++.exe' `
  '-DCMAKE_MAKE_PROGRAM=C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin/mingw32-make.exe' `
  '-DCMAKE_CXX_FLAGS=-B C:/Users/Keyten/Desktop/projects/OpenJK/build/toolchain/w64devkit/bin/ -Wall -Wextra -Werror -pedantic' `
  -DCMAKE_BUILD_TYPE=Release
& $cmake --build build/lightrecon-standalone-gcc --parallel 4
& $ctest --test-dir build/lightrecon-standalone-gcc --output-on-failure --timeout 30
```

The first two build directories use the configurations in the linked core build
instructions; no engine or OpenGL startup is involved in any CPU test.

Changed implementation files: `lightrecon/control.h/.cpp` and its core CMake list;
`tr_lightrecon.h/.cpp`, shared cvar/registration `.inc` files, `tr_local.h` and the
static-lighting lifecycle adapter; both SP/MP `tr_init.cpp` and CMake lists;
the new control test, test CMake/README and linked architecture documentation.

The [disabled-rendering regression suite](rend2-reconstruction-regression.md)
adds versioned preprocessed shader/interface baselines, GPU compilation and
actual SP A/B/A visual checks for this control boundary.
