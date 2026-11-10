// ASR23 geometry-only forward adapter. No RGB targets, answers, fitting or assets.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/offline.h"
#include "lightrecon/shape_selection.h"
#include "lightrecon/bake_math.h"
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace Rend2LightRecon;
using namespace Core;
using namespace Bake::Detail;
void Must(Error e) { if (!e) throw std::runtime_error(e.message); }
void Need(bool b, const char *s) { if (!b) throw std::runtime_error(s); }
void ReadVec(std::istream &in, Vec3 &p) { in >> p.x >> p.y >> p.z; Need(bool(in) && Finite(p), "Finite vector required"); }
void JsonVec(Vec3 p) { std::cout << '[' << p.x << ',' << p.y << ',' << p.z << ']'; }
template<class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> p) { return VectorChunk<T>(p, 0, p->size()); }
Frame MakeFrame(Vec3 forward) {
    V f = Unit(V(forward)), ref = std::abs(f.z) < .9 ? V(0,0,1) : V(0,1,0);
    Frame out; out.known = true; out.forward = f.Float();
    out.right = Unit(Cross(ref, f)).Float(); out.up = Cross(f, V(out.right)).Float(); return out;
}
int main(int argc, char **argv) {
 try {
    Need(argc == 3, "usage: lightrecon_blind_basis_tool geometry.jkobs geometry.request");
    ReadContext read; read.maxBytes = 128ull << 20;
    std::shared_ptr<const Offline::Dataset> data; Must(Offline::OpenFile(argv[1], Offline::Options(), data));
    Offline::Sources source; Offline::RegionSnapshot graph; Must(data->Load(source, graph));
    Need(source.geometry && source.domain, "Captured geometry/domain required");
    // The caller must supply the independently sanitized public archive.
    std::shared_ptr<const StaticVisibility> visibility;
    Must(StaticVisibility::Build(source.geometry, VisibilityBuildOptions(), read, visibility, source.domain));
    PointRefine::SceneInput scene; scene.map = source.metadata.map; scene.domain = source.domain;
    scene.geometry = visibility; scene.originalSurfaces = source.geometry; scene.dynamic = source.dynamic;
    scene.domainKey = Disk::Hash("ASR23 original domain", 21);
    std::shared_ptr<const PointRefine::PlacementProvider> placement;
    Must(PointRefine::MakePlacement(scene, read, placement));
    Bake::EvaluationContext context; context.visibility = visibility; context.geometryKey = visibility->CertificationKey();
    Shape::Model model; model.law = Shape::Law::DiffuseIntegral; model.output = Shape::Output::RelativeIncident;
    model.allowAssumed = true; model.units.worldUnitsPerMeter = 1;
    model.provenance = "ASR23 conditional modern relative transfer; fixed forward RGB8 gain; no SI calibration";
    std::ifstream in(argv[2]); std::string magic; size_t count = 0; in >> magic >> count;
    if (magic == "JKPOINTS23") {
        Need(count <= 4096, "Original-grid point classification limit");
        std::cout << "{\"locations\":[";
        for (size_t i = 0; i < count; ++i) {
            Vec3 p; ReadVec(in, p); SpatialLocation location; Must(source.domain->Locate(p, location));
            std::cout << (i ? "," : "") << "{\"space\":" << int(location.space)
                      << ",\"boundary\":" << (location.boundary ? "true" : "false") << '}';
        }
        std::string extra; Need(!(in >> extra), "Geometry queries cannot contain RGB");
        std::cout << "]}\n"; return 0;
    }
    Need(magic == "JKBASIS23" && count <= 128, "Bounded geometry protocol required");
    struct Candidate { std::string requested; Shape::Emitter e; bool valid = true, solar = false; std::string reason; };
    std::vector<Candidate> candidates;
    for (size_t j = 0; j < count; ++j) {
        Candidate c; Id id = 0; in >> c.requested >> id;
        c.e.geometry.source.id = id; c.e.geometry.source.state = HypothesisState::Candidate;
        c.e.geometry.position.known = true; c.e.rgbAmplitude = Vec3(1,1,1);
        if (c.requested == "surface" || c.requested == "surface_point") {
            uint32_t ordinal = 0; in >> ordinal;
            ShapeSelection::Request r; r.scene = scene; r.fixture = id; r.emittingSurfaces = {ordinal};
            // Full-face emission is public authored shader evidence in these owned fixtures.
            r.fullSurfaceEmissionAttested = true; r.emissionProvenance = "Owned original full-face emissive shader";
            ShapeSelection::Result ranked; Must(ShapeSelection::Rank(r, read, ranked));
            c.valid = false;
            for (const auto &v : ranked.ranked)
                if (v.kind == EmitterKind::Rectangle &&
                    (v.status == ShapeSelection::Status::Supported || v.status == ShapeSelection::Status::Approximation)) {
                    c.e = v.emitter; c.valid = true; break;
                }
            if (!c.valid) c.reason = "No supported original rectangle";
            if (c.valid && c.requested == "surface_point") {
                c.e.geometry.position.world = (V(c.e.geometry.position.world) + V(c.e.geometry.frame.forward)*2).Float();
                c.e.geometry.kind = EmitterKind::Point; c.e.geometry.angular.kind = AngularKind::Isotropic;
                c.e.geometry.geometry = EmitterGeometry(); c.e.support = SourceSupport::Contract();
                c.e.support.kind = SourceSupport::Kind::FreePoint;
            }
        } else {
            ReadVec(in, c.e.geometry.position.world);
            c.e.geometry.kind = EmitterKind::Point; c.e.geometry.angular.kind = AngularKind::Isotropic;
            c.e.support.kind = SourceSupport::Kind::FreePoint;
            if (c.requested == "spot" || c.requested == "sun") {
                Vec3 axis; ReadVec(in, axis); c.e.geometry.frame = MakeFrame(axis);
                if (c.requested == "spot") {
                    c.e.geometry.kind = EmitterKind::Spot; c.e.geometry.angular.kind = AngularKind::SmoothCone;
                    in >> c.e.geometry.angular.cosInner >> c.e.geometry.angular.cosOuter;
                    c.e.support.kind = SourceSupport::Kind::FreeSpot;
                } else c.solar = true;
            } else Need(c.requested == "point", "Unsupported proposal type");
        }
        c.e.support.fixture = id; c.e.support.map = scene.map.sha256;
        c.e.support.geometry = context.geometryKey; c.e.support.provenance = "ASR23 public evidence proposal";
        candidates.push_back(c);
    }
    size_t n = 0; in >> n; Need(n > 0 && n <= 1024, "Bounded nonempty receiver geometry required");
    auto rows = std::make_shared<std::vector<Bake::Receiver>>(n);
    for (size_t i = 0; i < n; ++i) {
        auto &r = (*rows)[i]; uint32_t ordinal = 0; in >> ordinal;
        r.sample = i+1; r.surface = ordinal+1; r.material = 1;
        ReadVec(in, r.world); ReadVec(in, r.geometricNormal); r.shadingNormal = r.geometricNormal; r.mappedPlanar = true;
    }
    std::string extra; Need(!(in >> extra), "RGB/extra fields forbidden in geometry request");
    std::cout << std::setprecision(15) << "{\"candidates\":[";
    uint64_t rays = 0, work = 0;
    for (size_t j = 0; j < count; ++j) {
        auto &c = candidates[j]; SourceSupport::Result support;
        if (c.valid && !c.solar) {
            Must(SourceSupport::Check(scene, *placement, c.e.geometry, c.e.geometry, c.e.support, read, support));
            rays += support.rays; work += support.work;
            c.valid = PointRefine::Admissible(support.placement); c.reason = support.reason;
        }
        std::vector<Shape::Prediction> predictions;
        if (c.valid && !c.solar) {
            Shape::Sampling sampling; sampling.areaDivisions = 4; sampling.compareRefined = true;
            sampling.maxRays = 2000000; sampling.maxWork = 32000000;
            std::shared_ptr<const Shape::Block> block;
            auto e = Shape::Prepare(model, {c.e}, Lease(rows), context, sampling, read, block);
            if (!e) { c.valid = false; c.reason = e.message; }
            else {
                Shape::Work meter; Must(block->Evaluate({Vec3(1000000,1000000,1000000)}, read, predictions, meter));
                rays += block->PreparationWork().rays + meter.rays;
                work += block->PreparationWork().traversal + meter.traversal;
            }
        }
        std::cout << (j ? "," : "") << "{\"admitted\":" << (c.valid ? "true" : "false")
                  << ",\"type\":\"" << (c.solar ? "sun" : ShapeSelection::KindName(c.e.geometry.kind)) << "\",\"position\":";
        JsonVec(c.e.geometry.position.world);
        std::cout << ",\"placement\":\"" << (c.solar ? "SkyBoundary" : PointRefine::ConstraintName(support.placement.state))
                  << "\",\"column\":[";
        for (size_t i = 0; i < n; ++i) {
            if (i) std::cout << ',';
            if (!c.valid) { std::cout << "null"; continue; }
            if (!c.solar) { if (predictions[i].available) std::cout << predictions[i].value.x; else std::cout << "null"; }
            else {
                const auto &r = (*rows)[i]; V direction = V(c.e.geometry.frame.forward)*-1;
                const double cosine = std::max(0., Dot(V(r.geometricNormal), direction));
                // As in Shape::Prepare, a known one-sided zero cosine is zero
                // independent of visibility. Do not trace a coplanar dead term.
                if (cosine <= 1e-12) { std::cout << 0; continue; }
                RayQuery q; q.startWorld = r.world; q.endWorld = (V(r.world)+direction*65536).Float();
                q.purpose = RayPurpose::Sky; q.originSurface = r.surface;
                q.bias.normalKnown = true; q.bias.geometricNormal = r.geometricNormal;
                q.policy.facing = FacingPolicy::TwoSided;
                RayResult hit; Must(visibility->Query(q, read, hit)); ++rays; work += hit.nodeTests + hit.primitiveTests;
                if (hit.state == VisibilityState::Unknown) std::cout << "null";
                else std::cout << (hit.sky == SkyReach::Reached ? cosine*100 : 0);
            }
        }
        std::cout << "]}";
    }
    std::cout << "],\"rays\":" << rays << ",\"traversal_work\":" << work << "}\n";
    return 0;
 } catch (const std::exception &e) { std::cerr << e.what() << '\n'; return 2; }
}
