// X15 immutable geometry-only observation comparison domain. No target RGB or source XYZ.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "emitter_shape.h"
#include <ostream>
namespace Rend2LightRecon
{
namespace Core
{
namespace ReceiverSupport
{
constexpr uint32_t kVersion = 2; // fixed compiler receiver-origin certification
enum class Role
{
	Training,
	Validation,
	Heldout,
	Gap
};
enum class Channel
{
	Surface,
	GridAmbient,
	GridDirected,
	GridDirection,
	DeluxeDirection
};
enum class Reason : uint32_t
{
	Supported,
	InvalidGeometry,
	UnknownProfile,
	ProfileRule,
	UnsupportedSource,
	UnsupportedDomain,
	UnsupportedStyle,
	NonPlanar,
	UncertifiedPlacement,
	GeometryError,
	MappingError,
	DistinctNormals,
	OrderedGrid,
	MaterialRequired,
	UnknownTransfer,
	UnknownPrimaries,
	InvalidCalibration,
	MissingVisibility,
	UnsupportedSampling,
	GeometryUnavailable,
	DisabledChannel,
	PartitionGap,
	ReceiverOriginSolid,
	ReceiverOriginBoundary,
	ReceiverOriginUnknown,
	Count
};
enum Uncertainty : uint32_t
{
	MaterialUnknown = 1,
	OpticalUnknown = 2,
	GeometryApproximate = 4,
	AssumedProfile = 8,
	UnknownArea = 16
};
struct Operator
{
	Shape::Model model;
	Bake::Options bake;
	Shape::Sampling sampling; // declared strict receiver/source quadrature policy, geometry only
	// Declared feature classes, independent of candidate identities/positions/RGB.
	std::vector<Bake::SourceKind> sources = {Bake::SourceKind::Point, Bake::SourceKind::Spot};
	bool transferAttested = true, primariesAttested = true, requiresMaterial = false;
	bool physicalEnvironmentImplemented = false;
	std::string scope;
};
struct Geometry
{
	Bake::Receiver receiver;
	Id region = 0, block = 0;
	Role role = Role::Training;
	Channel channel = Channel::Surface;
	double areaWorldSquared = 0; // actual receiver area, never a representative weight
	bool enabled = true, geometryKnown = true, materialKnown = false, opticsKnown = false,
		 required = true;
	bool materialAvailable =
		false; // bounded/conditional material hypothesis, distinct from known reflectance
	uint8_t channelMask = 7; // channel PRESENCE only, no measured values
};
struct Decision
{
	Reason reason = Reason::Supported;
	Bake::CapabilityReason capability = Bake::CapabilityReason::Supported;
};
Disk::Digest OperatorKey(const Operator &);
Disk::Digest GeometryKey(const Geometry &);
Decision Check(const Operator &, const Geometry &, const Bake::EvaluationContext &);
// Uses the declared compiler push, unchanged normals, and the SAME zero-tolerance
// classifier as the historical forward ray. No source XYZ or target RGB needed.
Error CheckOrigin(const Operator &, const Geometry &, const Bake::EvaluationContext &,
				  const PointClassifier *, Decision &, uint64_t &probes);
struct Entry
{
	Id sample = 0, surface = 0, material = 0, region = 0, block = 0;
	Role role = Role::Training;
	Channel channel = Channel::Surface;
	double areaWorldSquared = 0;
	uint32_t uncertainty = 0;
	uint8_t channelMask = 7;
	bool required = true;
	Disk::Digest geometry{};
	std::vector<Decision> profiles;
	bool common = false;
};
struct Summary
{
	uint64_t requested = 0, admitted = 0, unknownGeometry = 0, unknownOptics = 0,
			 unknownMaterial = 0, unknownArea = 0;
	uint64_t requiredGroups = 0, checks = 0, bytes = 0, originProbes = 0;
	double requestedArea = 0, commonArea = 0;
	uint64_t roleRequested[4] = {}, roleCommon[4] = {};
	double roleArea[4] = {}, roleCommonArea[4] = {};
	std::vector<uint64_t> profileAdmitted;
	std::vector<double> profileArea;
};
struct Request
{
	Disk::Digest map{}, geometry{}, partition{}, policy{};
	Bake::EvaluationContext context;
	std::shared_ptr<const PointClassifier> domain;
	Disk::Digest domainKey{}, traceKey{};
	std::vector<Operator> operators;
	size_t maxRows = 100000, batchRows = 128;
	uint64_t maxBytes = 64ull << 20, maxChecks = 16000000;
};
class Manifest
{
  public:
	const Disk::Digest &Key() const
	{
		return key_;
	}
	const Request &Policy() const
	{
		return request_;
	}
	const Summary &Report() const
	{
		return summary_;
	}
	const std::vector<Entry> &Entries() const
	{
		return entries_;
	}
	const Entry *Find(Id) const;
	bool ContainsOperator(const Operator &) const;
	Error Match(const Geometry &, const Entry *&) const;

  private:
	Request request_;
	Summary summary_;
	Disk::Digest key_{};
	std::vector<Entry> entries_;
	std::vector<Disk::Digest> operators_;
	friend Error Build(const Request &, std::shared_ptr<const ChunkSource<Geometry>>, uint64_t,
					   const ReadContext &, std::shared_ptr<const Manifest> &);
};
Error Build(const Request &, std::shared_ptr<const ChunkSource<Geometry>>, uint64_t,
			const ReadContext &, std::shared_ptr<const Manifest> &);
// Streaming output: one row at a time, no dense source x luxel payload.
Error WriteJson(const Manifest &, std::ostream &, const ReadContext &);
const char *ReasonName(Reason);
enum class RuntimeState
{
	Available,
	KnownBlocked,
	UnknownVisibility,
	UnsupportedSource,
	WorkLimit,
	MissingPrediction,
	NonFinite,
	Excluded
};
struct RuntimePrediction
{
	Id sample = 0;
	RuntimeState state = RuntimeState::MissingPrediction;
	Vec3 value;
};
struct Coverage
{
	bool complete = false;
	uint64_t requested = 0, admitted = 0, excluded = 0, used = 0, blocked = 0, missing = 0,
			 unknownVisibility = 0, unsupported = 0, workLimited = 0, nonFinite = 0;
	Disk::Digest denominator{};
};
// A complete blocked-zero is admissible; an absent row is never black. The subset
// is selected BEFORE source evaluation (e.g. one fixed coarse spatial schedule).
Error AuditCoverage(const Manifest &, const std::vector<Id> &subset,
					const std::vector<RuntimePrediction> &, const ReadContext &, Coverage &);
} // namespace ReceiverSupport
} // namespace Core
} // namespace Rend2LightRecon
