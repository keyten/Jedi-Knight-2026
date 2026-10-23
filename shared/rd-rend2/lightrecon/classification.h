// Baked evidence typing and conservative fitting eligibility. CPU only.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lightmaps.h"
#include "decode.h"

namespace Rend2LightRecon
{
namespace Core
{
constexpr uint32_t kClassificationVersion = 1;
constexpr size_t kObservationReasonCount = 29;
class SurfaceGeometry;
struct BakedSurface;
struct LuxelTile;

enum MaterialSignal : uint32_t
{
	MaterialLightmapped = 1u << 0,
	MaterialVertexLit = 1u << 1,
	MaterialGlow = 1u << 2,
	MaterialSky = 1u << 3,
	MaterialCutout = 1u << 4,
	MaterialTranslucent = 1u << 5,
	MaterialUnlit = 1u << 6,
	MaterialAnimated = 1u << 7,
	MaterialDeformed = 1u << 8,
	MaterialNoDraw = 1u << 9,
	MaterialMissing = 1u << 10,
	MaterialReflection = 1u << 11,
	MaterialEntityDependent = 1u << 12
};
enum MaterialUncertainty : uint32_t
{
	BakeReflectanceUnknown = 1u << 0,
	TextureIsNotBakeAlbedo = 1u << 1,
	TexturePixelsUnavailable = 1u << 2,
	MultipleBaseLayers = 1u << 3,
	RuntimeShaderInterpretation = 1u << 4,
	EmissionUncalibrated = 1u << 5,
	MaterialValuesInvalid = 1u << 6,
	TextureTransformPresent = 1u << 7
};
enum class BakedMeaning
{
	Unknown,
	DiffuseModulationBakeUnknown // current shader multiplies texture; bake may contain
								 // transport/material effects
};
enum class EmissionProvenance
{
	None,
	GlowTexture,
	LegacyShader,
	AuthoredLuminance
};
struct MaterialFacts
{
	bool available = false; // copied facts of the actual parsed shader variant, never a GPU handle
	uint32_t signals = 0;
	uint32_t uncertainty =
		BakeReflectanceUnknown | TextureIsNotBakeAlbedo | TexturePixelsUnavailable;
	BakedMeaning meaning = BakedMeaning::Unknown;
	uint32_t surfaceFlags = 0,
			 contentFlags = 0; // verbatim source engine facts; core uses named signals
	std::string baseTexture, emissionTexture;
	bool baseTextureSrgb = false, baseModulationKnown = false;
	Vec3 baseModulation; // shader colour parameter, NOT a reflectance measurement
	// Optional independently supplied source-linear texture candidate. Runtime
	// does not read pixels back or mistake its bright-part SSR statistic for albedo.
	bool baseColourKnown = false;
	Vec3 baseColourLinear;
	uint32_t baseLayerCount = 0, emissionLayerCount = 0;
	EmissionProvenance emissionProvenance = EmissionProvenance::None;
	bool emissionColourKnown = false;
	Vec3 emissionColour; // relative texture/shader modulation; distinct from incident baked RGB
	float authoredLuminance = 0;  // only AuthoredLuminance: cd/m2; never inferred from glow
	float legacySurfaceLight = 0; // q3map_surfacelight, uncalibrated bake-tool units
	Vec3 legacySourceColour;	  // q3map_lightRGB, raw shader parameter; unknown transfer
};
uint32_t
MaterialUncertaintyFlags(const MaterialFacts &); // preserves invalid raw fields, vetoes fitting

enum class StyleStatus
{
	Unused,
	Static,
	Styled
}; // nonzero ID: animation unknown, never silently static
struct StyleChannel
{
	uint32_t slot = 0;
	uint8_t id = 255;
	StyleStatus status = StyleStatus::Unused;
};
StyleChannel LightStyle(uint32_t slot, uint8_t id);
enum class ObservationPath
{
	StaticLightmap,
	StyledLightmap,
	VertexColour,
	Other,
	Excluded
};
enum ObservationReason : uint64_t
{
	RelativeBakedCandidate = 1ull << 0,
	SeparateStyledChannel = 1ull << 1,
	VertexEvidenceOnly = 1ull << 2,
	UnknownBakeAlbedo = 1ull << 3,
	UncalibratedBakedScale = 1ull << 4,
	NoLightmap = 1ull << 5,
	UnusedStyle = 1ull << 6,
	MissingMaterial = 1ull << 7,
	UnsupportedMaterial = 1ull << 8,
	SkyBoundary = 1ull << 9,
	EmissiveHintOnly = 1ull << 10,
	AlphaCoverageUnknown = 1ull << 11,
	TranslucentTransport = 1ull << 12,
	UnlitPresentation = 1ull << 13,
	AnimatedMaterial = 1ull << 14,
	MovingGeometry = 1ull << 15,
	DeformedGeometry = 1ull << 16,
	UnknownGeometryOwnership = 1ull << 17,
	IntentionallyExcluded = 1ull << 18,
	MalformedGeometry = 1ull << 19,
	NoCoveredCentre = 1ull << 20,
	AmbiguousChart = 1ull << 21,
	UnsupportedChart = 1ull << 22,
	MissingColourImage = 1ull << 23,
	InvalidSourceCombination = 1ull << 24,
	MultipleChartReferences = 1ull << 25,
	VertexPaintOrLightingUnknown = 1ull << 26,
	InvalidColourData = 1ull << 27,
	ReflectionPresentation = 1ull << 28
};
struct ObservationClassification
{
	ObservationPath path = ObservationPath::Excluded;
	StyleChannel style;
	uint32_t materialSignals = 0, materialUncertainty = 0;
	BakedMeaning meaning = BakedMeaning::Unknown;
	uint64_t reasons = 0;
	bool diffuseFitCandidate =
		false; // relative fit only, conditional on unknown bake/material history
	bool supplementaryEvidence = false, emitterHint = false;
};
const char *ObservationPathName(ObservationPath);
const char *ObservationReasonName(uint64_t singleBit);
ObservationClassification ClassifySurface(const BakedSurface &, uint32_t slot, bool vertex);
Error ClassifyLuxel(const LuxelTile &, size_t sample, ObservationClassification &);

enum class DeluxeState
{
	Available,
	Missing,
	Unpaired,
	Ambiguous,
	DimensionsMismatch,
	UnsupportedEncoding,
	ZeroSentinel,
	Neutral,
	Invalid
};
const char *DeluxeStateName(DeluxeState);
struct DeluxeDirection
{
	DeluxeState state = DeluxeState::Missing;
	uint8_t raw[4] = {0, 0, 0, 255};
	Vec3 signedEncoded,
		toSource; // RGB/255 - .5; WORLD receiver -> dominant incoming light, not propagation
	float componentStep = 1.0f / 255, angularBoundRadians = 0;
	bool confidenceKnown = false; // quantization bound is not reconstruction confidence
};
DeluxeDirection DecodeDeluxeRgb8(const uint8_t rgb[4]);
struct DeluxeBinding
{
	std::shared_ptr<const LightmapObservations> owner;
	size_t lighting = 0, deluxe = 0;
	DeluxeState state = DeluxeState::Missing;
	bool alternativeFallback = false; // e.g. external colour with embedded direction
};
// Resolves explicit pairs only, never adjacency/filename guessing. Optional
// candidate ordinal chooses between alternatives; SIZE_MAX requests an unambiguous match.
Error BindDeluxe(std::shared_ptr<const LightmapObservations>, size_t lighting, DeluxeBinding &,
				 size_t candidate = SIZE_MAX);
struct DeluxeSample
{
	DeluxeBinding binding;
	uint32_t x = 0, y = 0;
	DeluxeDirection
		direction; // supplementary evidence, never an emitter/sun or tangent-space normal
};
Error ReadDeluxe(const DeluxeBinding &, uint32_t x, uint32_t y, const ReadContext &,
				 DeluxeSample &);
Error ReadLuxelDeluxe(const LuxelTile &, size_t sample, const ReadContext &, DeluxeSample &);

enum class VertexHdrState
{
	NotRequested,
	Missing,
	Present,
	Invalid,
	ReadFailed
};
struct VertexObservation
{
	std::shared_ptr<const SurfaceGeometry> owner;
	uint32_t surface = 0, vertex = 0, sourceVertex = UINT32_MAX;
	StyleChannel style;
	Vec3 positionWorld, shadingNormal;
	bool shadingNormalKnown = false;
	// One vertex can touch several faces; no invented unique geometric normal,
	// area or dense chart. Triangle geometry remains in the pinned snapshot.
	ObservationClassification classification;
	Decode::Observation colour; // exact original RGBA for this source style
	VertexHdrState hdrState = VertexHdrState::NotRequested;
	Decode::Observation hdrAlternative;	   // same position, NO additional energy observation
	bool hdrStyleAssociationKnown = false; // vertlight.raw has one RGB tuple, no style IDs
};
Error ReadVertexObservation(const Decode::Decoder &, std::shared_ptr<const SurfaceGeometry>,
							uint32_t surface, uint32_t vertex, uint32_t slot, const ReadContext &,
							VertexObservation &);

struct ObservationSummary
{
	uint64_t surfaces = 0, lightmappedCharts = 0, vertexSamples = 0;
	uint64_t staticCharts = 0, styledCharts = 0, unusedChannels = 0;
	uint64_t styles[256] = {}, vertexStyles[256] = {};
	uint64_t emissiveSurfaces = 0, skySurfaces = 0, cutoutSurfaces = 0, translucentSurfaces = 0;
	uint64_t unlitSurfaces = 0, movingSurfaces = 0, animatedSurfaces = 0, excludedSurfaces = 0;
	uint64_t selectedLightingImages = 0, alternativeImages = 0, missingImages = 0;
	uint64_t pairedDeluxeImages = 0, unavailableDeluxeImages = 0;
	bool luxelsScanned = false;
	uint64_t texels = 0, validLuxels = 0, fitCandidates = 0, styledLuxels = 0, excludedLuxels = 0;
	uint64_t deluxeAvailable = 0, deluxeUnavailable = 0;
	uint64_t exclusionReasons[kObservationReasonCount] =
		{}; // per named ObservationReason bit; nonexclusive
};
struct ClassificationOptions
{
	bool scanLuxels = false; // explicit potentially lengthy CPU work; never implicit in a getter
	uint64_t maxTexels = 4ull * 1024 * 1024;
	size_t maxImages = 8192;
};
// First nonmissing colour representation per logical page, deterministic source
// order. Other representations are alternatives. Deluxe pixels are never RGB
// measurements; styled charts are never added to the static component.
Error SummarizeObservations(std::shared_ptr<const SurfaceGeometry>,
							std::shared_ptr<const LightmapObservations>,
							const ClassificationOptions &, const ReadContext &,
							ObservationSummary &);
} // namespace Core
} // namespace Rend2LightRecon
