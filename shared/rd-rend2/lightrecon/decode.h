// Explicit photometric interpretation of original baked samples. CPU only.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "input.h"

namespace Rend2LightRecon
{
namespace Core
{
namespace Decode
{
constexpr uint32_t kVersion = 1;
enum class Encoding
{
	Rgb8,
	Rgba8,
	LinearRgb32F
};
enum class Component
{
	Lightmap,
	GridAmbient,
	GridDirected,
	Vertex
};
enum class Transfer
{
	Unknown,
	Linear,
	SRgb,
	PowerGamma
};
enum class Primaries
{
	Unknown,
	Rec709D65
};
enum class HdrOrigin
{
	Unknown,
	RadianceRgbE,
	LightgridRawFloat32,
	VertexRawFloat32
};
enum class Quantization
{
	Unknown,
	RoundNearest,
	Truncate
};
enum class Knowledge
{
	Unknown,
	Documented,
	Assumed
};
enum class State
{
	Unavailable,
	Available,
	Invalid
};
enum class Space
{
	EncodedRelative,
	LinearSourceRelative,
	RendererEncoded,
	RendererLinear,
	SceneLinearEquivalent
};
enum class AbsoluteScale
{
	Unknown,
	Invalid
}; // this baked-input decoder never establishes measured lux/cd/nits
enum class Reason
{
	None,
	Missing,
	UnknownTransfer,
	UnknownPrimaries,
	NoCalibration,
	NoRendererSettings,
	Unsupported,
	Invalid
};
enum ChannelFlag : uint32_t
{
	Quantized = 1u << 0,
	Black = 1u << 1,
	NearZero = 1u << 2,
	Saturated = 1u << 3,
	NearSaturated = 1u << 4,
	Missing = 1u << 5,
	NonFinite = 1u << 6,
	Negative = 1u << 7,
	HdrPrecisionUnknown = 1u << 8
};
enum Flag : uint32_t
{
	UnknownAbsoluteScale = 1u << 0,
	UnknownBakeOverbright = 1u << 1,
	UnknownExposure = 1u << 2,
	TransferAssumed = 1u << 3,
	PrimariesUnknown = 1u << 4,
	PrimariesAssumed = 1u << 5,
	MagnitudeHistoryUnknown = 1u << 6,
	FullySaturated = 1u << 7,
	RendererNormalized = 1u << 8,
	RendererQuantized = 1u << 9,
	RendererBlackFloor = 1u << 10,
	RendererDimGrayscale = 1u << 11,
	ExtendedSRgb = 1u << 12,
	HalfRangeExceeded = 1u << 13,
	ExposureAssumed = 1u << 14,
	BakeOverbrightAssumed = 1u << 15
};
struct Raw
{
	Encoding encoding = Encoding::Rgb8;
	Component component = Component::Lightmap;
	HdrOrigin hdrOrigin = HdrOrigin::Unknown;
	bool missing = false, directionData = false;
	uint8_t bytes[4] = {0, 0, 0, 255}; // alpha retained, never used as brightness
	Vec3 hdr; // exact decoded float representation, including invalid bits for diagnostics
};
struct SourcePolicy
{
	Transfer documentedTransfer = Transfer::Unknown;
	Transfer assumedTransfer =
		Transfer::Unknown; // explicit alternative when original bake TF is unknown
	float gamma = 2.2f;	   // only PowerGamma: linear = encoded^gamma
	Primaries primaries = Primaries::Unknown;
	bool assumeRec709 = false;
	std::string transferProvenance, assumptionRationale;
	// Explicit monotone gains, never inferred from current renderer cvars or bake bits.
	float encodedGain = 1.0f, linearGain = 1.0f;
	Knowledge bakeOverbright = Knowledge::Unknown;
	int bakeOverbrightBits = 0; // context ONLY; does not automatically choose gain/domain
	Knowledge exposure = Knowledge::Unknown;
	float preExposureStops = 0.0f;		// if known/assumed, undo 2^stops AFTER transfer
	bool magnitudeHistoryKnown = false; // false: unknown prior clipping/colour normalization
	Quantization quantization = Quantization::Unknown;
	uint8_t nearZeroCode = 1, nearSaturatedCode = 254;
	float hdrNearZero = 1.0e-6f;
};
struct RendererSettings
{
	bool known = false; // unavailable unless caller provides an actual controlled snapshot
	bool linearLighting = false, physicalPbr = false, hdr = false, toneMap = false;
	bool floatLightmaps = false; // EFFECTIVE glRefConfig.floatLightmap; requires hdr
	int mapOverbrightBits = 0, requestedOverbrightBits = 0, effectiveOverbrightBits = 0;
	bool hdrLightmapSeen = false; // disables forced LDR linearization after HDR discovery
	// Recorded context, NOT applied to pre-display samples by this API.
	float displayGamma = 1.0f, cameraExposure = 0.0f, exposureCompensation = 0.0f;
	bool autoExposure = false, exposureKnown = false;
	int lightmapDebug = 0; // intensity false-colour mode 2 is explicitly unsupported
};
struct SceneCalibration
{
	bool enabled = false;
	float relativeToScene =
		1.0f; // supplied gain for the stated reference/presentation; never inferred
	std::string reference, method;
	Id provenance = 0;
	Units presentation; // geometry scale and physicalToScene CONTEXT, not historical brightness
	PhotometricUnit assertedUnit =
		PhotometricUnit::Relative; // other units are rejected, not certified
};
struct Profile
{
	uint32_t version = kVersion;
	std::string identity, sourceProvenance;
	// Nonempty only when the host explicitly attests a common relative scale.
	// Equal filenames/map hashes alone do not establish this relationship.
	std::string relativeDomain;
	SourcePolicy source;
	RendererSettings renderer;
	SceneCalibration calibration;
};
struct Bounds
{
	bool lowerKnown = false, upperKnown = false;
	float lower = 0, upper = 0;
};
struct Value
{
	State state = State::Unavailable;
	Reason reason = Reason::None;
	Space space = Space::EncodedRelative;
	Vec3 rgb; // available values finite/nonnegative; zero slots in unavailable/invalid stages are
			  // NOT lighting
	Bounds bounds[3]; // quantization/censoring in THIS space, not a statistical confidence interval
	uint32_t flags = 0;
	uint8_t censoredChannels = 0; // b=255 -> lower-bound constraint, even if displayed point is 1
};
struct Observation
{
	Raw raw;								 // raw input is retained even when decoding fails
	std::shared_ptr<const Profile> metadata; // immutable pinned recipe, one shared copy per decoder
	uint32_t channels[3] = {};
	uint32_t flags = UnknownAbsoluteScale;
	AbsoluteScale absoluteScale = AbsoluteScale::Unknown;
	Transfer resolvedTransfer = Transfer::Unknown;
	Primaries resolvedPrimaries = Primaries::Unknown;
	Value encoded, source, renderer, scene;
};
// One immutable profile per stream/interpretation/hypothesis. Calls have no I/O,
// cvars, engine state, GPU use or full-image allocation. Each output owns its
// raw sample and pins the recipe, so it can outlive decoder/input buffers.
class Decoder
{
  public:
	explicit Decoder(Profile profile = Profile());
	const Error &Validation() const
	{
		return validation_;
	}
	std::shared_ptr<const Profile> Metadata() const
	{
		return profile_;
	}
	// Output is ALWAYS replaced with raw+diagnostics. A failed call supplies no
	// available transformed lighting; this differs from an unchanged-output API.
	Error Run(const Raw &, Observation &) const;

  private:
	std::shared_ptr<const Profile> profile_;
	Error validation_;
};
Error ValidateProfile(const Profile &);
bool ForcedLinear(const RendererSettings &); // R_SetupLinearLighting plus HDR-discovery state
enum class CommonSpace
{
	Incomparable,
	Relative,
	SceneEquivalent
};
CommonSpace Compare(const Observation &, const Observation &);
uint32_t
CanonicalIssues(const Observation &); // aggregate only; channel masks remain in Observation

struct LightmapSample
{
	std::shared_ptr<const LightmapObservations> owner;
	size_t image = 0;
	uint64_t pixel = 0;
	Observation value;
};
// Original logical identity/source/map hash remain accessible through owner.
// Bounded 3/4-value read only; deluxe data explicitly reports Unavailable.
Error Lightmap(const Decoder &, std::shared_ptr<const LightmapObservations>, size_t image,
			   uint64_t pixel, const ReadContext &, LightmapSample &);
} // namespace Decode
} // namespace Core
} // namespace Rend2LightRecon
