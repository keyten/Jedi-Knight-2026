// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "decode.h"
#include "lightmaps.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace Rend2LightRecon
{
namespace Core
{
namespace Decode
{
namespace
{
float Get(const Vec3 &v, unsigned i)
{
	return i == 0 ? v.x : i == 1 ? v.y : v.z;
}
void Put(Vec3 &v, unsigned i, float x)
{
	if (!i)
		v.x = x;
	else if (i == 1)
		v.y = x;
	else
		v.z = x;
}
bool Positive(float x)
{
	return std::isfinite(x) && x > 0;
}
template <typename T> bool Enum(T v, T last)
{
	return static_cast<unsigned>(v) <= static_cast<unsigned>(last);
}
void Unavailable(Value &value, Space space, Reason reason)
{
	value = Value();
	value.space = space;
	value.reason = reason;
}
void Invalidate(Observation &out)
{
	for (Value *value : {&out.encoded, &out.source, &out.renderer, &out.scene})
	{
		*value = Value();
		value->state = State::Invalid;
		value->reason = Reason::Invalid;
	}
}
bool Store(double x, float &out)
{
	if (!std::isfinite(x) || x < 0 || x > std::numeric_limits<float>::max())
		return false;
	out = static_cast<float>(x);
	return x == 0 || out != 0; // reject loss to zero rather than silently altering colour ratios
}
double Linear(double x, Transfer transfer, float gamma)
{
	switch (transfer)
	{
	case Transfer::Linear:
		return x;
	case Transfer::SRgb:
		return x <= 0.04045 ? x / 12.92 : std::pow((x + 0.055) / 1.055, 2.4);
	case Transfer::PowerGamma:
		return std::pow(x, gamma);
	case Transfer::Unknown:
		return 0; // never evaluated for an unknown transfer
	}
	return 0;
}
// Matches the renderer macro's float arithmetic before its double pow call.
float RendererSRgb(float x)
{
	return x <= 0.04045f ? x / 12.92f : static_cast<float>(std::pow((x + 0.055f) / 1.055f, 2.4));
}
Bounds QuantizedBounds(uint8_t code, Quantization quantization)
{
	Bounds bounds;
	bounds.lowerKnown = true;
	bounds.upperKnown = code != 255;
	const double below = quantization == Quantization::RoundNearest
							 ? 0.5
							 : quantization == Quantization::Truncate ? 0.0 : 1.0;
	const double above = quantization == Quantization::RoundNearest ? 0.5 : 1.0;
	bounds.lower = static_cast<float>(std::max(0.0, (code - below) / 255.0));
	if (bounds.upperKnown)
		bounds.upper = static_cast<float>(std::min(1.0, (code + above) / 255.0));
	return bounds;
}
bool SourceValue(double encoded, const SourcePolicy &policy, Transfer transfer, float &result)
{
	const double exposure =
		policy.exposure == Knowledge::Unknown ? 1.0 : std::exp2(-double(policy.preExposureStops));
	return Store(Linear(encoded * policy.encodedGain, transfer, policy.gamma) * policy.linearGain *
					 exposure,
				 result);
}
bool SourceStage(const Raw &raw, const Profile &p, Observation &out)
{
	Transfer transfer = p.source.documentedTransfer;
	if (raw.encoding == Encoding::LinearRgb32F)
	{
		// Already linear by representation. Never apply a second transfer decode.
		if ((transfer != Transfer::Unknown && transfer != Transfer::Linear) ||
			(p.source.assumedTransfer != Transfer::Unknown &&
			 p.source.assumedTransfer != Transfer::Linear))
			return false;
		transfer = Transfer::Linear;
	}
	else if (transfer == Transfer::Unknown)
	{
		transfer = p.source.assumedTransfer;
		if (transfer != Transfer::Unknown)
			out.flags |= TransferAssumed;
	}
	out.resolvedTransfer = transfer;
	out.resolvedPrimaries = p.source.primaries;
	if (out.resolvedPrimaries == Primaries::Unknown && p.source.assumeRec709)
	{
		out.resolvedPrimaries = Primaries::Rec709D65;
		out.flags |= PrimariesAssumed;
	}
	if (out.resolvedPrimaries == Primaries::Unknown)
		out.flags |= PrimariesUnknown;
	if (transfer == Transfer::Unknown)
	{
		Unavailable(out.source, Space::LinearSourceRelative, Reason::UnknownTransfer);
		return true;
	}
	out.source.state = State::Available;
	out.source.space = Space::LinearSourceRelative;
	out.source.censoredChannels = out.encoded.censoredChannels;
	for (unsigned c = 0; c < 3; ++c)
	{
		float result;
		if (!SourceValue(Get(out.encoded.rgb, c), p.source, transfer, result))
			return false;
		Put(out.source.rgb, c, result);
		const Bounds &in = out.encoded.bounds[c];
		Bounds &bound = out.source.bounds[c];
		bound.lowerKnown = in.lowerKnown;
		bound.upperKnown = in.upperKnown;
		if (in.lowerKnown && !SourceValue(in.lower, p.source, transfer, bound.lower))
			return false;
		if (in.upperKnown && !SourceValue(in.upper, p.source, transfer, bound.upper))
			return false;
		if (transfer == Transfer::SRgb && Get(out.encoded.rgb, c) * p.source.encodedGain > 1)
			out.source.flags |= ExtendedSRgb;
	}
	return true;
}
bool RendererStage(const Raw &raw, const RendererSettings &p, Value &out)
{
	if (!p.known)
	{
		Unavailable(out, Space::RendererEncoded, Reason::NoRendererSettings);
		return true;
	}
	const bool hdr = raw.encoding == Encoding::LinearRgb32F;
	const bool map = raw.component == Component::Lightmap;
	if ((map && (p.lightmapDebug == 2 || (hdr && !p.floatLightmaps))) || (hdr && !p.hdr))
	{
		Unavailable(out, Space::RendererEncoded, Reason::Unsupported);
		return true;
	}
	const bool linearize = map && !hdr && ForcedLinear(p);
	out.state = State::Available;
	out.space = hdr || linearize ? Space::RendererLinear : Space::RendererEncoded;
	float rgb[3] = {};
	if (hdr)
	{
		for (unsigned c = 0; c < 3; ++c)
			rgb[c] = Get(raw.hdr, c); // no OB scaling for HDR lightmap/grid
	}
	else if (map && p.floatLightmaps)
	{
		for (unsigned c = 0; c < 3; ++c)
		{
			rgb[c] = std::max(float(raw.bytes[c]), 0.499f);
			if (!raw.bytes[c])
				out.flags |= RendererBlackFloor;
		}
		if (rgb[0] + rgb[1] + rgb[2] < 12.0f)
		{
			const float average = (rgb[0] + rgb[1] + rgb[2]) * 0.3333f;
			for (float &channel : rgb)
				channel = average;
			out.flags |= RendererDimGrayscale;
		}
		float scale = 1.0f / 255.0f;
		scale = static_cast<float>(scale *
								   std::pow(2.0f, p.mapOverbrightBits - p.effectiveOverbrightBits));
		for (float &channel : rgb)
			channel *= scale;
	}
	else
	{
		const int shift = std::max(0, p.mapOverbrightBits - p.effectiveOverbrightBits);
		int code[3] = {raw.bytes[0] << shift, raw.bytes[1] << shift, raw.bytes[2] << shift};
		const int high = std::max(code[0], std::max(code[1], code[2]));
		if (high > 255)
		{
			for (int &channel : code)
				channel = channel * 255 / high;
			out.flags |= RendererNormalized;
		}
		for (unsigned c = 0; c < 3; ++c)
			rgb[c] = static_cast<float>(code[c]) / 255.0f;
		out.flags |= RendererQuantized;
	}
	for (unsigned c = 0; c < 3; ++c)
	{
		if (linearize)
		{
			if (rgb[c] > 1)
				out.flags |= ExtendedSRgb;
			rgb[c] = RendererSRgb(rgb[c]);
		}
		if (!std::isfinite(rgb[c]) || rgb[c] < 0)
			return false;
		if ((hdr || (map && p.floatLightmaps)) && rgb[c] > 65504.0f)
			out.flags |= HalfRangeExceeded;
		Put(out.rgb, c, rgb[c]);
	}
	// Coupled normalization/grayscale and GPU storage destroy useful source
	// inequalities. Do NOT propagate monotone source bounds into this stage.
	return true;
}
bool SceneStage(const Profile &p, Observation &out)
{
	if (!p.calibration.enabled)
	{
		Unavailable(out.scene, Space::SceneLinearEquivalent, Reason::NoCalibration);
		return true;
	}
	if (out.source.state != State::Available)
	{
		Unavailable(out.scene, Space::SceneLinearEquivalent, out.source.reason);
		return true;
	}
	if (out.resolvedPrimaries != Primaries::Rec709D65)
	{
		Unavailable(out.scene, Space::SceneLinearEquivalent, Reason::UnknownPrimaries);
		return true;
	}
	out.scene = out.source;
	out.scene.space = Space::SceneLinearEquivalent;
	for (unsigned c = 0; c < 3; ++c)
	{
		float value;
		if (!Store(double(Get(out.source.rgb, c)) * p.calibration.relativeToScene, value))
			return false;
		Put(out.scene.rgb, c, value);
		Bounds &bounds = out.scene.bounds[c];
		if (bounds.lowerKnown &&
			!Store(double(bounds.lower) * p.calibration.relativeToScene, bounds.lower))
			return false;
		if (bounds.upperKnown &&
			!Store(double(bounds.upper) * p.calibration.relativeToScene, bounds.upper))
			return false;
	}
	return true;
}
} // namespace

Error ValidateProfile(const Profile &p)
{
	if (p.version != kVersion)
		return Failure(Code::UnsupportedSchema, "unsupported observation decoder version");
	for (const auto *s :
		 {&p.identity, &p.sourceProvenance, &p.relativeDomain, &p.source.transferProvenance,
		  &p.source.assumptionRationale, &p.calibration.reference, &p.calibration.method})
		if (s->size() > 4096)
			return Failure(Code::MemoryLimit, "observation metadata string exceeds limit");
	const auto &s = p.source;
	const auto &r = p.renderer;
	const auto &c = p.calibration;
	if (!Enum(s.documentedTransfer, Transfer::PowerGamma) ||
		!Enum(s.assumedTransfer, Transfer::PowerGamma) ||
		!Enum(s.primaries, Primaries::Rec709D65) || !Enum(s.bakeOverbright, Knowledge::Assumed) ||
		!Enum(s.exposure, Knowledge::Assumed) || !Enum(s.quantization, Quantization::Truncate) ||
		!Positive(s.gamma) || s.gamma > 8 || !Positive(s.encodedGain) || !Positive(s.linearGain) ||
		!std::isfinite(s.preExposureStops) || std::abs(s.preExposureStops) > 128 ||
		!std::isfinite(s.hdrNearZero) || s.hdrNearZero < 0 || s.nearZeroCode >= s.nearSaturatedCode)
		return Failure(Code::InvalidInput, "invalid source observation policy");
	if ((s.documentedTransfer != Transfer::Unknown &&
		 (s.assumedTransfer != Transfer::Unknown || s.transferProvenance.empty())) ||
		((s.assumedTransfer != Transfer::Unknown || s.assumeRec709 ||
		  s.bakeOverbright == Knowledge::Assumed || s.exposure == Knowledge::Assumed) &&
		 s.assumptionRationale.empty()))
		return Failure(Code::InvalidInput,
					   "observation transfer/assumption needs explicit unambiguous provenance");
	if (!Positive(r.displayGamma) || !std::isfinite(r.cameraExposure) ||
		!std::isfinite(r.exposureCompensation) || r.mapOverbrightBits < -16 ||
		r.mapOverbrightBits > 15 || r.effectiveOverbrightBits < 0 ||
		r.effectiveOverbrightBits > 2 || (r.floatLightmaps && !r.hdr))
		return Failure(Code::InvalidInput, "invalid/unsafe renderer-compatible settings");
	if (!Positive(c.relativeToScene) || !Positive(c.presentation.worldUnitsPerMeter) ||
		!Positive(c.presentation.physicalToScene) || c.assertedUnit != PhotometricUnit::Relative ||
		(c.enabled && (c.reference.empty() || c.method.empty() || !c.provenance)))
		return Failure(Code::InvalidInput,
					   "baked scene-equivalent calibration needs a reference, provenance and "
					   "finite positive gain; no measured physical unit is certified");
	return Error();
}
bool ForcedLinear(const RendererSettings &r)
{
	return (r.linearLighting || r.physicalPbr) && r.hdr && r.toneMap &&
		   !r.effectiveOverbrightBits && !r.hdrLightmapSeen;
}
Decoder::Decoder(Profile profile)
	: profile_(std::make_shared<const Profile>(std::move(profile))),
	  validation_(ValidateProfile(*profile_))
{
}
Error Decoder::Run(const Raw &input, Observation &out) const
{
	const Raw raw = input; // also allow Run(out.raw, out) without destroying the source
	out = Observation();
	out.raw = raw;
	out.metadata = profile_;
	const auto &p = *profile_;
	if (p.source.bakeOverbright == Knowledge::Unknown)
		out.flags |= UnknownBakeOverbright;
	if (p.source.exposure == Knowledge::Unknown)
		out.flags |= UnknownExposure;
	if (p.source.exposure == Knowledge::Assumed)
		out.flags |= ExposureAssumed;
	if (p.source.bakeOverbright == Knowledge::Assumed)
		out.flags |= BakeOverbrightAssumed;
	if (!p.source.magnitudeHistoryKnown)
		out.flags |= MagnitudeHistoryUnknown;
	bool invalid = !Enum(raw.encoding, Encoding::LinearRgb32F) ||
				   !Enum(raw.component, Component::GridDirected) ||
				   !Enum(raw.hdrOrigin, HdrOrigin::LightgridRawFloat32);
	for (unsigned c = 0; c < 3; ++c)
	{
		if (raw.missing)
			out.channels[c] |= Missing;
		else if (raw.encoding == Encoding::LinearRgb32F)
		{
			const float x = Get(raw.hdr, c);
			out.channels[c] |= HdrPrecisionUnknown;
			if (raw.hdrOrigin == HdrOrigin::RadianceRgbE)
				out.channels[c] |= Quantized;
			if (!std::isfinite(x))
			{
				out.channels[c] |= NonFinite;
				invalid = true;
			}
			else if (x < 0)
			{
				out.channels[c] |= Negative;
				invalid = true;
			}
			else
			{
				if (!x)
					out.channels[c] |= Black;
				if (x <= p.source.hdrNearZero)
					out.channels[c] |= NearZero;
			}
		}
		else
		{
			out.channels[c] |= Quantized;
			if (!raw.bytes[c])
				out.channels[c] |= Black;
			if (raw.bytes[c] <= p.source.nearZeroCode)
				out.channels[c] |= NearZero;
			if (raw.bytes[c] == 255)
				out.channels[c] |= Saturated;
			if (raw.bytes[c] >= p.source.nearSaturatedCode)
				out.channels[c] |= NearSaturated;
		}
	}
	if (!validation_)
	{
		Invalidate(out);
		out.absoluteScale = AbsoluteScale::Invalid;
		return validation_;
	}
	if (invalid)
	{
		Invalidate(out);
		return Failure(Code::InvalidInput,
					   "invalid observation encoding or negative/non-finite HDR sample");
	}
	if (raw.missing || raw.directionData)
	{
		const Reason reason = raw.missing ? Reason::Missing : Reason::Unsupported;
		for (Value *value : {&out.encoded, &out.source, &out.renderer, &out.scene})
			value->reason = reason;
		return Failure(Code::Unavailable,
					   raw.missing ? "missing baked sample"
								   : "deluxe directions are not photometric observations");
	}
	out.encoded.state = State::Available;
	out.encoded.space = raw.encoding == Encoding::LinearRgb32F ? Space::LinearSourceRelative
															   : Space::EncodedRelative;
	for (unsigned c = 0; c < 3; ++c)
	{
		Put(out.encoded.rgb, c,
			raw.encoding == Encoding::LinearRgb32F ? Get(raw.hdr, c) : raw.bytes[c] / 255.0f);
		if (raw.encoding != Encoding::LinearRgb32F)
			out.encoded.bounds[c] = QuantizedBounds(raw.bytes[c], p.source.quantization);
		if (out.channels[c] & Saturated)
			out.encoded.censoredChannels |= uint8_t(1u << c);
	}
	if (out.encoded.censoredChannels == 7)
		out.flags |= FullySaturated;
	if (!SourceStage(raw, p, out))
	{
		Invalidate(out);
		return Failure(
			Code::InvalidInput,
			"source transfer conflicts with linear HDR, or source RGB overflow/underflow",
			"decode.source");
	}
	if (!RendererStage(raw, p.renderer, out.renderer))
	{
		Invalidate(out);
		return Failure(Code::InvalidInput,
					   "renderer-compatible RGB is outside finite nonnegative range",
					   "decode.renderer");
	}
	if (!SceneStage(p, out))
	{
		Invalidate(out);
		return Failure(Code::InvalidInput, "scene-equivalent calibration RGB overflow/underflow",
					   "decode.scene");
	}
	return Error();
}
CommonSpace Compare(const Observation &a, const Observation &b)
{
	if (!a.metadata || !b.metadata)
		return CommonSpace::Incomparable;
	if (a.scene.state == State::Available && b.scene.state == State::Available &&
		a.metadata->calibration.reference == b.metadata->calibration.reference &&
		a.metadata->calibration.presentation.physicalToScene ==
			b.metadata->calibration.presentation.physicalToScene)
		return CommonSpace::SceneEquivalent;
	if (a.source.state == State::Available && b.source.state == State::Available &&
		!a.metadata->relativeDomain.empty() &&
		a.metadata->relativeDomain == b.metadata->relativeDomain &&
		a.resolvedPrimaries == b.resolvedPrimaries)
		return CommonSpace::Relative;
	return CommonSpace::Incomparable;
}
uint32_t CanonicalIssues(const Observation &o)
{
	uint32_t flags = 0;
	for (uint32_t channel : o.channels)
	{
		if (channel & Saturated)
			flags |= ObservationClipped;
		if (channel & Missing)
			flags |= ObservationMissing;
		if (channel & Quantized)
			flags |= ObservationQuantized;
		if (channel & (NonFinite | Negative))
			flags |= ObservationUnreliable;
	}
	if ((o.flags & (TransferAssumed | PrimariesAssumed | ExposureAssumed | BakeOverbrightAssumed |
					MagnitudeHistoryUnknown)) ||
		o.source.state != State::Available)
		flags |= ObservationUnreliable;
	return flags;
}
Error Lightmap(const Decoder &decoder, std::shared_ptr<const LightmapObservations> owner,
			   size_t image, uint64_t pixel, const ReadContext &context, LightmapSample &out)
{
	out = LightmapSample();
	out.owner = std::move(owner);
	out.image = image;
	out.pixel = pixel;
	if (!out.owner || image >= out.owner->Images().size())
		return Failure(Code::InvalidInput, "missing lightmap lease/image");
	const auto &source = out.owner->Images()[image];
	Raw raw;
	raw.missing = source.missing;
	raw.directionData = source.role == LightmapRole::DeluxeDirection;
	raw.encoding = source.encoding == LightmapEncoding::BspRgb8
					   ? Encoding::Rgb8
					   : source.encoding == LightmapEncoding::ExternalRgba8
							 ? Encoding::Rgba8
							 : Encoding::LinearRgb32F;
	if (raw.encoding == Encoding::LinearRgb32F)
		raw.hdrOrigin = HdrOrigin::RadianceRgbE;
	if (raw.missing)
		return decoder.Run(raw, out.value);
	if (pixel >= uint64_t(source.width) * source.height)
		return Failure(Code::InvalidInput, "lightmap pixel out of range");
	if (raw.encoding == Encoding::LinearRgb32F)
	{
		Chunk<float> values;
		Error error = out.owner->ReadFloats(image, pixel * 3, 3, context, values);
		if (!error)
			return error;
		raw.hdr = Vec3(values.data[0], values.data[1], values.data[2]);
	}
	else
	{
		Chunk<uint8_t> values;
		Error error =
			out.owner->ReadBytes(image, pixel * source.channels, source.channels, context, values);
		if (!error)
			return error;
		for (size_t i = 0; i < values.count; ++i)
			raw.bytes[i] = values.data[i];
	}
	return decoder.Run(raw, out.value);
}
} // namespace Decode
} // namespace Core
} // namespace Rend2LightRecon
