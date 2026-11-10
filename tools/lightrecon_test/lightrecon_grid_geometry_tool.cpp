// ASR13 read-only original-grid/geometry bridge. No solver or asset writes.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_grid_evidence.h"
#include "lightrecon/offline.h"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace OriginalGridEvidence;
namespace O = Rend2LightRecon::Core::Offline;
namespace
{
void Check(Error e) { if (!e) throw std::runtime_error(e.message); }
void V(Vec3 p) { std::cout << '[' << p.x << ',' << p.y << ',' << p.z << ']'; }
double Axis(Vec3 p, unsigned a) { return a == 0 ? p.x : a == 1 ? p.y : p.z; }
Vec3 ReadPoint(std::istream &s)
{
 double a, b, c;
 if (!(s >> a >> b >> c) || !std::isfinite(a+b+c) || std::max({std::abs(a),std::abs(b),std::abs(c)}) > 1e7)
  throw std::runtime_error("Invalid world point");
 return Vec3(float(a),float(b),float(c));
}
Bounds ReadBox(std::istream &s)
{
 Bounds b; b.known = true; b.minWorld = ReadPoint(s); b.maxWorld = ReadPoint(s);
 for (unsigned a = 0; a < 3; ++a) if (Axis(b.minWorld,a) >= Axis(b.maxWorld,a)) throw std::runtime_error("Invalid box");
 return b;
}
bool Inside(const Bounds &b, Vec3 p)
{
 for (unsigned a = 0; a < 3; ++a) if (Axis(p,a) < Axis(b.minWorld,a) || Axis(p,a) > Axis(b.maxWorld,a)) return false;
 return true;
}
bool Dynamic(const O::Sources &s, Vec3 a, Vec3 b)
{
 for (const auto &d : s.dynamic)
 {
  if (!d.sweptExtentKnown || !d.bounds.known) return true;
  bool hit = true;
  for (unsigned k = 0; k < 3; ++k)
   if (std::max(Axis(a,k),Axis(b,k)) < Axis(d.bounds.minWorld,k) ||
       std::min(Axis(a,k),Axis(b,k)) > Axis(d.bounds.maxWorld,k)) hit = false;
  if (hit) return true;
 }
 return false;
}
Bounds Span(Vec3 a, Vec3 b)
{
 Bounds r; r.known = true;
 r.minWorld = Vec3(std::min(a.x,b.x)-.01f,std::min(a.y,b.y)-.01f,std::min(a.z,b.z)-.01f);
 r.maxWorld = Vec3(std::max(a.x,b.x)+.01f,std::max(a.y,b.y)+.01f,std::max(a.z,b.z)+.01f);
 return r;
}
}
int main(int argc, char **argv)
{
 try
 {
  if (argc != 2) throw std::runtime_error("usage: lightrecon_grid_geometry_tool original.jkobs (line protocol on stdin)");
  std::shared_ptr<const O::Dataset> data; Check(O::OpenFile(argv[1],O::Options(),data));
  O::Sources s; O::RegionSnapshot snapshot; Check(data->Load(s,snapshot));
  if (!s.grid) throw std::runtime_error("Original BSP lightgrid required; no generated-probe fallback");
  ReadContext read; read.maxBytes = 256ull << 20;
  std::shared_ptr<const StaticVisibility> scene;
  if (s.domain && s.domain->SourceIdentity()->sha256 != s.grid->Map().sha256) throw std::runtime_error("Domain/grid identity mismatch");
  if (s.geometry)
  {
   if (s.geometry->Map().sha256 != s.grid->Map().sha256) throw std::runtime_error("Geometry/grid identity mismatch");
   VisibilityBuildOptions o; o.patchToleranceWorld = s.metadata.visibilityPatchToleranceWorld;
   Check(StaticVisibility::Build(s.geometry,o,read,scene,s.domain));
  }
  const auto &g = *s.grid; const auto &layout = g.Layout();
  std::cout << std::setprecision(17) << std::boolalpha;
  std::cout << "{\"schema\":\"org.openjk.grid-geometry-bridge.v1\",\"map_sha256\":\"" << Disk::Hex(g.Map().sha256)
   << "\",\"records_sha256\":\"" << Disk::Hex(Disk::Hash(g.Records().data(),g.Records().size()))
   << "\",\"indices_sha256\":\"" << Disk::Hex(Disk::Hash(g.Indices().data(),g.Indices().size()))
   << "\",\"geometry_key\":";
  if (scene) std::cout << '"' << Disk::Hex(scene->CertificationKey()) << '"'; else std::cout << "null";
  std::cout << ",\"origin\":"; V(layout.originWorld); std::cout << ",\"spacing\":"; V(layout.spacingWorld);
  std::cout << ",\"shape\":[" << layout.bounds[0] << ',' << layout.bounds[1] << ',' << layout.bounds[2]
   << "],\"cells\":" << g.Report().cells << ",\"compiler_known\":" << s.metadata.compilerKnown
   << ",\"synthetic\":" << s.metadata.synthetic << ",\"dynamic_models\":" << s.dynamic.size() << "}" << std::endl;
  std::string line;
  while (std::getline(std::cin,line))
  {
   std::istringstream command(line); std::string op; command >> op;
   if (op == "quit") break;
   if (op == "cells")
   {
    const Bounds box = ReadBox(command);
    // Bound work before extraction. No silent truncation of an ROI.
    std::vector<uint64_t> selected; GridCell c;
    for (uint64_t i = 0; i < g.Report().cells; ++i)
    {
     Check(g.Cell(i,c)); if (Inside(box,c.centreWorld)) selected.push_back(i);
     if (selected.size() > 4096) throw std::runtime_error("ROI exceeds 4096 original cells");
    }
    Input in; in.grid = s.grid; in.domain = s.domain; in.dynamic = s.dynamic; in.profile = s.metadata.decoding;
    Options options; // Encoded relative RGB; no irradiance/attenuation calibration.
    std::cout << "{\"rows\":["; bool comma = false;
    for (auto ordinal : selected)
    {
     Observation r; Check(Extract(in,options,ordinal,read,r));
     if (comma) std::cout << ',';
     comma = true;
     std::cout << "{\"ordinal\":" << ordinal << ",\"xyz\":[" << r.raw.coordinates[0] << ',' << r.raw.coordinates[1] << ',' << r.raw.coordinates[2]
      << "],\"position\":"; V(r.raw.centreWorld);
     std::cout << ",\"space\":\"" << SpaceName(r.location.space) << "\",\"boundary\":" << r.location.boundary
      << ",\"dynamic_possible\":" << r.dynamicPossible << ",\"present\":" << (r.raw.presence == GridPresence::Present)
      << ",\"issues\":" << r.raw.issues
      << ",\"correlation_key\":";
     if (r.raw.presence == GridPresence::Present)
      std::cout << '"' << Disk::Hex(Disk::Hash(g.Records().data()+r.raw.sourceRecord*kGridRecordBytes,kGridRecordBytes)) << '"';
     else std::cout << "null";
     std::cout << ",\"consumer256\":"; V(r.direction.consumer256);
     std::cout << ",\"writer255\":"; V(r.direction.writer255);
     std::cout << ",\"writer_possible\":" << r.direction.writer255CodePossible << ",\"styles\":[";
     bool sc = false;
     for (const auto &style : r.styles) if (style.active)
     {
      if (sc) std::cout << ',';
      sc = true;
      std::cout << "{\"id\":" << unsigned(style.id) << ",\"directed\":[";
      for (unsigned k = 0; k < 3; ++k) std::cout << (k ? "," : "") << unsigned(r.raw.directed[style.slot][k]);
      std::cout << "],\"weak\":" << style.weakDirected << '}';
     }
     std::cout << "]}";
    }
    std::cout << "]}" << std::endl;
   }
   else if (op == "point" || op == "box")
   {
    SpatialLocation p; SpaceClass space = SpaceClass::Unknown; uint64_t work = 0;
    if (op == "point") { Vec3 v = ReadPoint(command); if (s.domain) Check(s.domain->Locate(v,p)); space = p.space; }
    else { Bounds b = ReadBox(command); if (s.domain) Check(s.domain->ClassifyBox(b,space,work)); }
    std::cout << "{\"space\":\"" << SpaceName(space) << "\",\"boundary\":" << p.boundary << "}" << std::endl;
   }
   else if (op == "ray" || op == "segment")
   {
    Vec3 a = ReadPoint(command), b = ReadPoint(command); RayResult r;
    SpaceClass span = SpaceClass::Unknown; uint64_t work = 0;
    if (s.domain)
    {
     if (op == "segment")
     {
      // ASR24 adapter: certify the actual convex segment with the existing BSP
      // support provider. The older ray operation keeps its enclosing-box rule.
      SpatialSupport support; support.vertices = {a,b}; bool contact = false;
      Check(s.domain->ClassifySupport(support,span,contact,work));
     }
     else Check(s.domain->ClassifyBox(Span(a,b),span,work));
    }
    if (scene)
    {
     RayQuery q; q.startWorld = a; q.endWorld = b; q.maxWork = 65536;
     q.policy.facing = FacingPolicy::TwoSided;
     Check(scene->Query(q,read,r));
    }
    const bool clear = span == SpaceClass::Free && r.state == VisibilityState::Visible && r.reliability == RayReliability::Geometric && !r.issues;
    std::cout << "{\"static_state\":\"" << (clear ? "clear" : r.state == VisibilityState::Blocked ? "blocked" : "unknown")
     << "\",\"span_space\":\"" << SpaceName(span) << "\",\"issues\":" << r.issues
     << ",\"dynamic_possible\":" << Dynamic(s,a,b)
     << ",\"opaque_hit_known\":" << (r.state == VisibilityState::Blocked &&
         r.reliability == RayReliability::Geometric && r.terminalHit.positionKnown &&
         r.terminalHit.obstacle == ObstacleKind::Opaque)
     << ",\"hit_surface\":" << r.terminalHit.surface << ",\"hit_position\":";
    if (r.terminalHit.positionKnown) V(r.terminalHit.positionWorld); else std::cout << "null";
    std::cout << "}" << std::endl;
   }
   else if (op == "mesh")
   {
    Bounds b = ReadBox(command); LocalGeometry mesh;
    if (scene) Check(scene->ExtractBox(b,read,20000,2000000,mesh));
    std::cout << "{\"coverage_complete\":" << mesh.coverageComplete << ",\"issues\":" << mesh.issues << ",\"triangles\":[";
    bool comma = false;
    for (const auto &t : mesh.triangles)
    {
     if (comma) std::cout << ',';
     comma = true;
     std::cout << '['; V(t.geometry.verticesWorld[0]); std::cout << ','; V(t.geometry.verticesWorld[1]); std::cout << ','; V(t.geometry.verticesWorld[2]); std::cout << ']';
    }
    std::cout << "]}" << std::endl;
   }
   else throw std::runtime_error("Unknown bridge command");
  }
  return 0;
 }
 catch (const std::exception &e) { std::cerr << "grid geometry: " << e.what() << '\n'; return 1; }
}
