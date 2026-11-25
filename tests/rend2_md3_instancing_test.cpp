#include "tr_md3_instance_policy.h"
#include <cstdlib>
#include <iostream>
#include <limits>
#include <set>
static void Check(bool condition) { if (!condition) std::abort(); }
int main() {
    using namespace Rend2Md3;
    float axis[3][3] = {{0,1,0},{-1,0,0},{0,0,1}};
    float scale[3] = {0,0,0};
    Check(UniformPositiveTransform(axis, scale));
    scale[0]=scale[1]=scale[2]=2; Check(UniformPositiveTransform(axis, scale));
    scale[1]=3; Check(!UniformPositiveTransform(axis, scale));
    scale[1]=std::numeric_limits<float>::quiet_NaN(); Check(!UniformPositiveTransform(axis, scale));
    scale[0]=scale[1]=scale[2]=-1; Check(!UniformPositiveTransform(axis, scale));
    scale[0]=scale[1]=scale[2]=1;
    axis[0][1]=-1; Check(!UniformPositiveTransform(axis, scale)); // mirrored
    axis[0][1]=1; axis[2][1]=0.1f; Check(!UniformPositiveTransform(axis, scale)); // shear
    axis[2][1]=0; axis[2][2]=0; Check(!UniformPositiveTransform(axis, scale)); // singular
    Eligibility valid={true,true,true,true,true,true,true,true};
    Check(Eligible(valid));
    for (int i=0;i<8;++i) {
        Eligibility bad=valid;
        switch(i) {
        case 0: bad.rigidSingleFrame=false;break;
        case 1: bad.ordinaryRenderFx=false;break;
        case 2: bad.uniformTransform=false;break;
        case 3: bad.opaqueSingleStage=false;break;
        case 4: bad.stableGeometry=false;break;
        case 5: bad.ordinaryMaterial=false;break;
        case 6: bad.supportedView=false;break;
        case 7: bad.supportedProgram=false;break;
        }
        Check(!Eligible(bad));
    }
    // Same geometry/material can have different transforms and lighting. A hash
    // collision with another material must not merge it, even with room left.
    struct Item { int mesh, material; float translation, lighting; };
    std::vector<Item> items;
    Planner planner;
    for(int i=0;i<70;++i) {
        items.push_back({i==17?2:1,i==31?2:1,float(i)*2,float(i)*0.1f});
        planner.Add(i,42,[&](int a,int b){return items[a].mesh==items[b].mesh && items[a].material==items[b].material;});
    }
    Check(planner.groups.size()==6);
    std::set<int> seen; int saved=0;
    for(const auto& group:planner.groups) {
        Check(!group.members.empty() && group.members.size()<=32);
        for(int i:group.members) {
            Check(seen.insert(i).second);
            Check(items[i].mesh==items[group.members[0]].mesh);
            Check(items[i].material==items[group.members[0]].material);
        }
        saved+=int(group.members.size())-1;
    }
    Check(seen.size()==70 && saved==64);
    Planner gap;
    gap.Add(0,1,[](int,int){return true;});
    gap.Add(2,1,[](int,int){return true;});
    Check(gap.groups.size()==2); // an ineligible intervening draw is a barrier

    Check(HasUploadSpace(0,16384,13312,256));
    Check(!HasUploadSpace(4096,16384,13312,256));
    Check(!HasUploadSpace(SIZE_MAX-10,SIZE_MAX,13312,256));
    Check(!HasUploadSpace(0,SIZE_MAX,SIZE_MAX,256));
    Check(!HasUploadSpace(0,16384,1,255));
    std::cout<<"MD3 eligibility, transform/lighting independence, hash collision, split and overflow tests passed\n";
}
