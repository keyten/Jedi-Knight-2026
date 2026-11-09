// Offline experiment adapter. Production geometry APIs are unchanged.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/offline.h"
#include <fstream>
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
void Check(Error e) { if (!e) throw std::runtime_error(e.message); }
struct Receiver { Vec3 p; Id surface; };
int main(int argc, char** argv)
{
    try {
        if (argc != 3) throw std::runtime_error("usage: idf_geometry sanitized.jkobs receivers.txt");
        std::shared_ptr<const Offline::Dataset> data;
        Check(Offline::OpenFile(argv[1], Offline::Options(), data));
        Offline::Sources s; Offline::RegionSnapshot graph; Check(data->Load(s, graph));
        if (s.images || s.grid || !s.geometry || s.metadata.hints != Offline::HintPolicy::BlindBenchmark)
            throw std::runtime_error("Sanitized geometry only");
        for (const auto& surface : s.geometry->Surfaces())
            for (const auto& v : surface.vertices)
                if (v.colourPresent) throw std::runtime_error("Hidden vertex RGB forbidden");
        std::ifstream f(argv[2]); size_t count; f >> count;
        if (!f || count > 64) throw std::runtime_error("Receiver budget");
        std::vector<Receiver> receivers(count);
        for (auto& r : receivers) { f >> r.surface >> r.p.x >> r.p.y >> r.p.z; ++r.surface; }
        if (!f) throw std::runtime_error("Receiver request");
        ReadContext read; read.maxBytes = 512ull*1024*1024;
        VisibilityBuildOptions options; options.patchToleranceWorld = s.metadata.visibilityPatchToleranceWorld;
        std::shared_ptr<const StaticVisibility> visibility;
        Check(StaticVisibility::Build(s.geometry, options, read, visibility, s.domain));
        float lo[3]={1e30f,1e30f,1e30f}, hi[3]={-1e30f,-1e30f,-1e30f};
        for (const auto& surface : s.geometry->Surfaces())
            for (const auto& v : surface.vertices) {
                float xyz[3]={v.position.x,v.position.y,v.position.z};
                for(int i=0;i<3;++i) {lo[i]=std::min(lo[i],xyz[i]);hi[i]=std::max(hi[i],xyz[i]);}
            }
        std::cout << "{\"ready\":true,\"bounds\":["<<lo[0]<<','<<lo[1]<<','<<lo[2]<<','<<hi[0]<<','<<hi[1]<<','<<hi[2]<<"]}\n" << std::flush;
        Vec3 p;
        while (std::cin >> p.x >> p.y >> p.z) {
            PointDomain domain = PointDomain::Unknown;
            if (s.domain) Check(s.domain->Classify(p, 1e-5, domain));
            Bounds box; box.known = true;
            box.minWorld = Vec3(p.x-.5f,p.y-.5f,p.z-.5f);
            box.maxWorld = Vec3(p.x+.5f,p.y+.5f,p.z+.5f);
            GeometryBoxResult overlap; Check(visibility->OverlapBox(box, read, overlap));
            bool free = domain == PointDomain::Free && overlap.coverageComplete && !overlap.intersects;
            uint64_t work = overlap.work;
            std::cout << "{\"domain\":" << int(domain) << ",\"free\":" << (free?"true":"false")
                      << ",\"overlap\":" << (overlap.intersects?"true":"false")
                      << ",\"coverage_complete\":" << (overlap.coverageComplete?"true":"false") << ",\"states\":[";
            for (size_t i=0;i<receivers.size();++i) {
                RayResult result;
                if (free) {
                    RayQuery q; q.startWorld=p; q.endWorld=receivers[i].p;
                    q.originKnownFree=true; q.targetSurface=receivers[i].surface; q.targetToleranceWorld=.05;
                    Check(visibility->Query(q,read,result));
                    work += result.nodeTests+result.primitiveTests;
                }
                if (i) std::cout << ',';
                std::cout << int(result.state);
            }
            std::cout << "],\"rays\":" << (free?count:0) << ",\"work\":" << work << "}\n" << std::flush;
        }
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
}
