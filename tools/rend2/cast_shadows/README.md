# ASR05 cast-shadow constraints

Read the [paper conclusions, mathematics, measured results and diagnostic
policy](../../../docs/rend2-cast-shadow-constraints-asr05.md).

`study.py` is an offline analytical beam/receiver-wall fixture, a bounded
horizontal-strip diagnostic and explicit oracle correspondence ablations.
It uses known complete geometry in metres and a declared relative linear
quantizer. It does not load BSPs, infer automatic caster truth, write canonical
assets or change the historical solver. Unknown geometry is never rendered as
empty. `Compatible` never certifies an original historical source.

Python 3.8+ standard library, from repository root:

```powershell
python -m unittest discover -s tools/rend2/cast_shadows -p 'test_*.py' -v
python tools/rend2/cast_shadows/study.py --output build/asr05/results.json
python tools/rend2/cast_shadows/study.py --output build/asr05/repeat.json
Get-FileHash build/asr05/results.json,build/asr05/repeat.json -Algorithm SHA256
```

The only repository code dependency is the existing small geometry helpers in
`light_localization_research/shadow_localization.py`; it is not modified.
[`results.json`](results.json) is the deterministic research witness. No input
asset, answer file, renderer, compiler, network access or third-party Python
package is needed. Held-out visibility/RGB evaluation happens after parameter
selection. Oracle source/caster labels are supplied, never reported as inferred.
[`sources.json`](sources.json) records reviewed paper versions/sections and
LF-normalized hashes of the existing code inspected for this study.

The full grid retains 8,924 shading-compatible candidates; two supplied beam
endpoint matches retain one on the coarse grid. The finer local grid retains
52 positions, so the coarse result does not establish exact localization. The
no-caster angular-mask counterexample is byte-identical at all 135 strip samples.
False-positive and Unknown controls, finite edge reprojection, ray hit identity,
degenerate correspondences and nonblack mixed-source shadows have numerical tests.
