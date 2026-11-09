// ASR17 private geometry-only witnesses. Never feeds a certificate or fitter.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
namespace MountDiagnosis
{
static void Polygon(std::ostream &o, const std::vector<Vec3> &v)
{
	o << '[';
	for (size_t i = 0; i < v.size(); ++i) { if (i) o << ','; Vec(o, v[i]); }
	o << ']';
}
template<class T> static void Numbers(std::ostream &o, const std::vector<T> &v)
{
	o << '[';
	for (size_t i = 0; i < v.size(); ++i) o << (i ? "," : "") << v[i];
	o << ']';
}
static std::vector<SpatialSupport> Supports(const PhysicalEmitter &e, const SourceSupport::Contract &c)
{
	std::vector<SpatialSupport> out;
	SpatialSupport s;
	if (c.kind == SourceSupport::Kind::OriginalSurface)
	{
		for (size_t i = 0; i < c.original.count; ++i)
		{
			s.vertices.assign(c.original.data[i].world, c.original.data[i].world + 3);
			out.push_back(s);
		}
	}
	else
	{
		if (e.kind == EmitterKind::Point || e.kind == EmitterKind::Spot) s.vertices = {e.position.world};
		else if (e.kind == EmitterKind::Rectangle)
		{
			V p(e.position.world), r = V(e.frame.right) * e.geometry.halfWidthWorld,
			  u = V(e.frame.up) * e.geometry.halfHeightWorld;
			s.vertices = {(p-r-u).Float(), (p+r-u).Float(), (p+r+u).Float(), (p-r+u).Float()};
		}
		else
		{
			s.kind = SpatialSupport::Kind::Cylinder;
			s.centre = e.position.world; s.axis = e.frame.right;
			s.halfLengthWorld = e.geometry.halfLengthWorld; s.radiusWorld = e.geometry.radiusWorld;
		}
		out.push_back(s);
	}
	for (auto &v : out)
	{
		v.contactPlaneKnown = c.mountKnown;
		v.contactNormal = c.outward;
		v.contactDistance = Dot(V(c.outward), V(c.anchorWorld));
	}
	return out;
}
static Bounds BoundsOf(const std::vector<SpatialSupport> &supports, Vec3 anchor)
{
	V low(anchor), high(anchor);
	for (const auto &s : supports)
		for (unsigned k = 0; k < 3; ++k)
		{
			double lo = std::numeric_limits<double>::infinity(), hi = -lo;
			if (s.kind == SpatialSupport::Kind::Cylinder)
			{
				double r = s.halfLengthWorld * std::abs(V(s.axis)[k]) + s.radiusWorld *
					std::sqrt(std::max(0., 1 - V(s.axis)[k] * V(s.axis)[k]));
				lo = V(s.centre)[k] - r; hi = V(s.centre)[k] + r;
			}
			else for (auto p : s.vertices) { lo = std::min(lo, V(p)[k]); hi = std::max(hi, V(p)[k]); }
			low[k] = std::min(low[k], lo); high[k] = std::max(high[k], hi);
		}
	Bounds b; b.known = true; b.minWorld = low.Float(); b.maxWorld = high.Float();
	float *lo[] = {&b.minWorld.x, &b.minWorld.y, &b.minWorld.z};
	float *hi[] = {&b.maxWorld.x, &b.maxWorld.y, &b.maxWorld.z};
	for (unsigned k = 0; k < 3; ++k)
	{
		if (*lo[k] > low[k]) *lo[k] = std::nextafter(*lo[k], -std::numeric_limits<float>::infinity());
		if (*hi[k] < high[k]) *hi[k] = std::nextafter(*hi[k], std::numeric_limits<float>::infinity());
	}
	return b;
}
class Writer
{
	std::ofstream rows;
	uint64_t work = 0;
  public:
	Writer(const std::string &path, const O::Sources &s, const StaticVisibility &rays)
		: rows(path + "/diagnosis.jsonl")
	{
		rows << std::setprecision(17);
		std::ofstream o(path + "/diagnosis-world.json"); o << std::setprecision(17);
		const auto &b = s.domain->Source(); const auto &r = rays.Report();
		o << "{\"schema\":\"org.openjk.mount-diagnosis-world.v1\",\"bounds\":[";
		Vec(o,b.worldBounds.minWorld); o << ','; Vec(o,b.worldBounds.maxWorld);
		o << "],\"planes\":[";
		for (size_t i=0;i<b.planes.size();++i) { if(i)o<<','; o<<'[';Vec(o,b.planes[i].normal);o<<','<<b.planes[i].distance<<']'; }
		o << "],\"nodes\":[";
		for (size_t i=0;i<b.nodes.size();++i) { const auto &n=b.nodes[i];o<<(i?",":"")<<'['<<n.plane<<','<<n.children[0]<<','<<n.children[1]<<']'; }
		o << "],\"leaves\":[";
		for (size_t i=0;i<b.leaves.size();++i) { const auto &l=b.leaves[i];o<<(i?",":"")<<'['<<l.cluster<<','<<l.area<<','<<l.firstBrush<<','<<l.brushCount<<']'; }
		o << "],\"brushes\":[";
		for (size_t i=0;i<b.brushes.size();++i) { const auto &v=b.brushes[i];o<<(i?",":"")<<'['<<v.firstSide<<','<<v.sideCount<<','<<v.world<<','<<v.solid<<','<<v.sky<<']'; }
		o << "],\"sides\":";Numbers(o,b.sides);o<<",\"leaf_brushes\":";Numbers(o,b.leafBrushes);
		o << ",\"visibility\":{\"spatial_complete\":"<<r.spatialCoverageComplete<<",\"unbounded\":"<<r.unboundedUnsupported
		  <<",\"unsupported_bounds\":"<<r.unsupportedBounds<<",\"excluded_moving\":"<<r.excludedMoving
		  <<",\"patch_triangles\":"<<r.patchTriangles<<"},\"surface_metadata\":[";
		for (size_t i=0;i<s.geometry->Surfaces().size();++i)
		{
			const auto &v=s.geometry->Surfaces()[i];if(i)o<<',';
			o<<"{\"id\":"<<v.ordinal+1<<",\"model\":"<<v.model<<",\"kind\":"<<unsigned(v.kind)
			 <<",\"material\":"<<Q(v.material)
			 <<",\"motion\":"<<unsigned(v.motion)<<",\"signals\":"<<v.materialFacts.signals
			 <<",\"cull\":"<<unsigned(v.materialFacts.cull)<<",\"vertices\":"<<v.vertices.size()<<'}';
		}
		o << "]}";
		Need(bool(o) && bool(rows), "ASR17 witness output unavailable");
	}
	void Add(Id source, const Shape::Emitter &e, const SourceSupport::Result &result,
			 const O::Sources &s, const StaticVisibility &rays, const ReadContext &read)
	{
		const auto &c=e.support; auto supports=Supports(e.geometry,c);
		if (c.kind == SourceSupport::Kind::UnsupportedAssembly)
		{
			rows<<"{\"schema\":\"org.openjk.mount-diagnosis.v1\",\"source\":"<<Q(std::to_string(source))
				<<",\"fixture\":"<<Q(std::to_string(c.fixture))<<",\"kind\":"<<unsigned(e.geometry.kind)
				<<",\"contract\":"<<unsigned(c.kind)<<",\"state\":"<<Q(PointRefine::ConstraintName(result.placement.state))
				<<",\"reason\":"<<Q(result.reason)<<",\"anchor\":[0,0,0],\"normal\":[0,0,0],\"surfaces\":[],"
				  "\"local\":{\"complete\":0,\"issues\":0,\"triangles\":[]},\"supports\":[],\"rays\":[],\"point_probes\":[],"
				  "\"diagnostic_scope\":\"unsupported contract; no fabricated support probes\",\"diagnostic_cumulative_work\":"<<work<<"}\n";
			Need(bool(rows), "ASR17 witness output unavailable");
			return;
		}
		auto bounds=BoundsOf(supports,c.anchorWorld); LocalGeometry local;
		Check(rays.ExtractBox(bounds,read,4096,1048576,local)); work+=local.work;
		rows<<"{\"schema\":\"org.openjk.mount-diagnosis.v1\",\"source\":"<<Q(std::to_string(source))
			<<",\"fixture\":"<<Q(std::to_string(c.fixture))<<",\"kind\":"<<unsigned(e.geometry.kind)
			<<",\"contract\":"<<unsigned(c.kind)<<",\"state\":"<<Q(PointRefine::ConstraintName(result.placement.state))
			<<",\"reason\":"<<Q(result.reason)<<",\"anchor\":";Vec(rows,c.anchorWorld);
		rows<<",\"normal\":";Vec(rows,c.outward);rows<<",\"position\":";Vec(rows,e.geometry.position.world);
		rows<<",\"stand_off\":"<<c.maximumStandOffWorld<<",\"surfaces\":";Numbers(rows,c.surfaces);
		rows<<",\"named_orientation\":[";
		for(size_t i=0;i<c.surfaces.size();++i)
		{
			const auto &v=s.geometry->Surfaces().at(size_t(c.surfaces[i]-1));if(i)rows<<',';
			rows<<"{\"surface\":"<<c.surfaces[i]<<",\"cull\":"<<unsigned(v.materialFacts.cull)<<",\"basis_normal\":";
			Vec(rows,v.lightmapBasis[2]);rows<<",\"vertex_normals\":[";
			Need(v.indices.size()<=192,"ASR17 named triangle witness budget");
			for(size_t j=0;j<v.indices.size();++j)
			{
				if(j)rows<<',';Vec(rows,v.vertices.at(v.indices[j]).shadingNormal);
			}
			rows<<"]}";
		}
		rows<<']';
		rows<<",\"bounds\":[";Vec(rows,bounds.minWorld);rows<<',';Vec(rows,bounds.maxWorld);
		rows<<"],\"local\":{\"complete\":"<<local.coverageComplete<<",\"issues\":"<<local.issues<<",\"triangles\":[";
		for(size_t i=0;i<local.triangles.size();++i)
		{
			const auto &t=local.triangles[i]; if(i)rows<<',';
			rows<<"{\"id\":"<<Q(std::to_string(t.primitive))<<",\"surface\":"<<t.surface<<",\"optical\":"<<unsigned(t.optical)
				<<",\"error\":"<<t.errorWorld<<",\"vertices\":";
			Polygon(rows,std::vector<Vec3>(t.geometry.verticesWorld,t.geometry.verticesWorld+3));rows<<'}';
		}
		rows<<"]},\"supports\":[";
		for(size_t i=0;i<supports.size();++i)
		{
			const auto &v=supports[i]; SpaceClass space;bool contact=false;uint64_t cost=0;
			Check(s.domain->ClassifySupport(v,space,contact,cost));work+=cost;
			if(i)rows<<',';rows<<"{\"kind\":"<<unsigned(v.kind)<<",\"vertices\":";Polygon(rows,v.vertices);
			rows<<",\"centre\":";Vec(rows,v.centre);rows<<",\"axis\":";Vec(rows,v.axis);
			rows<<",\"half_length\":"<<v.halfLengthWorld<<",\"radius\":"<<v.radiusWorld
				<<",\"contact_known\":"<<v.contactPlaneKnown<<",\"contact_distance\":"<<v.contactDistance
				<<",\"native_space\":"<<unsigned(space)<<",\"native_contact\":"<<contact<<",\"work\":"<<cost<<'}';
		}
		// Probe the first polygon interior, not the contract's arbitrary corner.
		// These query points never replace the candidate's position or support.
		V probe(e.geometry.position.world);
		if (supports[0].vertices.size()>=3)
		{
			probe=V();for(auto p:supports[0].vertices)probe=probe+V(p);
			probe=probe/double(supports[0].vertices.size());
		}
		rows<<"],\"probe_centre\":";Vec(rows,probe.Float());rows<<",\"point_probes\":[";
		for(unsigned i=0;i<4;++i)
		{
			double offset=(i<2?1:-1)*(i%2?4:.125);
			Vec3 point=(probe+V(c.outward)*offset).Float();SpatialLocation l;
			Check(s.domain->Locate(point,l,0));work+=l.work;
			if(i)rows<<',';rows<<"{\"offset\":"<<offset<<",\"point\":";Vec(rows,point);
			rows<<",\"space\":"<<unsigned(l.space)<<",\"leaf\":"<<l.leaf<<",\"cluster\":"<<l.cluster
				<<",\"boundary\":"<<l.boundary<<'}';
		}
		rows<<"],\"rays\":[";
		for(unsigned i=0;i<2;++i)
		{
			RayQuery q; V p(probe),n(c.outward);
			q.startWorld=(p+n*(i?.125:4)).Float();q.endWorld=(p+n*(i?4:-4)).Float();
			q.maxWork=1048576; RayResult r;Check(rays.Query(q,read,r)); work+=r.nodeTests+r.primitiveTests;
			if(i)rows<<',';rows<<"{\"name\":"<<Q(i?"front-segment":"cross-mount")<<",\"start\":";Vec(rows,q.startWorld);
			rows<<",\"end\":";Vec(rows,q.endWorld);rows<<",\"state\":"<<unsigned(r.state)<<",\"issues\":"<<r.issues
				<<",\"surface\":"<<r.firstHit.surface<<",\"triangle\":"<<Q(std::to_string(r.firstHit.triangle))
				<<",\"optical\":"<<unsigned(r.firstHit.obstacle)<<",\"hit_known\":"<<r.firstHit.positionKnown<<",\"hit\":";
			Vec(rows,r.firstHit.positionWorld);rows<<'}';
		}
		rows<<"],\"diagnostic_cumulative_work\":"<<work<<"}\n";
		Need(work<=16000000 && bool(rows), "ASR17 diagnostic work/output limit");
	}
};
} // namespace MountDiagnosis
