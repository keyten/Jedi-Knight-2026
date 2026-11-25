#include "tr_character_cull.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
using namespace CharacterCull;
static int checks = 0;
static void Check(bool condition, const char *name) {
    ++checks;
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}
int main() {
    View view = {{0,0,0}, {{1,0,0},{0,1,0},{0,0,1}}, 90, 70};
    Body body = {{-1000,0,0}, 64, true, false};
    const float player[] = {0,0,0};
    Check(Cull(body,view,player,256,true), "distant body behind camera is removed");
    Check(!Cull(body,view,player,256,false), "disabled restores all bodies");
    Check(!Cull(body,view,player,0,true), "zero range disables approximation");
    body.character=false;
    Check(!Cull(body,view,player,256,true), "independent projectile/FX never culled");
    body.character=true; body.protectedBody=true;
    Check(!Cull(body,view,player,256,true), "own body, jetpack, ATST exemption");
    body.protectedBody=false; body.origin[0]=-256;
    Check(!Cull(body,view,player,256,true), "keep-distance boundary inclusive");
    body.origin[0]=-257;
    Check(Cull(body,view,player,256,true), "outside keep-distance boundary");
    body.origin[0]=1000;
    Check(!Cull(body,view,player,256,true), "far visible body never removed");
    body.origin[1]=1120;
    Check(!Cull(body,view,player,256,true), "edge silhouette and angular margin retained");
    body.origin[1]=2000;
    Check(Cull(body,view,player,256,true), "distant side body removed");
    view.fovX=140; body.origin[0]=300; body.origin[1]=800;
    Check(!Cull(body,view,player,256,true), "wide-FOV side-plane normalization");
    view.fovX=90; body.origin[0]=-700; body.origin[1]=0; body.radius=200;
    Check(!Cull(body,view,player,256,true), "large modded body extends keep distance");
    body.radius=64; body.origin[0]=-400; body.origin[2]=300;
    Check(!Cull(body,view,player,256,true), "elevated caster extends keep distance");
    body.origin[0]=-2800; body.origin[2]=0;
    const float remotePlayer[]={-2600,0,0};
    Check(!Cull(body,view,remotePlayer,256,true), "near player but far camera retained");
    body.origin[0]=-200;
    Check(!Cull(body,view,remotePlayer,256,true), "near camera but far player retained");
    body.origin[0]=-1000;
    Check(Cull(body,view,remotePlayer,256,true), "far from both camera and player removed");
    view.axis[0][0]=-1; view.axis[1][1]=-1;
    Check(!Cull(body,view,player,256,true), "turning camera restores body in same frame");
    view.axis[0][0]=1; view.axis[1][1]=1;
    body.radius=std::numeric_limits<float>::quiet_NaN();
    Check(!Cull(body,view,player,256,true), "invalid bounds fail open");
    body.radius=64; view.fovY=180;
    Check(!Cull(body,view,player,256,true), "unsupported projection fails open");
    view.fovY=70; view.axis[1][0]=std::numeric_limits<float>::quiet_NaN();
    Check(!Cull(body,view,player,256,true), "invalid camera fails open");
    Check(!RenderHistoryStale(10,10,true), "duplicate scene preserves current pose");
    Check(!RenderHistoryStale(10,11,true), "consecutive frame preserves smoothing");
    Check(RenderHistoryStale(10,12,true), "reentry after skipped frame resets render smoothing");
    Check(!RenderHistoryStale(-1,12,true), "first-ever pose has no stale history");
    Check(!RenderHistoryStale(10,12,false), "disabled preserves legacy render smoothing across gaps");
    std::printf("Character render culling: %d checks passed\n",checks);
}
