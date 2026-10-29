// Initial hypotheses only; never physical emitter certification or fitted lighting.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "surfaces.h"
#include "emission_geometry.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace Offline { struct Access; }
constexpr uint32_t kEmitterEvidenceVersion = 1;
enum CandidateShape : uint32_t
{
	CandidatePoint = 1u << 0,
	CandidateRectangle = 1u << 1,
	CandidateTube = 1u << 2,
	CandidateSpot = 1u << 3,
	CandidateSurface = 1u << 4,
	CandidateSun = 1u << 5,
	CandidateSky = 1u << 6
};
enum class EvidenceOrigin
{
	GlowSurface,
	SurfaceLight,
	AuthoredArea,
	AutomaticArea,
	BspEntity,
	SunShader,
	SkyShader,
	SurfaceObservation // appended: inferred candidate geometry, never authored intensity
};
enum class EvidenceUnit
{
	Unknown,
	RelativeShader,
	LegacyBakeTool,
	Candela,
	LuminanceNits,
	IlluminanceLux
};
enum EvidenceIssue : uint32_t
{
	UncalibratedEmission = 1u << 0,
	ShapeHypothesis = 1u << 1,
	MaskUnavailable = 1u << 2,
	MaskCoordinatesUnsupported = 1u << 3,
	AnimatedEvidence = 1u << 4,
	MovingEvidence = 1u << 5,
	EvidenceUnsupportedGeometry = 1u << 6,
	MissingTarget = 1u << 7,
	MultipleEmissionLayers = 1u << 8,
	ColourTransferUnknown = 1u << 9,
	AuthoredPrior = 1u << 10,
	DynamicAuthoredMode = 1u << 11,
	AmbiguousCorrespondence = 1u << 12,
	AlphaEmissionUnknown = 1u << 13,
	NoLuminousMaskSamples = 1u << 14
};
struct EmitterEvidence
{
	Id id = 0;
	EvidenceOrigin origin = EvidenceOrigin::GlowSurface;
	std::string source, material, emissionTexture, name, mode,
		raw; // verbatim JSON/entity or declaration where available
	uint32_t ordinal = 0, shapes = 0, issues = 0, materialSignals = 0;
	std::vector<uint32_t> surfaces, entities; // original BSP ordinals, not pointers
	bool positionKnown = false, orientationKnown = false, colourKnown = false,
		 intensityKnown = false;
	Vec3 position, right, up, outward, toSource,
		colour; // world units; outward=propagation, sun=receiver->sun
	float halfWidth = 0, halfHeight = 0, radius = 0, areaWorld = 0;
	float sourceRadiusHint = 0, coneDegrees = 0;
	bool coneKnown = false, chromaticityKnown = false;
	Vec3 chromaticity; // optional Y=1 from raw colour; does not calibrate intensity or transfer
	float intensity = 0, range = 0;
	EvidenceUnit unit = EvidenceUnit::Unknown;
	uint8_t style = 0;
	bool twoSided = false, maskSampled = false, staticEligible = false;
	float geometricCoverage = 0; // sampled area / bounding rectangle; NOT physical confidence
};
struct EmitterCandidate
{
	Id id = 0;
	uint32_t shapes = 0, issues = 0;
	bool staticEligible = false;
	// Primary exact authored geometry wins; all other observations remain evidence.
	size_t primary = 0;
	std::vector<size_t> evidence;
	std::vector<Id> sourceIds; // aliases survive merging, scoped to map SHA and evidence schema
};
struct EvidenceReport
{
	uint64_t triangles = 0, groups = 0, maskBlobs = 0, unsupported = 0, animated = 0, moving = 0,
			 authored = 0, entities = 0, suns = 0, skies = 0, bytes = 0;
};
struct EmissionMaskRequest
{
	std::string material, texture;
};
class EmissionMaskProvider
{
  public:
	virtual ~EmissionMaskProvider() = default;
	// Synchronous source CPU pixels; no GL readback required or allowed by the core.
	virtual Error Read(const EmissionMaskRequest &, const ReadContext &,
					   Emission::Mask &) const = 0;
};
struct ShaderEvidenceText
{
	std::string material, text;
}; // exact selected shader body, not global renderer sun
struct EmitterEvidenceInput
{
	LightmapMapIdentity map;
	std::shared_ptr<const SurfaceGeometry> geometry;
	std::shared_ptr<const EmissionMaskProvider> masks;
	std::vector<ShaderEvidenceText> shaders;
	std::string entityText, areaJson, areaPath;
};
struct EvidenceOptions
{
	uint64_t maxBytes = 64ull * 1024 * 1024, maxTriangles = 500000, maxWork = 50000000;
	size_t maxEvidence = 100000, maxTextBytes = 8 * 1024 * 1024;
	uint32_t maskAxis = 96;
	float maskThreshold = 0.1f, pointExtent = 4, tubeAspect = 6;
};
class EmitterEvidenceSet
{
  public:
	const LightmapMapIdentity &Map() const
	{
		return map_;
	}
	const std::vector<EmitterEvidence> &Evidence() const
	{
		return evidence_;
	}
	const std::vector<EmitterCandidate> &Candidates() const
	{
		return candidates_;
	}
	const EvidenceReport &Report() const
	{
		return report_;
	}

  private:
	friend struct Offline::Access;
	friend Error ExtractEmitterEvidence(const EmitterEvidenceInput &, const EvidenceOptions &,
										const ReadContext &,
										std::shared_ptr<const EmitterEvidenceSet> &);
	LightmapMapIdentity map_;
	std::vector<EmitterEvidence> evidence_;
	std::vector<EmitterCandidate> candidates_;
	EvidenceReport report_;
};
// Explicit synchronous build. No renderer controls, files, reconstruction or mutation on read.
// Failure clears output; published sets own all strings/geometry hints, not source/world pointers.
Error ExtractEmitterEvidence(const EmitterEvidenceInput &, const EvidenceOptions &,
							 const ReadContext &, std::shared_ptr<const EmitterEvidenceSet> &);
const char *EvidenceOriginName(EvidenceOrigin);
} // namespace Core
} // namespace Rend2LightRecon
