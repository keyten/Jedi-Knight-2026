# Historical baked-light model fixtures

The [versioned specification](../../../docs/rend2-bake-model.md) distinguishes
source-supported q3map/q3map2 models from unknown stock-JA production bakes.
These Python tools are offline research fixtures. They do not change rendering,
decoding, reconstruction, or the physical PBR contract.

Run from the repository root with Python 3.8+ (standard library only):

```powershell
python tools/rend2/bake_model/test_kernels.py -v
$q3 = Resolve-Path build/toolchain/netradiant-20220628/portable/netradiant_1.5.0-20220628-windows-amd64/q3map2.exe
python tools/rend2/bake_model/compare_bakes.py --q3map2 $q3 --output build/new-bake-model-comparison
```

The compiler is optional to the seven numerical tests and required for the real
BSP comparison. Installation, official download and verified package checksum:
[existing q3map2 validation](../../../docs/rend2-q3map2-validation.md).
The binary's neighboring DLLs must remain installed. A different compiler hash
is reported unverified even if numerical comparisons pass. There is no automatic
compiler download and no licensed game content is needed.

`compare_bakes.py` creates 22 sealed self-authored `.map` fixtures, TGA textures
and shaders, then executes BSP/VIS/LIGHT for each. Each case independently fixes
the authored source values, attenuation mode and output transfer. No gain is
fitted. It checks original BSP RGB8 against forward predictions, grid/deluxe
direction paths where supported, wrong-falloff residuals, light retention, and
paired bounce/grid controls. One declared sky/wall boundary band is reported
unchecked, with its discrepancy retained. RBSP's source-defined approximate
grid merging has a separate four-byte/four-bin tolerance plus one bin for float
roundoff. See the specification for validation scope and numerical results.

Use `--case inverse_square --case linear` for a smaller run. Bounce selections
must also include `--case inverse_square`. Use a new output directory: the tool
refuses to overwrite a prior witness. It writes maps, unmodified BSPs, command
logs, full argv, input/output hashes and `result.json` under that directory.
The supplied `validation.json` is a compact witness, not a full compiled dataset
and not stock-bake certification. Kernel functions are not a full compiler:
tracing, general sample placement, material transport and full writer replay
remain explicit future work.

`profiles.json` is a proposed profile catalogue; it is not a production schema
or automatic classifier. `source-lock.json` records immutable upstream sources
and LF-normalized hashes. Fetch the recorded URLs to reproduce source inspection;
`inspection_path` gives the ignored local cache used in this investigation.
No external source or executable is redistributed.

Optional read-only inventory of licensed private archives:

```powershell
python tools/rend2/bake_model/inspect_stock.py --pk3 '<private>/assets0.pk3' --pk3 '<private>/assets1.pk3' --pk3 '<private>/assets2.pk3' --pk3 '<private>/assets3.pk3' --map maps/hoth2.bsp --map maps/mp/ffa1.bsp --output build/new-stock-inventory.json
```

Archives are read in the explicit order, later ones taking precedence. The tool
publishes hashes/counts and any compiler provenance fields, never map/texture or
entity content. An absence of retained sources is not evidence for a particular
compiler. `stock-inventory.json` records five local observations, all unverified
UnknownProfile. Generated authoring fixtures are GPL-2.0-or-later; repository
licensing applies to tools. Keep source answers/recipes outside blind solver
inputs, as in the [existing dataset workflow](../datasets/README.md).
