# X19 runtime-query and MP controls

`run.py` measures the unchanged V02 native query workload, using a saved
pre-change executable and the current `lightrecon_validation_tool`. It never
launches a game and records all MP cases as Unperformed without permission.
Use fresh output directories:

```powershell
python tools/rend2/runtime_hardening/run.py --before build/x19-baseline/lightrecon_validation_tool.exe --out build/x19-queries-final
python -m unittest tools.rend2.validation.test_mp_control tools.rend2.validation.test_report
python tools/rend2/runtime_hardening/report.py --out tools/rend2/runtime_hardening/results/v1
```

The baseline executable must come from pre-X19 revision `07241cc39`. Its hash is
recorded before work; no executable is committed. Query counts, successes and
Unavailable counts must match. The after workload is now also a CTest allocation
regression, with 4096 batches of 8 queries and 128 warm-up calls.

MP runtime is separate. The hardened `validation/runtime_mp.py` still requires
`--allow-mp-launch`, including for direct programmatic calls. Default launch
budget is one. An invalid earlier control stops subsequent cases; unexecuted
cases remain Unperformed. A declared camera is currently available for mp/duel4.
The harness changes no system network/firewall settings or net_ip/net_port cvars,
and never responds to Windows permission prompts. Do not run it without explicit
user authorization.

Passing synthetic log/image classification tests does not establish an actual
MP frozen-world A/A control. The measured report retains that distinction.
