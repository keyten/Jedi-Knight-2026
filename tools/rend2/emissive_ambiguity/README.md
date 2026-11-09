# ASR06 emissive-versus-reflected-light ambiguity

Read the [research report](../../../docs/rend2-emissive-ambiguity-asr06.md)
for the ESR-NeRF applicability review, evidence policy and measured limitations.

Python 3.8+ standard library, from the repository root:

```powershell
python -m unittest discover -s tools/rend2/emissive_ambiguity -p 'test_*.py' -v
python tools/rend2/emissive_ambiguity/study.py --output build/asr06/results.json
python tools/rend2/emissive_ambiguity/study.py --output build/asr06/repeat.json
Get-FileHash build/asr06/results.json,build/asr06/repeat.json -Algorithm SHA256
```

[`results.json`](results.json) is the checked deterministic witness. It contains
four scene families at three intensities, proposal-level false-source rates,
recall, interval errors, explicit abstentions and an equal-outgoing-radiance
counterexample. [`sources.json`](sources.json) identifies the reviewed primary
paper and the inspected repository inputs. `study.py` defaults to writing the
witness; use `--output` to retain the checked copy during exploratory runs.

This is a bounded analytical study using exact rectangular source support,
fixed primary illumination and declared reflectance. The existing inverse-GI
`transport.Transfer` supplies reciprocal passive transfer and power guards.
The reference integrates 24x24 sender points; fitting uses 8x8. The receiver
operator is an explicit midpoint approximation. The synthetic quantizer and
ideal directional proxy are **not** the historical JA observation operators.

The diagnostic accepts training evidence only. Independent held-out receiver
RGB, directional proxies and source labels are evaluated after the decision.
It cannot certify an exact historical lamp, remove authored provenance, write
canonical assets, modify the historical solver or affect world shading.
Unknown geometry, reflectance, roots and encoding cause abstention. Missing
transfer in an exported map must never be interpreted as this complete toy
network's absent edges. The conservative result is 0/9 false sources with 2/3
true sources retained; the dim source remains ambiguous.
