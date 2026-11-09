// ASR07 bounded original-archive sky/direction audit; never calibrates stock RGB8.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/offline.h"
#include "lightrecon/sun_fit.h"
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
static void Check(Error e) { if (!e) throw std::runtime_error(e.message); }
static void Vec(std::ostream &o, Vec3 v) { o << '[' << v.x << ',' << v.y << ',' << v.z << ']'; }
int main(int argc, char **argv)
{
    try
    {
        if (argc != 4 && argc != 5) throw std::runtime_error("usage: lightrecon_solar_audit ARCHIVE RECEIVERS OUTPUT; ARCHIVE --find-outdoor [GEOMETRY_POINTS] OUTPUT");
        ReadContext read; read.maxBytes = 256ull << 20;
        std::shared_ptr<const Offline::Dataset> data;
        Check(Offline::OpenFile(argv[1], {}, data));
        Offline::Sources s; Offline::RegionSnapshot regions;
        Check(data->Load(s, regions));
        std::shared_ptr<const StaticVisibility> visibility;
        VisibilityBuildOptions visibilityOptions;
        visibilityOptions.patchToleranceWorld = data->Info().visibilityPatchToleranceWorld;
        Check(StaticVisibility::Build(s.geometry, visibilityOptions, read, visibility, s.domain));
        uint64_t work=0;
        auto query=[&](RayQuery &q,RayResult &r)
        {
            if(work>=64000000)throw std::runtime_error("Solar audit traversal budget");
            q.maxWork=std::min<uint64_t>(q.maxWork,64000000-work);
            Check(visibility->Query(q,read,r));work+=r.nodeTests+r.primitiveTests;
        };
        if(std::string(argv[2])=="--find-outdoor")
        {
            if(argc!=5)throw std::runtime_error("Outdoor selection requires training geometry points");
            size_t skySurfaces=0,knownMaterials=0;
            for(const auto &surface:s.geometry->Surfaces())
            {
                skySurfaces+=(surface.materialFacts.signals&MaterialSky)!=0;
                knownMaterials+=surface.materialFacts.available;
            }
            std::vector<Vec3> near;
            if(argc==5)
            {
                std::ifstream points(argv[3]);std::string pointMagic;size_t pointCount=0;
                points>>pointMagic>>pointCount;
                if(!points||pointMagic!="JKSOLAROUTDOOR1"||!pointCount||pointCount>4096)throw std::runtime_error("Invalid outdoor geometry points");
                near.resize(pointCount);
                for(auto &p:near){points>>p.x>>p.y>>p.z;if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z))throw std::runtime_error("Nonfinite outdoor point");}
                std::string extra;if(!points||(points>>extra))throw std::runtime_error("Truncated/extra outdoor points");
            }
            std::ofstream found(argv[argc-1]);
            uint64_t tested=0;
            if(s.grid)
            {
                const uint64_t step=std::max<uint64_t>(1,(s.grid->Report().cells+16383)/16384);
                for(uint64_t i=0;i<s.grid->Report().cells;i+=step)
                {
                    GridCell c;Check(s.grid->Cell(i,c));
                    if(!near.empty())
                    {
                        bool nearby=false;
                        for(const auto &p:near)if(std::abs(p.x-c.centreWorld.x)<=512&&std::abs(p.y-c.centreWorld.y)<=512&&std::abs(p.z-c.centreWorld.z)<=512){nearby=true;break;}
                        if(!nearby)continue;
                    }
                    RayQuery q;q.startWorld=c.centreWorld;q.endWorld=Vec3(q.startWorld.x,q.startWorld.y,q.startWorld.z+131072);
                    q.purpose=RayPurpose::Sky;RayResult r;query(q,r);++tested;
                    if(r.state==VisibilityState::Visible && r.sky==SkyReach::Reached && r.reliability==RayReliability::Geometric)
                    {
                        found<<std::setprecision(12)<<"{\"centre\":";Vec(found,c.centreWorld);
                        found<<",\"map_sha256\":\""<<Disk::Hex(s.metadata.map.sha256)<<"\",\"tested\":"<<tested
                             <<",\"sky_surfaces\":"<<skySurfaces<<",\"known_materials\":"<<knownMaterials
                             <<",\"selection\":\"First geometry-only stratified grid origin near a training receiver with actual geometric zenith sky hit; grid RGB/direction ignored\"}\n";
                        return found?0:1;
                    }
                }
            }
            throw std::runtime_error("No certified outdoor grid origin in bounded geometry scan");
        }
        if(argc!=4)throw std::runtime_error("Extra audit arguments");
        std::ifstream input(argv[2]); std::string magic, map; size_t count = 0;
        Vec3 centre; double radius = 0;
        input >> magic >> map >> centre.x >> centre.y >> centre.z >> radius >> count;
        if (!input || magic != "JKSOLARAUDIT1" || map != Disk::Hex(s.metadata.map.sha256) ||
            !count || count > 256 || !std::isfinite(radius) || radius <= 0 ||
            !std::isfinite(centre.x) || !std::isfinite(centre.y) || !std::isfinite(centre.z))
            throw std::runtime_error("Invalid audit request/map/limit");
        auto samples = std::make_shared<std::vector<Solar::Sample>>(count);
        for (size_t i = 0; i < count; ++i)
        {
            auto &v = (*samples)[i]; v.receiver.sample = i+1;
            input >> v.receiver.surface >> v.receiver.world.x >> v.receiver.world.y >> v.receiver.world.z
                  >> v.receiver.geometricNormal.x >> v.receiver.geometricNormal.y >> v.receiver.geometricNormal.z;
            v.receiver.shadingNormal = v.receiver.geometricNormal;
            const auto p=v.receiver.world,n=v.receiver.geometricNormal;
            double norm=double(n.x)*n.x+double(n.y)*n.y+double(n.z)*n.z;
            if(!std::isfinite(p.x)||!std::isfinite(p.y)||!std::isfinite(p.z)||!std::isfinite(norm)||std::abs(norm-1)>1e-4||
               !v.receiver.surface||v.receiver.surface>s.geometry->Surfaces().size())
                throw std::runtime_error("Invalid receiver geometry");
            v.receiver.mappedPlanar = true; v.eligible = true;
            // No target RGB is supplied to this nonnumeric refusal probe.
            // Default bytes have no availability/encoding attestation.
            // No placement or encoding certification from a mapped position.
            v.areaWorldSquared = 1;
        }
        std::string extra; if (!input || (input >> extra)) throw std::runtime_error("Truncated/extra audit request");
        auto hints = s.evidence ? Solar::AuthoredHints(*s.evidence) : std::vector<Solar::DirectionHint>();
        std::vector<Vec3> directions = {Vec3(0,0,1)};
        const double pi=3.14159265358979323846;
        double angle=0;
        for(double elevation : {pi/12,pi/4})
        {
            for(int j=0;j<8;++j,angle+=pi/4)
                directions.push_back(Vec3(float(std::cos(angle)*std::cos(elevation)),float(std::sin(angle)*std::cos(elevation)),float(std::sin(elevation))));
            angle+=pi/8;
        }
        for (const auto &h : hints) if (h.staticEligible && h.style == 0) directions.push_back(h.toSource);
        if (directions.size() > 48) throw std::runtime_error("Audit direction cap");
        std::ofstream out(argv[3]); out << std::setprecision(12);
        out << "{\"schema\":\"org.openjk.asr07-stock-solar-audit/1\",\"map_sha256\":\"" << map
            << "\",\"state\":\"SunUncertain\",\"profile\":\"UnknownProfile\",\"strength\":null,"
               "\"unknown_is_zero\":false,\"runtime_publication\":false,\"authored_directions\":[";
        for (size_t i=0;i<hints.size();++i)
        {
            if(i)out<<',';
            out<<"{\"id\":"<<hints[i].id<<",\"to_source\":";Vec(out,hints[i].toSource);
            out<<",\"static_eligible\":"<<(hints[i].staticEligible?"true":"false")<<",\"style\":"<<unsigned(hints[i].style)<<'}';
        }
        out << "],\"sky_queries\":[";
        for (size_t j=0;j<directions.size();++j)
        {
            uint64_t reached=0, blocked=0, unknown=0, issues=0;
            for (const auto &v : *samples)
            {
                RayQuery q; q.startWorld=v.receiver.world; q.purpose=RayPurpose::Sky;
                const auto d=directions[j];
                q.endWorld=Vec3(q.startWorld.x+d.x*131072,q.startWorld.y+d.y*131072,q.startWorld.z+d.z*131072);
                q.originSurface=v.receiver.surface; q.bias.normalKnown=true;
                q.bias.geometricNormal=v.receiver.geometricNormal;
                RayResult r; query(q,r); issues |= r.issues;
                if(r.reliability==RayReliability::Geometric && r.state==VisibilityState::Visible && r.sky==SkyReach::Reached)++reached;
                else if(r.reliability==RayReliability::Geometric && r.state==VisibilityState::Blocked)++blocked;
                else ++unknown;
            }
            if(j)out<<','; out<<"{\"to_source\":";Vec(out,directions[j]);
            out<<",\"reached\":"<<reached<<",\"blocked\":"<<blocked<<",\"unknown\":"<<unknown<<",\"issues_or\":"<<issues<<'}';
        }
        Vec3 mean, skyMean; uint64_t cells=0, skyCells=0, skyUnknown=0, omittedGridRays=0;
        std::vector<Vec3> skyPositions;
        if(s.grid) for(uint64_t i=0;i<s.grid->Report().cells;++i)
        {
            GridCell c;Check(s.grid->Cell(i,c));
            if(std::abs(c.centreWorld.x-centre.x)>radius || std::abs(c.centreWorld.y-centre.y)>radius || std::abs(c.centreWorld.z-centre.z)>radius)continue;
            auto h=Solar::GridHint(c,i+1); if(!h.staticEligible)continue;
            if(c.directed[0][0]==0 && c.directed[0][1]==0 && c.directed[0][2]==0)continue;
            ++cells;mean.x+=h.toSource.x;mean.y+=h.toSource.y;mean.z+=h.toSource.z;
            if(cells>4096){++omittedGridRays;continue;}
            RayQuery q;q.startWorld=c.centreWorld;q.purpose=RayPurpose::Sky;
            q.endWorld=Vec3(q.startWorld.x+h.toSource.x*131072,q.startWorld.y+h.toSource.y*131072,q.startWorld.z+h.toSource.z*131072);
            RayResult r;query(q,r);
            if(r.reliability==RayReliability::Geometric&&r.state==VisibilityState::Visible&&r.sky==SkyReach::Reached)
            {
                ++skyCells;skyMean.x+=h.toSource.x;skyMean.y+=h.toSource.y;skyMean.z+=h.toSource.z;
                if(skyPositions.size()<256)skyPositions.push_back(c.centreWorld);
            }
            else if(r.state!=VisibilityState::Blocked||r.reliability!=RayReliability::Geometric)++skyUnknown;
        }
        double length=std::sqrt(double(mean.x)*mean.x+double(mean.y)*mean.y+double(mean.z)*mean.z);
        out<<"],\"grid_static_roi_cells\":"<<cells<<",\"grid_unweighted_directional_coherence\":";
        if(cells)out<<length/cells;else out<<"null";
        out<<",\"grid_mean_seed\":";
        if(length>1e-6)Vec(out,Vec3(float(mean.x/length),float(mean.y/length),float(mean.z/length)));else out<<"null";
        double skyLength=std::sqrt(double(skyMean.x)*skyMean.x+double(skyMean.y)*skyMean.y+double(skyMean.z)*skyMean.z),baseline=0;
        for(size_t i=0;i<skyPositions.size();++i)for(size_t j=0;j<i;++j)
        {
            const auto a=skyPositions[i],b=skyPositions[j];
            baseline=std::max(baseline,std::sqrt(double(a.x-b.x)*(a.x-b.x)+double(a.y-b.y)*(a.y-b.y)+double(a.z-b.z)*(a.z-b.z)));
        }
        out<<",\"grid_is_solar_likelihood\":false,\"sky_visible_grid\":{\"count\":"<<skyCells
           <<",\"unknown\":"<<skyUnknown<<",\"omitted_rays\":"<<omittedGridRays
           <<",\"baseline_sample_count\":"<<skyPositions.size()<<",\"baseline_world\":"<<baseline<<",\"coherence\":";
        if(skyCells)out<<skyLength/skyCells;else out<<"null";
        out<<",\"mean_seed\":";
        if(skyLength>1e-6)Vec(out,Vec3(float(skyMean.x/skyLength),float(skyMean.y/skyLength),float(skyMean.z/skyLength)));else out<<"null";
        out<<'}';
        Solar::Request q;q.mapKey=s.metadata.map.sha256;q.lineage="asr07-stock-unknown-profile";
        q.geometry.visibility=visibility;q.geometry.geometryKey=visibility->CertificationKey();
        q.profiles={Bake::ProfileHypothesis()};q.training=VectorChunk<Solar::Sample>(samples,0,samples->size());
        Solar::FitResult fit;auto status=Solar::Fit(q,read,fit);
        out<<",\"fit_code\":"<<int(status.code)<<",\"frozen_proposals\":"<<fit.proposals.size()
           <<",\"receiver_count\":"<<count<<",\"sunlight_upper_bound\":null}\n";
        if(!out)throw std::runtime_error("Audit output write failed");
        return 0;
    }
    catch(const std::exception &e){std::cerr<<e.what()<<'\n';return 1;}
}
