"""CPU noise regression/benchmark using the actual C++ implementation.

Needs g++ (on PATH, --cxx, or beside the configured CMake executable).
GPU medium/LOD/RG8 checks live in test_volumetric_compute.py.
"""
import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cxx', default=shutil.which('g++'))
    args = parser.parse_args()
    if not args.cxx:
        for cache in (ROOT / 'build').glob('*/CMakeCache.txt'):
            match = re.search(r'^CMAKE_COMMAND:INTERNAL=(.+)$', cache.read_text(), re.M)
            candidate = Path(match[1].strip()).with_name('g++.exe') if match else None
            if candidate and candidate.exists():
                args.cxx = str(candidate)
                break
    if not args.cxx:
        parser.error('g++ not found; supply --cxx')
    code = (ROOT / 'shared/rd-rend2/tr_volumetric.cpp').read_text()
    noise = '#define FROXEL_NOISE_SIZE' + code.split('#define FROXEL_NOISE_SIZE', 1)[1].split(
        'static void R_CreateVolumetricNoiseImage', 1)[0]
    wind = '#define FROXEL_NOISE_COS30' + code.split('#define FROXEL_NOISE_COS30', 1)[1].split(
        'static qboolean R_VolumetricNoise(', 1)[0]
    finest = 'const float finestPeriod =' + code.split('const float finestPeriod =', 1)[1].split(
        'const float lambda', 1)[0]
    source = r'''
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
typedef unsigned char byte;
typedef int qboolean;
const int qtrue = 1, qfalse = 0, TAG_TEMP_WORKSPACE = 0;
#define MAX(a,b) ((a) > (b) ? (a) : (b))
#define MIN(a,b) ((a) < (b) ? (a) : (b))
float Com_Clamp(float a, float b, float x) { return MAX(a, MIN(b, x)); }
void *Z_Malloc(size_t n, int, int) { return malloc(n); }
void Z_Free(void *p) { free(p); }
#define Com_Memset memset
#define VectorCopy(a,b) memcpy(b,a,3*sizeof(float))
struct world_t {};
struct { world_t *world; } tr;
struct VolumetricFogBlock { float noiseMacroOffset[4], noiseDetailOffset[4]; };
''' + noise + wind + '\nfloat finestFeature(float macroContrast, float detailContrast, float macroPeriod, float detailPeriod) {\n' + finest + r'''
return finest;
}
int main() {
    const uint32_t seeds[2] = {0x5f3759dfu, 0x9e3779b9u};
    double maxError = 0;
    const float contrasts[] = {0.001f, .01f, .05f, .1f, .25f, .5f, .75f, 1,
                              1.25f, 1.5f, 2, 2.75f, 3, 3.9f, 4};
    const double alpha[3] = {.8191725133961645, .6710436067037893, .5497004779019703};
    double totalFast = 0, totalReference = 0;
    for (int field = 0; field < 2; field++) {
        int offset = 0;
        for (int l = 0; l < FROXEL_NOISE_LEVELS; l++) {
            const int size = FROXEL_NOISE_SIZE >> l;
            s_noise.levels[field][l] = s_noise.chain[field] + offset;
            offset += size * size * size;
        }
        assert(offset == FROXEL_NOISE_CHAIN);
        R_NoiseGenerateField(s_noise.levels[field][0], seeds[field]);
        int histogram[256] = {};
        for (int i = 0; i < FROXEL_NOISE_TEXELS; i++) histogram[s_noise.chain[field][i]]++;
        for (int n : histogram) assert(n == 1024);
        for (int l = 1; l < FROXEL_NOISE_LEVELS; l++)
            R_NoiseDownsample(s_noise.levels[field][l-1], FROXEL_NOISE_SIZE >> (l-1), s_noise.levels[field][l]);
        R_NoiseBuildDistribution(field);
        // Actual old estimator, same R3 samples, all half-mip levels.
        std::vector<float> samples[FROXEL_NOISE_NORM_STEPS];
        for (int j = 0; j < FROXEL_NOISE_NORM_STEPS; j++)
            for (int s = 0; s < FROXEL_NOISE_MEAN_SAMPLES; s++) {
                float u[3];
                for (int a = 0; a < 3; a++) {
                    double x = .5 + alpha[a] * s;
                    u[a] = (float)(x - floor(x));
                }
                samples[j].push_back(R_NoiseSampleLod(field, u, .5f*j));
            }
        for (float c : contrasts) {
            float norm[16];
            auto start = std::chrono::steady_clock::now();
            R_NoiseMeasureNorm(field, c, norm);
            totalFast += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count();
            start = std::chrono::steady_clock::now();
            for (int j = 0; j < FROXEL_NOISE_NORM_STEPS; j++) {
                double sum = 0;
                for (float n : samples[j]) sum += R_NoiseContrast(n, c);
                double error = fabs(norm[j]*sum/FROXEL_NOISE_MEAN_SAMPLES - 1);
                maxError = MAX(maxError, error);
                assert(error < .0001); // <0.01% added error, much tighter than the 1% target.
            }
            totalReference += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now()-start).count();
            for (int j = 13; j < 16; j++) assert(norm[j] == norm[12]);
        }
        float norm[16]; R_NoiseMeasureNorm(field, 0, norm);
        for (float n : norm) assert(n == 1);
    }
    printf("PASS: histogram normalization vs full estimator, 2 fields x 13 LODs x 15 contrasts; max added error %.6f%%\n", maxError*100);
    printf("Contrast table: %.3f ms average; reference pow-only pass: %.3f ms (excludes trilinear sampling)\n", totalFast/30, totalReference/30);
    assert(finestFeature(1, 1, 256, 4000) == 16);
    assert(finestFeature(0, 1, 256, 4000) == 250);
    assert(finestFeature(1, 0, 256, 4000) == 16);
    VolumetricFogBlock block = {};
    world_t world, secondWorld; tr.world = &world;
    float v[3] = {};
    R_VolumetricNoiseWind(&block, 120000, v, 1000, 2000);
    v[0] = 100;
    R_VolumetricNoiseWind(&block, 120010, v, 1000, 2000);
    assert(block.noiseMacroOffset[0] == 0); // 0 -> wind: no absolute-time teleport.
    R_VolumetricNoiseWind(&block, 120110, v, 1000, 2000);
    assert(fabs(block.noiseMacroOffset[0] - .01) < 1e-7);
    assert(fabs(block.noiseDetailOffset[1] - .0025) < 1e-7);
    v[0] = -100;
    R_VolumetricNoiseWind(&block, 120110, v, 1000, 2000);
    assert(fabs(block.noiseMacroOffset[0] - .01) < 1e-7);
    R_VolumetricNoiseWind(&block, 121110, v, 1000, 2000);
    assert(fabs(block.noiseMacroOffset[0] - .91) < 1e-7);
    R_VolumetricNoiseWind(&block, 121110, v, 2000, 4000);
    assert(fabs(block.noiseMacroOffset[0] - .91) < 1e-7); // period change preserves tile phase.
    R_VolumetricNoiseWind(&block, 121210, v, 2000, 4000);
    assert(fabs(block.noiseMacroOffset[0] - .905) < 1e-7);
    v[0] = 0;
    R_VolumetricNoiseWind(&block, 121210, v, 2000, 4000);
    R_VolumetricNoiseWind(&block, 99999999, v, 2000, 4000);
    assert(fabs(block.noiseMacroOffset[0] - .905) < 1e-7);
    R_VolumetricNoiseWind(&block, 121300, v, 2000, 4000);
    assert(block.noiseMacroOffset[0] == 0); // time rewind resets epoch.
    v[0] = 100;
    R_VolumetricNoiseWind(&block, 121300, v, 2000, 4000);
    R_VolumetricNoiseWind(&block, 122300, v, 2000, 4000);
    assert(fabs(block.noiseMacroOffset[0] - .05) < 1e-7);
    tr.world = &secondWorld;
    R_VolumetricNoiseWind(&block, 123300, v, 2000, 4000);
    assert(block.noiseMacroOffset[0] == 0);
    puts("PASS: finest active scale; wind edits, reversal, rotation, wrap, repeated time, stop, rewind, new world");
}
'''
    with tempfile.TemporaryDirectory(prefix='noise-check-', dir=ROOT / 'build') as directory:
        cpp, exe = Path(directory) / 'test.cpp', Path(directory) / 'test.exe'
        cpp.write_text(source)
        env = dict(os.environ)
        env['PATH'] = str(Path(args.cxx).resolve().parent) + os.pathsep + env.get('PATH', '')
        subprocess.run([args.cxx, '-std=c++11', '-O2', str(cpp), '-o', str(exe)], check=True, env=env)
        subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
