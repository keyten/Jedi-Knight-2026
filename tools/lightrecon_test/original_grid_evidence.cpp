// SPDX-License-Identifier: GPL-2.0-or-later
#include "original_grid_evidence.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace OriginalGridEvidence
{
namespace
{
const double pi = 3.14159265358979323846;
double Axis(Vec3 p, unsigned a) { return a == 0 ? p.x : a == 1 ? p.y : p.z; }
bool Free(const SpatialLocation &p) { return p.space == SpaceClass::Free && !p.boundary; }
bool Overlap(const Bounds &b, Vec3 a, Vec3 c)
{
	if (!b.known) return true;
	for (unsigned k = 0; k < 3; ++k)
		if (std::max(Axis(a,k),Axis(c,k)) < Axis(b.minWorld,k) ||
			std::min(Axis(a,k),Axis(c,k)) > Axis(b.maxWorld,k)) return false;
	return true;
}
bool Dynamic(const Input &in, Vec3 a, Vec3 b)
{
	for (const auto &d : in.dynamic)
	{
		// Original model bounds cannot bound unknown travel or an unknown bake pose.
		if (!d.sweptExtentKnown || Overlap(d.bounds,a,b)) return true;
	}
	return false;
}
Error Locate(const Input &in, Vec3 p, SpatialLocation &out)
{
	out = {};
	return in.domain ? in.domain->Locate(p,out) : Error();
}
bool Active(const GridCell &c, unsigned s)
{
	if (c.presence != GridPresence::Present) return false;
	for (unsigned j = 0; j <= s; ++j) if (c.styles[j] == 255) return false;
	return true;
}
int Slot(const GridCell &c, uint8_t id)
{
	int found = -1;
	for (unsigned s = 0; s < 4 && Active(c,s); ++s)
		if (c.styles[s] == id)
		{
			if (found >= 0) return -1; // ambiguous duplicate style ID
			found = int(s);
		}
	return found;
}
Colour DecodeColour(const GridCell &cell, unsigned slot, Decode::Component kind,
	const Options &o, const Decode::Decoder &decoder)
{
	Colour result;
	if (!Active(cell,slot)) return result;
	Decode::Observation decoded;
	if (!DecodeGridLdr(decoder,cell,slot,kind,decoded)) return result;
	result.decoderFlags = decoded.flags;
	const auto &value = o.colourSpace == ColourSpace::EncodedRgb8 ? decoded.encoded : decoded.source;
	if (value.state != Decode::State::Available) return result;
	const auto *bytes = kind == Decode::Component::GridAmbient ? cell.ambient[slot] : cell.directed[slot];
	// +/- one code includes unknown rounding. Merge is a named hypothesis, never inferred.
	// Decode the widened endpoints through the SAME monotone profile, with censoring retained.
	const int radius = o.compression == Compression::Q3map2Merge4 ? 5 : 1;
	Decode::Raw lo = decoded.raw, hi = decoded.raw;
	for (unsigned c = 0; c < 3; ++c)
	{
		lo.bytes[c] = uint8_t(std::max(0,int(bytes[c])-radius));
		hi.bytes[c] = uint8_t(std::min(255,int(bytes[c])+radius));
	}
	Decode::Observation dl, dh;
	if (!decoder.Run(lo,dl) || !decoder.Run(hi,dh)) return result;
	const auto &lv = o.colourSpace == ColourSpace::EncodedRgb8 ? dl.encoded : dl.source;
	const auto &hv = o.colourSpace == ColourSpace::EncodedRgb8 ? dh.encoded : dh.source;
	result.intensity.available = true;
	result.intensity.bounded = true;
	for (unsigned c = 0; c < 3; ++c)
	{
		auto &v = result.rgb[c];
		v.available = true;
		v.value = Axis(value.rgb,c);
		v.lower = Axis(lv.rgb,c);
		v.upper = Axis(hv.rgb,c);
		v.bounded = lv.state == Decode::State::Available && hv.state == Decode::State::Available &&
			hi.bytes[c] < 255; // saturation: no finite upper bound on preclip signal
		result.intensity.value += v.value/3;
		result.intensity.lower += v.lower/3;
		result.intensity.upper += v.upper/3;
		result.intensity.bounded = result.intensity.bounded && v.bounded;
	}
	return result;
}
Interval Difference(const Interval &a, const Interval &b, double distance)
{
	Interval out;
	if (!a.available || !b.available) return out;
	out.available = true;
	out.value = (b.value-a.value)/distance;
	out.bounded = a.bounded && b.bounded;
	if (out.bounded)
	{
		out.lower = (b.lower-a.upper)/distance;
		out.upper = (b.upper-a.lower)/distance;
	}
	return out;
}
Colour HdrColour(const GridCell &cell, Decode::Component kind, const Decode::Decoder &decoder)
{
	Colour out; Decode::Observation decoded;
	if (!cell.hdrPresent) return out;
	if (!DecodeGridHdr(decoder,cell,kind,decoded)) return out;
	out.decoderFlags = decoded.flags;
	if (decoded.source.state != Decode::State::Available) return out;
	out.intensity.available = true;
	for (unsigned c = 0; c < 3; ++c)
	{
		out.rgb[c].available = true; out.rgb[c].value = Axis(decoded.source.rgb,c);
		out.intensity.value += out.rgb[c].value/3;
	}
	// Headerless float precision/history provides no conservative measurement bound.
	return out;
}
} // namespace
Direction InterpretDirection(uint8_t polar, uint8_t azimuth, Compression compression)
{
	Direction d;
	d.consumer256 = DecodeGridDirection(polar,azimuth).toSource;
	// NormalToLatLong truncates a SIGNED atan2, then masks to a byte.
	const int signedAzimuth = azimuth <= 127 ? int(azimuth) : int(azimuth)-256;
	const double p = polar*2*pi/255, a = signedAzimuth*2*pi/255;
	d.writer255 = Vec3(float(std::cos(a)*std::sin(p)),float(std::sin(a)*std::sin(p)),float(std::cos(p)));
	if (polar == 128 && azimuth == 0) d.writer255 = Vec3(0,0,-1); // writer's south-pole special case
	d.consumerEncodingBoundRadians = DecodeGridDirection(polar,azimuth).errorBoundRadians;
	d.writerEncodingBoundRadians = 4*pi/255; // whole truncation bin in each angle
	d.writer255CodePossible = (polar <= 127 && azimuth != 128) || (polar == 128 && azimuth == 0);
	d.compressionBoundKnown = compression == Compression::Q3map2Merge4;
	// AddLightGridLumps accepts abs(byte difference)>=251: up to FIVE circular bins.
	d.mergeBoundRadians = d.compressionBoundKnown ? 10*2*pi/255 : 0;
	return d;
}
Error Extract(const Input &in, const Options &o, uint64_t ordinal, const ReadContext &read, Observation &out)
{
	out = {};
	if (!in.grid || !o.maxRayWork || !std::isfinite(o.weakDirectedCode) || o.weakDirectedCode < 0 ||
		(o.compression != Compression::Unknown && o.compression != Compression::Q3map2Merge4) ||
		(o.colourSpace != ColourSpace::EncodedRgb8 && o.colourSpace != ColourSpace::ProfileSourceRelative))
		return Failure(Code::InvalidInput,"Invalid original grid diagnostic input/options");
	if (in.domain && in.domain->SourceIdentity() &&
		in.domain->SourceIdentity()->sha256 != in.grid->Map().sha256)
		return Failure(Code::InvalidInput,"Grid/domain source identity mismatch");
	if (read.cancellation && read.cancellation->IsCancelled()) return Failure(Code::Cancelled,"Grid diagnostic cancelled");
	Decode::Decoder decoder(in.profile);
	if (!decoder.Validation()) return decoder.Validation();
	auto e = in.grid->Cell(ordinal,out.raw);
	if (!e) return e;
	if (!(e = Locate(in,out.raw.centreWorld,out.location))) return e;
	out.hdrAmbient = HdrColour(out.raw,Decode::Component::GridAmbient,decoder);
	out.hdrDirected = HdrColour(out.raw,Decode::Component::GridDirected,decoder);
	out.dynamicPossible = Dynamic(in,out.raw.centreWorld,out.raw.centreWorld);
	if (out.raw.presence == GridPresence::Present)
		out.direction = InterpretDirection(out.raw.latLong[0],out.raw.latLong[1],o.compression);
	for (unsigned s = 0; s < 4; ++s)
	{
		auto &style = out.styles[s];
		style.slot = uint8_t(s); style.id = out.raw.styles[s]; style.active = Active(out.raw,s);
		style.ambient = DecodeColour(out.raw,s,Decode::Component::GridAmbient,o,decoder);
		style.directed = DecodeColour(out.raw,s,Decode::Component::GridDirected,o,decoder);
		style.weakDirected = !style.active || std::max({out.raw.directed[s][0],out.raw.directed[s][1],
			out.raw.directed[s][2]}) <= o.weakDirectedCode;
	}
	if (!Free(out.location) || out.dynamicPossible || out.raw.presence != GridPresence::Present) return {};
	GridCell neighbours[6];
	const auto &layout = in.grid->Layout();
	uint64_t stride = 1;
	for (unsigned axis = 0; axis < 3; ++axis)
	{
		for (unsigned side = 0; side < 2; ++side)
		{
			const unsigned k = 2*axis+side;
			if ((!side && out.raw.coordinates[axis] == 0) ||
				(side && out.raw.coordinates[axis]+1 >= layout.bounds[axis])) continue;
			if (!(e = in.grid->Cell(side ? ordinal+stride : ordinal-stride,neighbours[k]))) return e;
			if (neighbours[k].presence != GridPresence::Present) continue;
			SpatialLocation location;
			if (!(e = Locate(in,neighbours[k].centreWorld,location))) return e;
			if (!Free(location)) { out.edges[k] = Edge::Unknown; continue; }
			if (Dynamic(in,out.raw.centreWorld,neighbours[k].centreWorld)) { out.edges[k] = Edge::Dynamic; continue; }
			out.edges[k] = Edge::Unknown;
			Bounds span; span.known = true;
			span.minWorld = Vec3(std::min(out.raw.centreWorld.x,neighbours[k].centreWorld.x),
				std::min(out.raw.centreWorld.y,neighbours[k].centreWorld.y),std::min(out.raw.centreWorld.z,neighbours[k].centreWorld.z));
			span.maxWorld = Vec3(std::max(out.raw.centreWorld.x,neighbours[k].centreWorld.x),
				std::max(out.raw.centreWorld.y,neighbours[k].centreWorld.y),std::max(out.raw.centreWorld.z,neighbours[k].centreWorld.z));
			// ClassifyBox requires positive volume. Expand outward (also at large world coordinates),
			// never shrink an unknown segment into a certified free one.
			const float infinity = std::numeric_limits<float>::infinity();
			span.minWorld = Vec3(std::nextafter(span.minWorld.x-1e-4f,-infinity),
				std::nextafter(span.minWorld.y-1e-4f,-infinity),std::nextafter(span.minWorld.z-1e-4f,-infinity));
			span.maxWorld = Vec3(std::nextafter(span.maxWorld.x+1e-4f,infinity),
				std::nextafter(span.maxWorld.y+1e-4f,infinity),std::nextafter(span.maxWorld.z+1e-4f,infinity));
			SpaceClass interior = SpaceClass::Unknown; uint64_t domainWork = 0;
			if (!in.domain) continue;
			if (!(e = in.domain->ClassifyBox(span,interior,domainWork))) return e;
			if (interior != SpaceClass::Free) continue;
			if (!in.visibility) continue;
			RayQuery q; q.startWorld = out.raw.centreWorld; q.endWorld = neighbours[k].centreWorld;
			q.originKnownFree = true; q.maxWork = o.maxRayWork;
			q.policy.facing = FacingPolicy::TwoSided;
			RayResult ray;
			if (!(e = in.visibility->Query(q,read,ray))) return e;
			out.rayWork += ray.nodeTests+ray.primitiveTests;
			if (ray.state == VisibilityState::Blocked) out.edges[k] = Edge::Blocked;
			// No approximate, optical-unknown, incomplete or assumption-based path is promoted.
			else if (ray.state == VisibilityState::Visible && ray.reliability == RayReliability::Geometric && !ray.issues)
				out.edges[k] = Edge::Clear;
		}
		stride *= layout.bounds[axis];
		if (out.edges[2*axis] != Edge::Clear || out.edges[2*axis+1] != Edge::Clear) continue;
		for (auto &style : out.styles)
		{
			if (!style.active || Slot(out.raw,style.id) != style.slot) continue;
			const int a = Slot(neighbours[2*axis],style.id), b = Slot(neighbours[2*axis+1],style.id);
			if (a < 0 || b < 0) continue;
			for (unsigned component = 0; component < 2; ++component)
			{
				const auto kind = component ? Decode::Component::GridDirected : Decode::Component::GridAmbient;
				const auto left = DecodeColour(neighbours[2*axis],unsigned(a),kind,o,decoder);
				const auto right = DecodeColour(neighbours[2*axis+1],unsigned(b),kind,o,decoder);
				auto &g = component ? style.directedGradient : style.ambientGradient;
				const double distance = 2*Axis(layout.spacingWorld,axis);
				g.intensity[axis] = Difference(left.intensity,right.intensity,distance);
				for (unsigned c = 0; c < 3; ++c) g.rgb[axis][c] = Difference(left.rgb[c],right.rgb[c],distance);
			}
		}
	}
	return {};
}
const char *SpaceName(SpaceClass s)
{
	switch (s) { case SpaceClass::Free: return "free"; case SpaceClass::Solid: return "solid";
	case SpaceClass::Outside: return "outside"; case SpaceClass::Sky: return "sky";
	case SpaceClass::Exterior: return "exterior"; default: return "unknown"; }
}
const char *EdgeName(Edge s)
{
	switch (s) { case Edge::Clear: return "clear"; case Edge::Blocked: return "blocked";
	case Edge::Unknown: return "unknown"; case Edge::Dynamic: return "dynamic"; default: return "unsupported"; }
}
} // namespace OriginalGridEvidence
