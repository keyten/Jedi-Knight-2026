// Numerical observation decoding tests. No GL, OpenJK startup or game assets.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/decode.h"
#include "lightrecon/lightmaps.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>

using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace D = Rend2LightRecon::Core::Decode;
namespace Reference
{
using byte = unsigned char;
struct Cvar
{
	int integer = 0;
};
Cvar mapBits, linear, physical, hdr, tone;
Cvar *r_mapOverBrightBits = &mapBits, *r_linearLighting = &linear, *r_pbrPhysical = &physical,
	 *r_hdr = &hdr, *r_toneMap = &tone;
struct Globals
{
	int overbrightBits = 0;
	bool forcedLinearLight = false, linearLight = false, hdrLighting = false;
} tr;
struct Config
{
	bool floatLightmap = false;
} glRefConfig;
void Ignore(int, const char *, ...) {}
struct Imports
{
	void (*Printf)(int, const char *, ...) = Ignore;
} ri;
enum
{
	PRINT_WARNING = 1,
	qfalse = 0,
	qtrue = 1
};
#define Q_max(a, b) std::max(a, b)
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#ifdef _MSC_VER
// Preserve the original renderer's deliberate int-to-byte narrowing in the oracle.
#pragma warning(push)
#pragma warning(disable : 4244)
#endif
#include "decode_renderer_reference.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#undef Q_max
#undef MAX
void Setup(const D::RendererSettings &p)
{
	mapBits.integer = p.mapOverbrightBits;
	tr.overbrightBits = p.effectiveOverbrightBits;
	linear.integer = p.linearLighting;
	physical.integer = p.physicalPbr;
	hdr.integer = p.hdr;
	tone.integer = p.toneMap;
	glRefConfig.floatLightmap = p.floatLightmaps;
	R_SetupLinearLighting();
	if (p.hdrLightmapSeen)
	{
		tr.forcedLinearLight = false;
		tr.hdrLighting = tr.linearLight = true;
	}
}
Vec3 Run(const D::Raw &raw)
{
	float color[4] = {raw.hdr.x, raw.hdr.y, raw.hdr.z, 1};
	byte bytes[4];
	std::memcpy(bytes, raw.bytes, 4);
	if (raw.encoding == D::Encoding::LinearRgb32F)
	{
		if (raw.component == D::Component::Lightmap)
			R_ColorShiftLightingFloats(color, color, 1, false);
	}
	else if (raw.component == D::Component::Lightmap && glRefConfig.floatLightmap)
		LdrFloat(bytes, color);
	else
	{
		R_ColorShiftLightingBytes(bytes, bytes);
		for (unsigned c = 0; c < 3; ++c)
			color[c] = bytes[c] / 255.0f;
		if (raw.component == D::Component::Lightmap)
			R_LinearizeLightingColor(color); // nearest sRGB texel
	}
	return Vec3(color[0], color[1], color[2]);
}
} // namespace Reference
#undef sRGBtoRGB
namespace
{
int checks = 0, failures = 0;
#define CHECK(x)                                                                                   \
	do                                                                                             \
	{                                                                                              \
		++checks;                                                                                  \
		if (!(x))                                                                                  \
		{                                                                                          \
			++failures;                                                                            \
			std::printf("FAIL line %d: %s\n", __LINE__, #x);                                       \
		}                                                                                          \
	} while (0)
void Near(float a, float b, double tolerance = 2.0e-6)
{
	CHECK(std::isfinite(a) && std::isfinite(b) &&
		  std::abs(double(a) - b) <= tolerance * std::max(1.0, std::abs(double(b))));
}
float Get(const Vec3 &v, unsigned c)
{
	return !c ? v.x : c == 1 ? v.y : v.z;
}
D::Raw Ldr(unsigned r, unsigned g, unsigned b)
{
	D::Raw raw;
	raw.bytes[0] = uint8_t(r);
	raw.bytes[1] = uint8_t(g);
	raw.bytes[2] = uint8_t(b);
	return raw;
}
D::Profile Assumed()
{
	D::Profile p;
	p.identity = "test/srgb/1";
	p.source.assumedTransfer = D::Transfer::SRgb;
	p.source.assumeRec709 = true;
	p.source.assumptionRationale = "synthetic sRGB/Rec709 fixture; historical bake remains unknown";
	p.source.quantization = D::Quantization::RoundNearest;
	return p;
}
void RawAndTransfer()
{
	static_assert(std::is_const<decltype(D::Observation().metadata)::element_type>::value,
				  "immutable decoding recipes");
	D::Observation out;
	D::Decoder unknown;
	CHECK(unknown.Run(Ldr(128, 64, 32), out));
	CHECK(out.encoded.state == D::State::Available && out.source.state == D::State::Unavailable);
	CHECK(out.source.reason == D::Reason::UnknownTransfer &&
		  out.renderer.reason == D::Reason::NoRendererSettings);
	CHECK(out.absoluteScale == D::AbsoluteScale::Unknown);
	const auto p = Assumed();
	D::Decoder srgb(p);
	for (auto raw : {Ldr(0, 0, 0), Ldr(128, 128, 128), Ldr(255, 255, 255), Ldr(255, 64, 1),
					 Ldr(254, 253, 2), Ldr(10, 11, 12)})
	{
		const auto original = raw;
		CHECK(srgb.Run(raw, out));
		CHECK(std::memcmp(raw.bytes, original.bytes, 4) == 0 &&
			  std::memcmp(raw.bytes, out.raw.bytes, 4) == 0);
		CHECK(out.source.state == D::State::Available &&
			  out.scene.reason == D::Reason::NoCalibration);
		CHECK(out.flags & D::TransferAssumed);
		CHECK(out.flags & D::PrimariesAssumed);
		CHECK(out.flags & D::UnknownBakeOverbright);
		CHECK(out.flags & D::UnknownExposure);
		for (unsigned c = 0; c < 3; ++c)
		{
			const double encoded = raw.bytes[c] / 255.0;
			const double expected =
				encoded <= .04045 ? encoded / 12.92 : std::pow((encoded + .055) / 1.055, 2.4);
			Near(Get(out.source.rgb, c), float(expected), 1.0e-7);
			CHECK(out.channels[c] & D::Quantized);
			CHECK(bool(out.channels[c] & D::Saturated) == (raw.bytes[c] == 255));
			CHECK(bool(out.channels[c] & D::Black) == (raw.bytes[c] == 0));
			CHECK(out.source.bounds[c].lowerKnown &&
				  out.source.bounds[c].upperKnown == (raw.bytes[c] != 255));
			CHECK(out.source.bounds[c].lower <= Get(out.source.rgb, c));
		}
		CHECK(bool(out.flags & D::FullySaturated) ==
			  (raw.bytes[0] == 255 && raw.bytes[1] == 255 && raw.bytes[2] == 255));
	}
	CHECK(srgb.Run(Ldr(128, 128, 128), out));
	Near(out.source.rgb.x, .2158605f, 1e-6);
	CHECK(srgb.Run(Ldr(255, 64, 1), out));
	CHECK(out.source.censoredChannels == 1);
	CHECK(D::CanonicalIssues(out) & ObservationClipped);
	CHECK(out.source.bounds[0].lower < 1 && !out.source.bounds[0].upperKnown);
	CHECK(srgb.Run(Ldr(254, 0, 0), out));
	CHECK(out.channels[0] & D::NearSaturated);
	CHECK(!(out.channels[0] & D::Saturated));
	CHECK(!(out.source.flags & D::RendererDimGrayscale));
	CHECK(out.source.rgb.x != out.source.rgb.y);
	D::Profile linear = p;
	linear.source.assumedTransfer = D::Transfer::Linear;
	CHECK(D::Decoder(linear).Run(Ldr(128, 64, 32), out));
	Near(out.source.rgb.x / out.source.rgb.y, 2);
	D::Profile gamma = p;
	gamma.source.assumedTransfer = D::Transfer::PowerGamma;
	gamma.source.gamma = 2;
	CHECK(D::Decoder(gamma).Run(Ldr(128, 64, 32), out));
	Near(out.source.rgb.x / out.source.rgb.y, 4);
	D::Profile known = p;
	known.source.assumedTransfer = D::Transfer::Unknown;
	known.source.documentedTransfer = D::Transfer::SRgb;
	known.source.transferProvenance = "fixture exporter";
	CHECK(D::Decoder(known).Run(Ldr(128, 128, 128), out));
	CHECK(!(out.flags & D::TransferAssumed));
	known.source.encodedGain = 2;
	known.source.linearGain = 3;
	known.source.exposure = D::Knowledge::Documented;
	known.source.preExposureStops = 1;
	CHECK(D::Decoder(known).Run(Ldr(128, 128, 128), out));
	Near(out.source.rgb.x, float(std::pow((2.0 * 128 / 255 + .055) / 1.055, 2.4) * 1.5));
	CHECK(out.source.flags & D::ExtendedSRgb);
	CHECK(!(out.flags & D::UnknownExposure));
	const auto beforeAlias = out;
	CHECK(D::Decoder(known).Run(out.raw, out));
	CHECK(out.raw.bytes[0] == beforeAlias.raw.bytes[0]);
	CHECK(out.source.rgb.x == beforeAlias.source.rgb.x);
	known.source.exposure = D::Knowledge::Assumed;
	known.source.bakeOverbright = D::Knowledge::Assumed;
	CHECK(D::Decoder(known).Run(Ldr(128, 128, 128), out));
	CHECK(out.flags & D::ExposureAssumed);
	CHECK(out.flags & D::BakeOverbrightAssumed);
	known.source.bakeOverbright = D::Knowledge::Documented;
	known.source.bakeOverbrightBits = 32; // informational bake metadata is NOT a renderer shift
	const auto beforeBakeMetadata = out.source.rgb;
	CHECK(D::Decoder(known).Run(Ldr(128, 128, 128), out));
	CHECK(out.source.rgb.x == beforeBakeMetadata.x);
	CHECK(out.metadata->source.bakeOverbrightBits == 32 && !(out.flags & D::UnknownBakeOverbright));
	// Quantizer assumptions affect bounds, never the preserved code or point value.
	for (auto quantizer :
		 {D::Quantization::Unknown, D::Quantization::RoundNearest, D::Quantization::Truncate})
	{
		auto boundsProfile = Assumed();
		boundsProfile.source.quantization = quantizer;
		CHECK(D::Decoder(boundsProfile).Run(Ldr(0, 128, 255), out));
		const float radius = quantizer == D::Quantization::RoundNearest ? .5f : 1.0f;
		Near(out.encoded.bounds[0].upper, radius / 255);
		const float below = quantizer == D::Quantization::Truncate ? 0 : radius;
		Near(out.encoded.bounds[1].lower, (128 - below) / 255);
		CHECK(!out.encoded.bounds[2].upperKnown);
	}
	D::Raw hdr;
	hdr.encoding = D::Encoding::LinearRgb32F;
	hdr.hdrOrigin = D::HdrOrigin::RadianceRgbE;
	hdr.hdr = Vec3(4, 1024, 65536);
	CHECK(unknown.Run(hdr, out));
	Near(out.source.rgb.y, 1024);
	CHECK(out.channels[0] & D::HdrPrecisionUnknown);
	CHECK(out.channels[0] & D::Quantized);
	CHECK(!out.source.bounds[0].lowerKnown);
	CHECK(!(out.channels[0] & D::Saturated));
	CHECK(!(out.flags & D::TransferAssumed));
	// Explicit sRGB policy on already-linear HDR is rejected, rather than decoded twice.
	CHECK(srgb.Run(hdr, out).code == Code::InvalidInput);
	CHECK(out.source.state == D::State::Invalid);
}
void RendererParity()
{
	D::Observation result;
	for (bool floatMap : {false, true})
		for (bool linear : {false, true})
			for (bool physical : {false, true})
				for (bool tone : {false, true})
					for (int effective : {0, 1, 2})
						for (int mapBits : {-2, 0, 1, 2, 8, 15})
							for (bool hdrSeen : {false, true})
							{
								D::Profile profile = Assumed();
								auto &r = profile.renderer;
								r.known = true;
								r.hdr = true;
								r.toneMap = tone;
								r.floatLightmaps = floatMap;
								r.linearLighting = linear;
								r.physicalPbr = physical;
								r.effectiveOverbrightBits = effective;
								r.requestedOverbrightBits = effective;
								r.mapOverbrightBits = mapBits;
								r.hdrLightmapSeen = hdrSeen;
								D::Decoder decoder(profile);
								CHECK(decoder.Validation());
								Reference::Setup(r);
								CHECK(D::ForcedLinear(r) == Reference::tr.forcedLinearLight);
								for (auto raw :
									 {Ldr(0, 0, 0), Ldr(1, 2, 3), Ldr(3, 4, 5), Ldr(128, 64, 32),
									  Ldr(255, 128, 17), Ldr(254, 253, 255)})
								{
									for (auto domain :
										 {D::Component::Lightmap, D::Component::GridAmbient,
										  D::Component::GridDirected})
									{
										raw.component = domain;
										CHECK(decoder.Run(raw, result));
										CHECK(result.renderer.state == D::State::Available);
										const Vec3 expected = Reference::Run(raw);
										for (unsigned c = 0; c < 3; ++c)
											Near(Get(result.renderer.rgb, c), Get(expected, c));
										CHECK(result.source.state ==
											  D::State::Available); // independently decoded,
																	// without render hacks
									}
								}
							}
	D::Profile p = Assumed();
	p.renderer.known = p.renderer.hdr = p.renderer.floatLightmaps = true;
	p.renderer.linearLighting = p.renderer.toneMap = true;
	CHECK(D::Decoder(p).Run(Ldr(0, 0, 0), result));
	CHECK(result.source.rgb.x == 0 && result.renderer.rgb.x > 0);
	CHECK(result.renderer.flags & D::RendererBlackFloor);
	CHECK(result.renderer.flags & D::RendererDimGrayscale);
	p.renderer.mapOverbrightBits = 2;
	CHECK(D::Decoder(p).Run(Ldr(255, 64, 32), result));
	CHECK(result.renderer.rgb.x > 1 && result.source.rgb.x == 1);
	p.renderer.floatLightmaps = false;
	CHECK(D::Decoder(p).Run(Ldr(255, 64, 32), result));
	CHECK(result.renderer.flags & D::RendererNormalized);
	CHECK(!result.renderer.bounds[0].lowerKnown); // no fake reversal of normalization
	p.renderer.hdr = false;
	p.renderer.effectiveOverbrightBits = 0;
	CHECK(D::Decoder(p).Run(Ldr(128, 64, 32), result));
	CHECK(!D::ForcedLinear(p.renderer));
	CHECK(result.renderer.space == D::Space::RendererEncoded);
	p.source.assumedTransfer = D::Transfer::Linear;
	p.renderer.hdr = true;
	p.renderer.linearLighting = p.renderer.toneMap = true;
	p.renderer.mapOverbrightBits = 0;
	CHECK(D::Decoder(p).Run(Ldr(128, 128, 128), result));
	Near(result.source.rgb.x, 128.0f / 255);
	Near(result.renderer.rgb.x, .2158605f);
	const auto beforeDisplay = result;
	p.renderer.displayGamma = 3;
	p.renderer.autoExposure = p.renderer.exposureKnown = true;
	p.renderer.cameraExposure = 8;
	p.renderer.exposureCompensation = -4;
	CHECK(D::Decoder(p).Run(Ldr(128, 128, 128), result));
	Near(result.source.rgb.x, beforeDisplay.source.rgb.x);
	Near(result.renderer.rgb.x, beforeDisplay.renderer.rgb.x);
	p.renderer.lightmapDebug = 2;
	CHECK(D::Decoder(p).Run(Ldr(128, 64, 32), result));
	CHECK(result.renderer.reason == D::Reason::Unsupported);
	// HDR lightmap bypasses overbright/gamma; raw HDR grid needs r_hdr but not float lightmaps.
	D::Raw raw;
	raw.encoding = D::Encoding::LinearRgb32F;
	raw.hdr = Vec3(2, 16, 65536);
	D::Profile h;
	h.renderer.known = h.renderer.hdr = h.renderer.floatLightmaps = true;
	h.renderer.mapOverbrightBits = 15;
	CHECK(D::Decoder(h).Run(raw, result));
	Near(result.renderer.rgb.z, 65536);
	CHECK(result.renderer.flags & D::HalfRangeExceeded);
	h.renderer.floatLightmaps = false;
	raw.component = D::Component::GridAmbient;
	raw.hdrOrigin = D::HdrOrigin::LightgridRawFloat32;
	CHECK(D::Decoder(h).Run(raw, result));
	Near(result.renderer.rgb.y, 16);
	CHECK(result.raw.hdrOrigin == D::HdrOrigin::LightgridRawFloat32);
	CHECK((result.channels[0] & D::HdrPrecisionUnknown) && !(result.channels[0] & D::Quantized));
}
void CalibrationAndFailures()
{
	D::Observation a, b;
	auto p = Assumed();
	CHECK(D::Decoder(p).Run(Ldr(64, 128, 254), a));
	CHECK(D::Compare(a, a) == D::CommonSpace::Incomparable);
	p.calibration.enabled = true;
	p.calibration.relativeToScene = 2;
	p.calibration.reference = "fixture/reference-1";
	p.calibration.method = "explicit scene-equivalent reference";
	p.calibration.provenance = 1;
	CHECK(D::Decoder(p).Run(Ldr(64, 128, 255), a));
	CHECK(a.scene.state == D::State::Available && a.absoluteScale == D::AbsoluteScale::Unknown);
	Near(a.scene.rgb.y, a.source.rgb.y * 2);
	CHECK(!a.scene.bounds[2].upperKnown && a.scene.censoredChannels == 4);
	auto grid = Ldr(64, 128, 255);
	grid.component = D::Component::GridAmbient;
	CHECK(D::Decoder(p).Run(grid, b));
	CHECK(D::Compare(a, b) == D::CommonSpace::SceneEquivalent);
	p.calibration.presentation.worldUnitsPerMeter = 100;
	CHECK(D::Decoder(p).Run(grid, b));
	Near(a.scene.rgb.y, b.scene.rgb.y); // geometry scale cannot reveal brightness
	p.calibration.presentation.physicalToScene = .001f;
	CHECK(D::Decoder(p).Run(grid, b));
	CHECK(D::Compare(a, b) == D::CommonSpace::Incomparable);
	Near(a.scene.rgb.y,
		 b.scene.rgb.y); // an explicit gain belongs to its stated presentation policy
	p.calibration.enabled = false;
	p.relativeDomain = "explicit common relative basis";
	CHECK(D::Decoder(p).Run(grid, b));
	CHECK(D::Decoder(p).Run(Ldr(64, 128, 254), a));
	CHECK(D::Compare(a, b) == D::CommonSpace::Relative);
	p.source.assumeRec709 = false;
	p.calibration.enabled = true;
	CHECK(D::Decoder(p).Run(grid, b));
	CHECK(b.scene.reason == D::Reason::UnknownPrimaries);
	CHECK(D::Decoder(p).Run(Ldr(64, 128, 254), a));
	// Equal Unknown enum values do not establish a shared colour basis, even
	// when the caller attests a common relative magnitude domain.
	CHECK(a.resolvedPrimaries == D::Primaries::Unknown);
	CHECK(D::Compare(a, b) == D::CommonSpace::Incomparable);
	for (unsigned mode = 0; mode < 15; ++mode)
	{
		p = Assumed();
		switch (mode)
		{
		case 0:
			p.source.gamma = 0;
			break;
		case 1:
			p.source.encodedGain = -1;
			break;
		case 2:
			p.source.linearGain = std::numeric_limits<float>::quiet_NaN();
			break;
		case 3:
			p.source.assumptionRationale.clear();
			break;
		case 4:
			p.source.documentedTransfer = D::Transfer::Linear;
			break;
		case 5:
			p.source.nearZeroCode = 255;
			break;
		case 6:
			p.renderer.mapOverbrightBits = 32;
			break;
		case 7:
			p.renderer.effectiveOverbrightBits = -1;
			break;
		case 8:
			p.renderer.floatLightmaps = true;
			break;
		case 9:
			p.calibration.enabled = true;
			break;
		case 10:
			p.calibration.assertedUnit = PhotometricUnit::Lux;
			break;
		case 11:
			p.calibration.presentation.physicalToScene = 0;
			break;
		case 12:
			p.source.preExposureStops = std::numeric_limits<float>::infinity();
			break;
		case 13:
			p.source.assumedTransfer = static_cast<D::Transfer>(99);
			break;
		case 14:
			p.source.hdrNearZero = -1;
			break;
		}
		D::Decoder decoder(p);
		CHECK(!decoder.Validation());
		CHECK(!decoder.Run(Ldr(1, 2, 3), a));
		CHECK(a.source.state == D::State::Invalid && a.absoluteScale == D::AbsoluteScale::Invalid);
		CHECK(a.raw.bytes[1] == 2 && a.source.rgb.y == 0);
	}
	p = Assumed();
	p.version = 999;
	CHECK(D::Decoder(p).Validation().code == Code::UnsupportedSchema);
	p = Assumed();
	p.identity.assign(4097, 'x');
	CHECK(D::Decoder(p).Validation().code == Code::MemoryLimit);
	D::Decoder defaults;
	for (float bad :
		 {-1.0f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
	{
		D::Raw raw;
		raw.encoding = D::Encoding::LinearRgb32F;
		raw.hdr = Vec3(1, bad, 2);
		CHECK(defaults.Run(raw, a).code == Code::InvalidInput);
		CHECK(a.source.state == D::State::Invalid);
		CHECK(a.channels[1] & (bad < 0 ? D::Negative : D::NonFinite));
		CHECK(std::memcmp(&a.raw.hdr.y, &raw.hdr.y, sizeof(float)) == 0);
	}
	D::Raw absent;
	absent.missing = true;
	CHECK(defaults.Run(absent, a).code == Code::Unavailable);
	CHECK(a.channels[0] & D::Missing);
	CHECK(a.source.reason == D::Reason::Missing);
	CHECK(D::CanonicalIssues(a) & ObservationMissing);
	p = Assumed();
	p.source.assumedTransfer = D::Transfer::PowerGamma;
	p.source.gamma = 8;
	p.source.encodedGain = std::numeric_limits<float>::max();
	CHECK(D::Decoder(p).Run(Ldr(255, 128, 0), a).code == Code::InvalidInput);
	CHECK(a.source.state == D::State::Invalid);
	p = Assumed();
	p.source.linearGain = std::numeric_limits<float>::denorm_min();
	CHECK(D::Decoder(p).Run(Ldr(1, 2, 3), a).code == Code::InvalidInput);
	CHECK(a.source.state == D::State::Invalid && a.raw.bytes[0] == 1);
	p = Assumed();
	p.source.assumedTransfer = D::Transfer::Linear;
	p.source.linearGain = 2;
	p.calibration.enabled = true;
	p.calibration.reference = "overflow fixture";
	p.calibration.method = "deliberately unrepresentable gain";
	p.calibration.provenance = 1;
	p.calibration.relativeToScene = std::numeric_limits<float>::max();
	const auto sceneOverflow = D::Decoder(p).Run(Ldr(255, 128, 0), a);
	CHECK(sceneOverflow.code == Code::InvalidInput && sceneOverflow.path == "decode.scene");
	CHECK(a.scene.state == D::State::Invalid && a.raw.bytes[0] == 255);
	D::Raw direction;
	direction.directionData = true;
	CHECK(defaults.Run(direction, a).code == Code::Unavailable);
	// Metadata is owned/frozen, not a borrowed mutable recipe.
	p = Assumed();
	D::Decoder frozen(p);
	p.source.gamma = 0;
	CHECK(frozen.Validation());
	CHECK(frozen.Run(Ldr(128, 128, 128), a));
	auto lease = a.metadata;
	CHECK(a.metadata == frozen.Metadata());
	CHECK(lease->source.gamma == 2.2f);
	const auto recipe = Assumed();
	D::Decoder repeated(recipe);
	CHECK(repeated.Run(Ldr(128, 64, 255), a));
	for (unsigned i = 0; i < 50; ++i)
	{
		CHECK(repeated.Run(Ldr(128, 64, 255), b));
		CHECK(a.source.rgb.x == b.source.rgb.x && a.source.rgb.y == b.source.rgb.y);
		CHECK(a.source.bounds[2].lower == b.source.bounds[2].lower &&
			  !b.source.bounds[2].upperKnown);
		CHECK(a.flags == b.flags && a.channels[0] == b.channels[0] && a.metadata == b.metadata);
	}
}
void ImageBoundary()
{
	LightmapCapture capture;
	capture.Begin(BuildPolicy::ForceRebuild);
	const uint8_t bsp[3] = {0, 128, 255};
	CHECK(capture.SetMap("maps/decode.bsp", bsp, 3));
	CHECK(capture.Embedded(bsp, 3, 0, 3, 1, 1, false));
	const uint8_t rgba[4] = {1, 2, 3, 17};
	CHECK(capture.Rgba8(2, LightmapRole::Lighting, "maps/decode/lm_0002.tga", 1, 1, rgba, 4));
	const float hdr[3] = {1, 4, 16};
	CHECK(capture.Rgb32F(3, "maps/decode/lm_0003.hdr", 1, 1, hdr, 3));
	CHECK(
		capture.Rgba8(4, LightmapRole::DeluxeDirection, "maps/decode/dm_0004.tga", 1, 1, rgba, 4));
	CHECK(capture.Missing(5, LightmapRole::Lighting, LightmapEncoding::ExternalRgba8,
						  "maps/decode/lm_0005.tga"));
	CHECK(capture.Freeze());
	auto set = capture.Acquire();
	D::LightmapSample out;
	ReadContext context;
	context.maxBytes = 12;
	CHECK(D::Lightmap(D::Decoder(Assumed()), set, 0, 0, context, out));
	CHECK(out.owner->Map().path == "maps/decode.bsp" && out.value.raw.bytes[2] == 255);
	CHECK(out.owner->Images()[out.image].logicalIndex == 0);
	CHECK(D::Lightmap(D::Decoder(Assumed()), set, 1, 0, context, out));
	CHECK(out.value.raw.bytes[3] == 17);
	CHECK(D::Lightmap(D::Decoder(), set, 2, 0, context, out));
	Near(out.value.source.rgb.z, 16);
	CHECK(D::Lightmap(D::Decoder(), set, 3, 0, context, out).code == Code::Unavailable);
	CHECK(D::Lightmap(D::Decoder(), set, 4, 0, context, out).code == Code::Unavailable);
	CHECK(out.value.raw.missing);
	CHECK(D::Lightmap(D::Decoder(), set, 0, UINT64_MAX, context, out).code == Code::InvalidInput);
	CHECK(D::Lightmap(D::Decoder(), set, 99, 0, context, out).code == Code::InvalidInput);
	context.maxBytes = 2;
	CHECK(D::Lightmap(D::Decoder(), set, 0, 0, context, out).code == Code::MemoryLimit);
	context.maxBytes = 12;
	CHECK(D::Lightmap(D::Decoder(Assumed()), set, 0, 0, context, out));
	capture.Reset();
	set.reset();
	CHECK(out.owner->Images()[0].bytes->at(1) == 128 && out.value.metadata->version == D::kVersion);
}
} // namespace
int main()
{
	RawAndTransfer();
	RendererParity();
	CalibrationAndFailures();
	ImageBoundary();
	std::printf("lightrecon decode: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
