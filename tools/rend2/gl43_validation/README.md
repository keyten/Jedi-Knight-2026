# GL 4.3 in-game validation

Copy `forwardplus.cfg` or `water.cfg` into the test installation's active
`base`/`OpenJK` directory and start the SP executable with `+exec <file>`.
Both configs leave the game open after their automated captures so the camera
and the water-body sleep/wake scenario can be checked interactively.

For the Forward+ beauty captures, `timescale 0` keeps the deterministic test
lights fixed between CPU and compute frames. Compare them exactly:

```text
python tools/rend2/gl43_validation/compare_images.py gl43-fplus-cpu-beauty.tga gl43-fplus-gpu-beauty.tga
```

Use `r_speeds 100` while profiling to expose `Forward+ cluster build` and
`Water depth contacts`. Run `r_forwardPlusBenchmark 120` once with
`r_forwardPlusCompute 0` and once with `1`; keep the camera fixed. For water,
also record `com_speeds 1` and the `Water surface` timer at 1920x1080 and
2560x1440. NVIDIA Optimus runs should select the discrete adapter for
`openjk_sp.x86_64.exe` before repeating the same configs.
