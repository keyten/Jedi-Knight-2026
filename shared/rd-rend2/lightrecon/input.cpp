// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "input.h"
#include <cmath>
#include <exception>
#include <limits>
#include <new>
#include <utility>

namespace Rend2LightRecon { namespace Core {
namespace {
bool Finite(const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); }
double Dot(const Vec3& a, const Vec3& b) { return double(a.x)*b.x + double(a.y)*b.y + double(a.z)*b.z; }
bool Unit(const Vec3& v) { return Finite(v) && std::abs(Dot(v, v) - 1.0) <= kUnitTolerance; }
bool Fraction(float f) { return std::isfinite(f) && f >= 0.0f && f <= 1.0f; }
bool Nonnegative(const Vec3& v) { return Finite(v) && v.x >= 0.0f && v.y >= 0.0f && v.z >= 0.0f; }
bool ProvenanceRef(Id id, const InputManifest& m)
{
	for (const Provenance& p : m.provenanceRecords) if (p.id == id) return true;
	return false;
}
bool Light(const ObservedLighting& v, const InputManifest& m)
{
	if (!Nonnegative(v.rgb) || (v.unit != PhotometricUnit::Relative && v.unit != PhotometricUnit::Lux)) return false;
	if (!v.calibration) return !v.known && v.unit == PhotometricUnit::Relative;
	for (const Calibration& c : m.calibrations)
		if (c.id == v.calibration) return (v.unit == PhotometricUnit::Relative) == (c.kind == CalibrationKind::UncalibratedRelative);
	return false;
}
Error Record(const SurfaceObservation& v, const InputManifest& m)
{
	if (!v.surface || !ProvenanceRef(v.provenance, m) || !Finite(v.positionWorld) || !Finite(v.normalWorld) ||
		(v.normalKnown && !Unit(v.normalWorld)) || !Light(v.lighting, m) ||
		!std::isfinite(v.areaWorldSquared) || v.areaWorldSquared < 0.0f)
		return Failure(Code::InvalidInput, "invalid surface observation, normal, provenance or lighting");
	return Error();
}
Error Record(const GridObservation& v, const InputManifest& m)
{
	if (!ProvenanceRef(v.provenance, m) || !Finite(v.positionWorld) || !Finite(v.directionToSource) ||
		(v.directionKnown && !Unit(v.directionToSource)) || !Light(v.ambient, m) || !Light(v.directed, m))
		return Failure(Code::InvalidInput, "invalid grid observation, direction, provenance or lighting");
	return Error();
}
Error Record(const Triangle& v, const InputManifest& m)
{
	if (!ProvenanceRef(v.provenance, m)) return Failure(Code::InvalidInput, "missing triangle provenance");
	for (const Vec3& p : v.verticesWorld) if (!Finite(p)) return Failure(Code::InvalidInput, "non-finite triangle vertex");
	const Vec3& a = v.verticesWorld[0]; const Vec3& b = v.verticesWorld[1]; const Vec3& c = v.verticesWorld[2];
	const double ux = double(b.x)-a.x, uy = double(b.y)-a.y, uz = double(b.z)-a.z;
	const double vx = double(c.x)-a.x, vy = double(c.y)-a.y, vz = double(c.z)-a.z;
	const double x = uy*vz-uz*vy, y = uz*vx-ux*vz, z = ux*vy-uy*vx;
	if (!(x*x+y*y+z*z > 0.0)) return Failure(Code::InvalidInput, "degenerate triangle");
	return Error();
}
Error Record(const Material& v, const InputManifest& m)
{
	if (!ProvenanceRef(v.provenance, m) || static_cast<unsigned>(v.support) > static_cast<unsigned>(MaterialSupport::Unsupported) ||
		!Fraction(v.baseColorLinear.x) || !Fraction(v.baseColorLinear.y) || !Fraction(v.baseColorLinear.z) ||
		!Fraction(v.metallic) || !Fraction(v.perceptualRoughness)) return Failure(Code::InvalidInput, "invalid material");
	return Error();
}
Error Record(const SourceHint& v, const InputManifest& m)
{
	if (!ProvenanceRef(v.provenance, m) || static_cast<unsigned>(v.kind) > static_cast<unsigned>(HintKind::TransportAperture))
		return Failure(Code::InvalidInput, "invalid hint kind or provenance");
	if (v.kind == HintKind::TransportAperture)
		return v.geometry ? Error() : Failure(Code::InvalidInput, "aperture hint needs geometry; it is not an emitter");
	ReconstructedLightingAsset a = AssetMetadata(m);
	if (v.kind == HintKind::EmissiveSurface)
	{
		if (!v.geometry) return Failure(Code::InvalidInput, "emissive surface hint needs geometry");
		EnvironmentLighting e; e.source.id = v.id; e.source.provenance = v.provenance;
		e.meanLuminance = v.surfaceEmission; a.environments.push_back(e); // reuse photometry validation only
	}
	else
	{
		a.emitters.push_back(v.emitter);
		if (v.kind == HintKind::Sun) { SunSource sun; sun.emitter = v.emitter.source.id; a.suns.push_back(sun); }
	}
	std::string reason;
	return Validate(a, &reason) ? Error() : Failure(Code::InvalidInput, reason);
}
Error ProviderResult(Error error, const char* path)
{
	if (static_cast<unsigned>(error.code) > static_cast<unsigned>(Code::AlgorithmError))
		return Failure(Code::ProviderError, "provider returned an unknown error code", path);
	if (!error && error.path.empty()) error.path = path;
	return error;
}
bool Cancelled(const std::shared_ptr<const Cancellation>& c) { return c && c->IsCancelled(); }
Error CancelError() { return Failure(Code::Cancelled, "reconstruction input operation cancelled"); }
} // namespace

Error Failure(Code code, const std::string& message, const std::string& path)
{
	Error e; e.code = code; e.message = message; e.path = path; return e;
}
ReconstructedLightingAsset AssetMetadata(const InputManifest& m)
{
	ReconstructedLightingAsset a; a.lineage = m.lineage; a.units = m.units; a.provenance = m.provenance;
	a.provenanceRecords = m.provenanceRecords; a.calibrations = m.calibrations; a.regions = m.regions;
	a.hypothesisGroups = m.hypothesisGroups; a.datasets = m.referencedDatasets; return a;
}
Error ValidateInput(const InputDataset& input)
{
	const InputManifest& m = input.manifest;
	if (m.schema != kInputSchema || m.version != kInputVersion) return Failure(Code::UnsupportedSchema, "unsupported input schema/version", "manifest.schema");
	if (static_cast<unsigned>(m.gridMeaning) > static_cast<unsigned>(GridMeaning::LegacyHdrSum))
		return Failure(Code::InvalidInput, "invalid grid component convention", "manifest.gridMeaning");
	if ((m.surfaces || m.grid || m.triangles || m.materials || m.hints) && (m.lineage.empty() || m.revision.empty() || !ProvenanceRef(m.provenance, m)))
		return Failure(Code::InvalidInput, "nonempty input needs lineage, immutable revision and root provenance", "manifest");
	std::string reason;
	if (!Validate(AssetMetadata(m), &reason)) return Failure(Code::InvalidInput, reason, "manifest.metadata");
	if (m.triangles && !input.triangles) return Failure(Code::MissingGeometry, "declared geometry has no provider", "triangles");
	if ((m.surfaces && !input.surfaces) || (m.grid && !input.grid) || (m.materials && !input.materials) || (m.hints && !input.hints))
		return Failure(Code::Unavailable, "declared input stream has no provider", "manifest.streams");
	return Error();
}
Error VisibilityProvider::Segment(const Vec3&, const Vec3&, const ReadContext&, float&) const { return Failure(Code::Unavailable, "segment visibility unavailable"); }
Error VisibilityProvider::Sky(const Vec3&, const Vec3&, const ReadContext&, float&) const { return Failure(Code::Unavailable, "sky visibility unavailable"); }

InputReader::InputReader(InputDataset input, ReadLimits limits, std::shared_ptr<const Cancellation> cancellation)
	: input_(std::move(input)), limits_(limits), cancellation_(cancellation)
{
	metadataError_ = ValidateInput(input_);
	if (!limits_.maxRecords || !limits_.maxBytes) metadataError_ = Failure(Code::InvalidInput, "read limits must be positive", "limits");
}
Error InputReader::ValidateMetadata() const { return metadataError_; }
template<typename T> Error InputReader::Read(const std::shared_ptr<const ChunkSource<T>>& source,
	uint64_t total, uint64_t first, size_t count, Chunk<T>& out, Ordering& order, const char* path)
{
	out = Chunk<T>();
	if (!metadataError_) return metadataError_;
	if (Cancelled(cancellation_)) return CancelError();
	if (first > total || count > total-first) return Failure(Code::InvalidInput, "requested span outside stream", path);
	if (count > limits_.maxRecords || count > limits_.maxBytes / sizeof(T)) return Failure(Code::MemoryLimit, "requested chunk exceeds read budget", path);
	if (!count) return Error();
	if (!source) return Failure(Code::Unavailable, "stream provider unavailable", path);
	try
	{
		ReadContext context; context.maxBytes = limits_.maxBytes; context.cancellation = cancellation_.get();
		Chunk<T> block;
		Error error = ProviderResult(source->Read(first, count, context, block), path);
		if (Cancelled(cancellation_)) return CancelError();
		if (!error) return error;
		if (!block.owner || !block.data || block.count != count) return Failure(Code::ProviderError, "invalid lease or short/oversized successful read", path);
		Id previous = first == order.end ? order.last : 0;
		for (size_t i = 0; i < count; ++i)
		{
			if (Cancelled(cancellation_)) return CancelError();
			error = Record(block.data[i], input_.manifest);
			if (!block.data[i].id || block.data[i].id <= previous) error = Failure(Code::InvalidInput, "record IDs must be strictly increasing");
			if (!error) { error.path = path; error.ordinal = first+i; error.record = block.data[i].id; return error; }
			previous = block.data[i].id;
		}
		order.end = first+count; order.last = previous; out = block; return Error();
	}
	catch (const std::bad_alloc&) { return Failure(Code::MemoryLimit, "provider/validation allocation failed", path); }
	catch (const std::exception& e) { return Failure(Code::ProviderError, e.what(), path); }
	catch (...) { return Failure(Code::ProviderError, "provider threw an unknown exception", path); }
}
Error InputReader::ReadSurfaces(uint64_t first, size_t count, Chunk<SurfaceObservation>& out) { return Read(input_.surfaces, input_.manifest.surfaces, first, count, out, surfaceOrder_, "surfaces"); }
Error InputReader::ReadGrid(uint64_t first, size_t count, Chunk<GridObservation>& out) { return Read(input_.grid, input_.manifest.grid, first, count, out, gridOrder_, "grid"); }
Error InputReader::ReadTriangles(uint64_t first, size_t count, Chunk<Triangle>& out) { return Read(input_.triangles, input_.manifest.triangles, first, count, out, triangleOrder_, "triangles"); }
Error InputReader::ReadMaterials(uint64_t first, size_t count, Chunk<Material>& out) { return Read(input_.materials, input_.manifest.materials, first, count, out, materialOrder_, "materials"); }
Error InputReader::ReadHints(uint64_t first, size_t count, Chunk<SourceHint>& out) { return Read(input_.hints, input_.manifest.hints, first, count, out, hintOrder_, "hints"); }

Error InputReader::TraceSegment(const Vec3& receiver, const Vec3& target, float& transmittance) const
{
	if (!Finite(receiver) || !Finite(target) || (receiver.x == target.x && receiver.y == target.y && receiver.z == target.z))
		return Failure(Code::InvalidInput, "visibility needs two finite distinct endpoints", "visibility.segment");
	if (!metadataError_) return metadataError_;
	if (Cancelled(cancellation_)) return CancelError();
	if (!input_.visibility) return Failure(Code::Unavailable, "visibility provider unavailable", "visibility.segment");
	try
	{
		ReadContext context; context.maxBytes = limits_.maxBytes; context.cancellation = cancellation_.get();
		float value = std::numeric_limits<float>::quiet_NaN();
		Error error = ProviderResult(input_.visibility->Segment(receiver, target, context, value), "visibility.segment");
		if (Cancelled(cancellation_)) return CancelError();
		if (!error) return error;
		if (!Fraction(value)) return Failure(Code::ProviderError, "visibility returned invalid transmittance", "visibility.segment");
		transmittance = value; return Error();
	}
	catch (const std::bad_alloc&) { return Failure(Code::MemoryLimit, "visibility allocation failed"); }
	catch (const std::exception& e) { return Failure(Code::ProviderError, e.what(), "visibility.segment"); }
	catch (...) { return Failure(Code::ProviderError, "visibility provider threw"); }
}
Error InputReader::TraceSky(const Vec3& receiver, const Vec3& directionToSky, float& transmittance) const
{
	if (!Finite(receiver) || !Unit(directionToSky)) return Failure(Code::InvalidInput, "sky visibility needs finite position and unit receiver-to-sky direction", "visibility.sky");
	if (!metadataError_) return metadataError_;
	if (Cancelled(cancellation_)) return CancelError();
	if (!input_.visibility) return Failure(Code::Unavailable, "visibility provider unavailable", "visibility.sky");
	try
	{
		ReadContext context; context.maxBytes = limits_.maxBytes; context.cancellation = cancellation_.get();
		float value = std::numeric_limits<float>::quiet_NaN();
		Error error = ProviderResult(input_.visibility->Sky(receiver, directionToSky, context, value), "visibility.sky");
		if (Cancelled(cancellation_)) return CancelError();
		if (!error) return error;
		if (!Fraction(value)) return Failure(Code::ProviderError, "sky visibility returned invalid transmittance", "visibility.sky");
		transmittance = value; return Error();
	}
	catch (const std::bad_alloc&) { return Failure(Code::MemoryLimit, "sky visibility allocation failed"); }
	catch (const std::exception& e) { return Failure(Code::ProviderError, e.what(), "visibility.sky"); }
	catch (...) { return Failure(Code::ProviderError, "sky visibility provider threw"); }
}

} } // namespace Rend2LightRecon::Core
