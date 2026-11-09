# ASR20 aesthetic fixture fitting

See [the objective, API and limitations](../../../docs/rend2-aesthetic-fit-asr20.md).

`run.py --out <fresh-directory>` freezes and repeats owned and pinned compiled
witnesses using the native `lightrecon_aesthetic_tool` and
`lightrecon_aesthetic_test` targets. `--stock-archive <mp/duel4.jkobs>` also audits
one frozen mounted stock region; its geometry and sample files remain private.

The CLI input starts with `JKASR20-ENCODED-ASSUMED-MATERIAL-1`, followed by fixed
gain, RGB background upper bounds, and fixture count. Each fixture is
`point id x y z upperR upperG upperB` or
`surface id originalSurfaceOrdinal upperR upperG upperB`. All nonempty fixture
subsets (at most four supplied fixtures) are compared. A fold starts with its row
count, then `id block surface x y z nx ny nz area rawR rawG rawB` per row.
The fit file contains training then validation; a separate file holds held-out
rows. Only one comparable original receiver material is accepted. Each fold must
have disjoint IDs and spatial blocks. `--export-region` is a trusted geometry-only
stock subset exporter, not an optimizer or an asset writer.

More general relative-linear targets, fixed supported tubes, and independently
bounded spot alternatives are supplied through the tool-only C++ API.
