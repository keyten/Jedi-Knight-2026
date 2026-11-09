// ASR12: tool-only interpretation of ORIGINAL BSP samples, never P02 probes.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lightrecon/regions.h"
#include <array>

namespace OriginalGridEvidence
{
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
enum class Compression { Unknown, Q3map2Merge4 };
enum class ColourSpace { EncodedRgb8, ProfileSourceRelative };
enum class Edge { Unsupported, Clear, Blocked, Unknown, Dynamic };
struct Options
{
	Compression compression = Compression::Unknown;
	ColourSpace colourSpace = ColourSpace::EncodedRgb8;
	double weakDirectedCode = 2; // diagnostic threshold, never a confidence estimate
	uint64_t maxRayWork = 65536;
};
struct Input
{
	std::shared_ptr<const LightgridObservations> grid;
	std::shared_ptr<const RegionDomain> domain;
	std::shared_ptr<const VisibilityProvider> visibility;
	std::vector<DynamicBoundary> dynamic;
	Decode::Profile profile;
};
struct Direction
{
	Vec3 consumer256, writer255; // receiver -> aggregate incoming light; Z up
	double consumerEncodingBoundRadians = 0, writerEncodingBoundRadians = 0;
	double mergeBoundRadians = 0;
	bool compressionBoundKnown = false;
	bool writer255CodePossible = false; // exact inspected writer output, NOT a compiler fingerprint
};
Direction InterpretDirection(uint8_t polar, uint8_t azimuth, Compression);
struct Interval
{
	double value = 0, lower = 0, upper = 0;
	bool available = false, bounded = false;
};
struct Colour
{
	Interval rgb[3], intensity; // arithmetic RGB mean, NOT calibrated luminance
	uint32_t decoderFlags = 0;
};
struct Gradient
{
	// Central difference only. Each axis independently supported. Units: selected colour / WU.
	Interval rgb[3][3], intensity[3]; // [axis][channel]
};
struct Style
{
	uint8_t slot = 0, id = 255;
	bool active = false, weakDirected = true;
	Colour ambient, directed;
	Gradient ambientGradient, directedGradient;
};
struct Observation
{
	GridCell raw;
	SpatialLocation location;
	Direction direction;
	std::array<Style, 4> styles;
	Colour hdrAmbient, hdrDirected; // independent dense, unstyled float alternative; relative scale
	// -x,+x,-y,+y,-z,+z. No one-sided or interpolated fallback.
	std::array<Edge, 6> edges{};
	uint64_t rayWork = 0;
	bool dynamicPossible = false, interpolationErrorKnown = false;
};
// Bounded one-cell/six-neighbour work. Provider errors propagate; unknown is never free.
Error Extract(const Input &, const Options &, uint64_t ordinal, const ReadContext &, Observation &);
const char *SpaceName(SpaceClass);
const char *EdgeName(Edge);
} // namespace OriginalGridEvidence
