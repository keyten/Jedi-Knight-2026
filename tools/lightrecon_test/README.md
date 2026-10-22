# Standalone reconstructed-lighting CPU tests

Run from the repository root (MSVC 2019 x64):

```powershell
cmake -S tools/lightrecon_test -B build/lightrecon-standalone -G "Visual Studio 16 2019" -A x64
cmake --build build/lightrecon-standalone --config Release --parallel 4
ctest --test-dir build/lightrecon-standalone -C Release --output-on-failure
```

On other platforms omit `-G ... -A x64`. CMake must be on PATH or invoked using
its absolute executable path. This project uses only C++11 and the standard
library: no OpenGL context, engine, SDL, Boost or game assets are required.

All executables link the one `rend2_lightrecon_core` static target:

- `lightrecon_test`: canonical asset and photometric contracts.
- `lightrecon_api_test`: read-only snapshots, queries and lifetime.
- `lightrecon_core_test`: streaming input, validation, sessions and host errors.

See [core architecture and complete build commands](../../docs/rend2-reconstruction-core.md).
The old `tools/volrecon_test` tests the experimental solver separately and is
unchanged. The new core has no default inverse solver and returns Unavailable.
