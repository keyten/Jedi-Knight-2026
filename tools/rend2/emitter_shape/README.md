# S08 extended source CPU experiments

Production C++ `Shape::Prepare`, `Block::Evaluate`, `Shape::Fit` and trusted
`EvaluateFrozen` / `Select` compare fixed-count point/spot/rectangle/tube models.
No Python fitting implementation, GL, runtime light or external numerical package.

```powershell
cmake --build build/s06-gcc --target lightrecon_shape_test --parallel 2
python -X utf8 tools/rend2/emitter_shape/benchmark.py --tool build/s06-gcc/lightrecon_shape_test.exe --out tools/rend2/emitter_shape/results/gcc
python -X utf8 -m unittest discover -s tools/rend2/emitter_shape -p 'test_*.py' -v
```

Owned analytical geometry only. Actual q3map2 spot/polygon kernels are reused,
but no native historical cylinder kernel is asserted. Finite Lambertian cylinder
and rectangle integration is a separate explicit model. Compiler-relative Q,
relative emitted radiance and calibrated cd/nits remain different semantics.

See [the support matrix, equations, measurements and limitations](../../../docs/rend2-emitter-shapes-s08.md).
Numerical witnesses exclude timing/RSS; resource measurements are separate.
