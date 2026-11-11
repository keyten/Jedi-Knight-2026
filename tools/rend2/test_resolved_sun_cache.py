"""Compile and exercise the ACTUAL production CSM cache classifier/layout key.

Only renderer/GL storage is stubbed; cache decisions and disk key are extracted
unchanged from tr_main.cpp. No parallel implementation of cache policy.
"""
import argparse
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def definition(text, marker, semicolon=False):
    start = text.index(marker)
    brace = text.index('{', start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return text[start:end] + (';' if semicolon else '')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--compiler', type=Path, required=True)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    a.output.mkdir(parents=True, exist_ok=True)
    text = (ROOT / 'shared/rd-rend2/tr_main.cpp').read_text()
    source = r'''
#include <cstdint>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#define Com_Memset std::memset
#define Com_Memcpy std::memcpy
typedef float vec3_t[3];
typedef unsigned char byte;
typedef int qboolean;
const int qfalse=0,qtrue=1,SUN_SHADOW_DISK_VERSION=1;
struct world_t { int unused; } worlds[2];
struct { world_t* world=&worlds[0]; int sunShadowCasterGeneration=1;
    int sunShadowCacheHits=0,sunShadowCacheBypasses=0,sunShadowCacheScrolls=0,sunShadowCacheMisses=0;
    uint64_t sunShadowCacheUpdatedPixels=0; void* sunShadowStaticFbo[2][3]{};
    struct { float sunShadowMvp[3][16],sunShadowSplits[3],sunShadowBlendWidths[2];
        float sunShadowTexelSize[3],sunShadowDepthSpan[3],sunShadowDepthTop; } refdef{}; } tr;
struct { int integer=2; } cacheCvar;
auto* r_sunShadowCache=&cacheCvar;
uint64_t casterSettings=1;
static uint64_t R_SunShadowCasterSettingsKey() { return casterSettings; }
static uint64_t R_SunShadowWorldDiskHash() { return 123; }
static float DotProduct(const float* a,const float* b) { return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
'''
    adapter = (ROOT / 'shared/rd-rend2/tr_lightrecon.cpp').read_text()
    source += definition(adapter, 'struct LegacySunShadowState', True)
    source += '\nstatic LegacySunShadowState s_legacySunShadows{};\n'
    source += definition(adapter, 'void R_CopyLegacySunShadowState(')
    source += definition(text, 'struct sunShadowCacheState_t', True)
    source += '\nstatic sunShadowCacheState_t s_sunShadowCache{};\n'
    for marker, semi in [('enum sunShadowCacheAction_t', True),
                         ('static uint64_t R_SunShadowHashBytes(', False),
                         ('static uint64_t R_SunShadowDiskLayoutKey(', False),
                         ('static sunShadowCacheAction_t R_ClassifySunShadowCache(', False)]:
        source += '\n' + definition(text, marker, semi)
    source += r'''
int checks=0,failures=0;
#define CHECK(x) do { ++checks; if(!(x)) { ++failures; std::printf("FAIL %d: %s\n",__LINE__,#x); } } while(0)
int main() {
    // Real C01 transition helper: an off scene must get its old shadow block,
    // even when it has no authored CSM to overwrite the canonical matrices.
    std::memset(&tr.refdef, 0x3f, sizeof(tr.refdef));
    const auto original=tr.refdef;
    R_CopyLegacySunShadowState(true);
    std::memset(&tr.refdef, 0, sizeof(tr.refdef));
    R_CopyLegacySunShadowState(false);
    CHECK(std::memcmp(&tr.refdef,&original,sizeof(original))==0);
    float mvp[16]{}; mvp[0]=mvp[5]=mvp[10]=mvp[15]=1;
    vec3_t axis[3]={{0,0,-1},{1,0,0},{0,1,0}},origin{},bounds[2]={{-100,-100,-100},{100,100,100}};
    int x,y,bank,oldBank;
    auto classify=[&]() { return R_ClassifySunShadowCache(0,mvp,axis,origin,bounds,1,1,512,512,&x,&y,&bank,&oldBank); };
    CHECK(classify()==SUN_CACHE_FULL);
    for(int i=0;i<1000;++i) CHECK(classify()==SUN_CACHE_HIT);
    CHECK(tr.sunShadowCacheMisses==1);
    // Effective direction changes the projection and the light axes.
    axis[0][0]=.6f; axis[0][2]=-.8f; mvp[0]=.8f;
    CHECK(classify()==SUN_CACHE_FULL); CHECK(classify()==SUN_CACHE_HIT);
    bounds[1][2]+=1; mvp[10]+=.01f;
    CHECK(classify()==SUN_CACHE_FULL);
    ++casterSettings; CHECK(classify()==SUN_CACHE_FULL);
    tr.world=&worlds[1]; CHECK(classify()==SUN_CACHE_FULL);
    ++tr.sunShadowCasterGeneration; CHECK(classify()==SUN_CACHE_FULL);
    // A metadata/photometry change does not appear in the depth key.
    CHECK(classify()==SUN_CACHE_HIT);
    auto key=[&]() { return R_SunShadowDiskLayoutKey(0,axis,bounds,1,1,512,512); };
    auto originalKey=key(); CHECK(key()==originalKey);
    axis[0][0]+=.01f; CHECK(key()!=originalKey); axis[0][0]-=.01f;
    originalKey=key(); bounds[1][0]+=1; CHECK(key()!=originalKey);
    originalKey=key(); ++casterSettings; CHECK(key()!=originalKey);
    CHECK(R_SunShadowDiskLayoutKey(0,axis,bounds,1,1,1024,1024)!=key());
    // Legacy mode 1 bypasses an animated sun, then builds once it settles.
    s_sunShadowCache={}; cacheCvar.integer=1;
    for(int i=0;i<20;++i) { mvp[0]+=.01f; CHECK(classify()==SUN_CACHE_BYPASS); }
    CHECK(classify()==SUN_CACHE_BYPASS); CHECK(classify()==SUN_CACHE_BYPASS);
    CHECK(classify()==SUN_CACHE_FULL); CHECK(classify()==SUN_CACHE_HIT);
    std::printf("C01 production cache: %d checks, %d failures\n",checks,failures);
    return failures?1:0;
}
'''
    cpp = a.output / 'production_sun_cache.cpp'
    exe = a.output / 'production_sun_cache.exe'
    cpp.write_text(source)
    subprocess.run([str(a.compiler), '-std=c++11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-B', str(a.compiler.parent) + '/', str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)


if __name__ == '__main__':
    main()
