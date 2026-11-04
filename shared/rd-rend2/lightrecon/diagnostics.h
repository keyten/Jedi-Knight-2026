// V03 data-only diagnostics. No renderer resources or numerical light model.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "field_builder.h"
#include "joint.h"
#include "lifecycle.h"
#include "producer.h"
#include "shadow_cache.h"
#include <ostream>
#include <set>
namespace Rend2LightRecon
{
namespace Core
{
namespace Diagnostics
{
constexpr uint32_t kVersion = 1;
enum class Layer : uint32_t
{
	Sources,
	Placement,
	Dynamic,
	Receivers,
	Direct,
	Sun,
	Sky,
	Indirect,
	Unresolved,
	Residual,
	Probes,
	Field,
	Visibility,
	Transport,
	Count
};
enum class State : uint32_t
{
	Accepted,
	Conditional,
	Rejected,
	Deferred,
	Budget,
	Unavailable,
	Unknown,
	Visible,
	Blocked,
	Invalid,
	Count
};
enum class Primitive : uint32_t
{
	Point,
	Line,
	Triangle
};
struct Record
{
	Id id = 0, source = 0, sample = 0, region = 0, configuration = 0, alternative = 0;
	Layer layer = Layer::Sources;
	State state = State::Unknown;
	Primitive primitive = Primitive::Point;
	bool geometryKnown = false, valueKnown = false, lowerKnown = false, upperKnown = false;
	Vec3 a, b, c, value, lower, upper;
	PhotometricUnit unit = PhotometricUnit::Relative;
	uint32_t issues = 0;
	std::string quantity, domain, profile, reason, provenance;
};
struct Limits
{
	size_t maxRecords = 16384;
	uint64_t maxBytes = 16ull << 20;
	size_t maxString = 4096;
};
struct Scene
{
	Disk::Digest map{}, fingerprint{};
	std::string mapPath, algorithm, settings, scope;
	uint64_t generation = 0, requested = 0, omitted = 0, invalidQueries = 0, unknownQueries = 0,
			 rays = 0;
	Units units;
	bool complete = false;
	std::vector<Record> records;
};
const char *Name(Layer);
const char *Name(State);
// Never assigns physical confidence, mutates source state, or runs a fit.
class Builder
{
	Scene &scene_;
	Limits limits_;
	uint64_t bytes_ = 0;

  public:
	explicit Builder(Scene &, Limits = {});
	bool Add(Record);
	void Box(Id, Layer, State, const Bounds &, const std::string &, Id source = 0);
	void Emitter(const PhysicalEmitter &, State, const std::string &profile,
				 const std::string &reason, Vec3 directionalWidgetAnchor = Vec3());
	void Placement(const Joint::Source &, const PointRefine::PlacementResult &,
				   const char *predicate, Vec3 from, Vec3 to, double radius,
				   const std::string &profile);
	void Dynamic(const DynamicBoundary &);
	void Probes(const Probes::Topology &);
	void ProbeQuery(Id, const Probes::Topology &, Vec3,
					const Probes::QueryPolicy & = Probes::QueryPolicy());
	void Field(const Fields::Artifact &);
	void Ray(Id, Id source, Vec3 from, Vec3 to, const RayResult &, const char *scope);
	void Shadow(const ShadowCache::Artifact &);
};
class PlacementCapture final : public Joint::DiagnosticSink
{
	Scene &scene_;
	Builder builder_;
	std::vector<std::string> profiles_;
	size_t maximum_, stored_ = 0;
	std::set<Disk::Digest> seen_;

  public:
	PlacementCapture(Scene &, std::vector<std::string> profiles, size_t maximum = 128, Limits = {});
	void Placement(const Joint::Source &, const PointRefine::PlacementResult &, const char *, Vec3,
				   Vec3, double, size_t) noexcept override;
};
// Explicit diagnostic-only work AFTER search/freeze. Uses the production Shape
// operator on training observations only; it never certifies a rejected source.
// Work and omissions are separate from fitting counters.
Error TrainingPairs(const Joint::Request &, const std::vector<Joint::Candidate> &,
					size_t maxSources, size_t maxRows, const ReadContext &, Scene &);
Error Contributions(const Producer::Artifact &, const ReadContext &, Scene &,
					size_t maximumRows = 128);
// Frozen canonical diagnostic receipts only; never a training reader or a
// feedback path to model selection. Reuses S13 observation/sample codecs.
Error FrozenObservations(const Producer::Artifact &, const ReadContext &, Scene &,
						 size_t maximumRows = 128);
struct Selection
{
	uint32_t layers = 0;
	Id source = 0, region = 0, configuration = 0;
	bool Matches(const Record &) const;
};
void Colour(State, uint8_t rgb[3]);
// Opt-in bounded spatial interchange, not a canonical lighting cache. IDs are
// exact uint64; map hash, schema, numeric values and checksum checked on import.
Error Encode(const Scene &, std::string &, const Limits & = Limits());
Error Decode(const std::string &, const Disk::Digest &expectedMap, Scene &,
			 const Limits & = Limits());
Error Json(const Scene &, std::ostream &, const Limits & = Limits());
Error Csv(const Scene &, std::ostream &, const Limits & = Limits());
Error Obj(const Scene &, std::ostream &, const Selection & = Selection(),
		  const Limits & = Limits());
// No raw RGB8/linear comparison. Caller declares the common domain, units and
// interval comparability. Signed discrepancy is never physical indirect energy.
Record Compare(Id, Vec3 position, const Vec3 &prediction, const Vec3 &lo, const Vec3 &hi,
			   uint8_t present, uint8_t unbounded, const std::string &domain, PhotometricUnit,
			   bool comparable);
struct Heatmap
{
	double minimum = 0, maximum = 1;
	bool signedValues = false;
};
Error HeatmapPpm(const std::vector<Record> &, Layer, unsigned width, const Heatmap &, std::string &,
				 const Limits & = Limits());
// Snapshot/lifecycle inspection only. Missing confidence/scale stays JSON null.
Error Report(const Snapshot &, const WorldReport *, std::ostream &json,
			 std::ostream *human = nullptr, const Scene * = nullptr, const Limits & = Limits());
} // namespace Diagnostics
} // namespace Core
} // namespace Rend2LightRecon
