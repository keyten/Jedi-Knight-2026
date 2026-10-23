// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "classification.h"
#include "surfaces.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <set>
#include <tuple>
#include <new>

namespace Rend2LightRecon
{
namespace Core
{
namespace
{
bool Finite(Vec3 v)
{
	return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
bool HasChart(const BakedSurface &s)
{
	for (size_t i = 0; i < 4; ++i)
		if (s.lightmap[i] >= 0 && s.styles[i] != 255)
			return true;
	return false;
}
bool VertexPath(const BakedSurface &s)
{
	return !HasChart(s) && (s.lightmap[0] == -3 || (s.materialFacts.signals & MaterialVertexLit));
}
bool Cancelled(const ReadContext &ctx)
{
	return ctx.cancellation && ctx.cancellation->IsCancelled();
}
uint64_t Rejections()
{
	return MissingMaterial | UnsupportedMaterial | SkyBoundary | EmissiveHintOnly |
		   AlphaCoverageUnknown | TranslucentTransport | UnlitPresentation | AnimatedMaterial |
		   MovingGeometry | DeformedGeometry | UnknownGeometryOwnership | IntentionallyExcluded |
		   MalformedGeometry | NoCoveredCentre | AmbiguousChart | UnsupportedChart |
		   MissingColourImage | InvalidSourceCombination | MultipleChartReferences |
		   ReflectionPresentation;
}
} // namespace
StyleChannel LightStyle(uint32_t slot, uint8_t id)
{
	StyleChannel s;
	s.slot = slot;
	s.id = id;
	s.status =
		id == 255 ? StyleStatus::Unused : id == 0 ? StyleStatus::Static : StyleStatus::Styled;
	return s;
}
uint32_t MaterialUncertaintyFlags(const MaterialFacts &m)
{
	auto nonnegative = [](Vec3 v) { return Finite(v) && v.x >= 0 && v.y >= 0 && v.z >= 0; };
	uint32_t flags = m.uncertainty;
	if (static_cast<unsigned>(m.cull) > static_cast<unsigned>(SurfaceCull::Back) ||
		static_cast<unsigned>(m.meaning) >
			static_cast<unsigned>(BakedMeaning::DiffuseModulationBakeUnknown) ||
		static_cast<unsigned>(m.emissionProvenance) >
			static_cast<unsigned>(EmissionProvenance::AuthoredLuminance))
		flags |= MaterialValuesInvalid;
	if ((m.baseColourKnown && (!nonnegative(m.baseColourLinear) || m.baseColourLinear.x > 1 ||
							   m.baseColourLinear.y > 1 || m.baseColourLinear.z > 1)) ||
		(m.baseModulationKnown && !nonnegative(m.baseModulation)) ||
		(m.emissionColourKnown && !nonnegative(m.emissionColour)) ||
		!std::isfinite(m.authoredLuminance) || m.authoredLuminance < 0 ||
		!std::isfinite(m.legacySurfaceLight) || m.legacySurfaceLight < 0 ||
		!nonnegative(m.legacySourceColour))
		flags |= MaterialValuesInvalid;
	return flags;
}
const char *ObservationPathName(ObservationPath p)
{
	switch (p)
	{
	case ObservationPath::StaticLightmap:
		return "static-lightmap";
	case ObservationPath::StyledLightmap:
		return "styled-lightmap";
	case ObservationPath::VertexColour:
		return "vertex-colour";
	case ObservationPath::Other:
		return "other";
	case ObservationPath::Excluded:
		return "excluded";
	}
	return "invalid";
}
const char *ObservationReasonName(uint64_t bit)
{
	switch (bit)
	{
#define REASON(x)                                                                                  \
	case x:                                                                                        \
		return #x
		REASON(RelativeBakedCandidate);
		REASON(SeparateStyledChannel);
		REASON(VertexEvidenceOnly);
		REASON(UnknownBakeAlbedo);
		REASON(UncalibratedBakedScale);
		REASON(NoLightmap);
		REASON(UnusedStyle);
		REASON(MissingMaterial);
		REASON(UnsupportedMaterial);
		REASON(SkyBoundary);
		REASON(EmissiveHintOnly);
		REASON(AlphaCoverageUnknown);
		REASON(TranslucentTransport);
		REASON(UnlitPresentation);
		REASON(AnimatedMaterial);
		REASON(MovingGeometry);
		REASON(DeformedGeometry);
		REASON(UnknownGeometryOwnership);
		REASON(IntentionallyExcluded);
		REASON(MalformedGeometry);
		REASON(NoCoveredCentre);
		REASON(AmbiguousChart);
		REASON(UnsupportedChart);
		REASON(MissingColourImage);
		REASON(InvalidSourceCombination);
		REASON(MultipleChartReferences);
		REASON(VertexPaintOrLightingUnknown);
		REASON(InvalidColourData);
		REASON(ReflectionPresentation);
#undef REASON
	}
	return "invalid-reason";
}
ObservationClassification ClassifySurface(const BakedSurface &s, uint32_t slot, bool vertex)
{
	ObservationClassification out;
	const auto &m = s.materialFacts;
	out.materialSignals = m.signals;
	out.materialUncertainty = MaterialUncertaintyFlags(m);
	out.meaning = m.meaning;
	out.reasons = UnknownBakeAlbedo | UncalibratedBakedScale;
	out.emitterHint = (m.signals & MaterialGlow) != 0 || m.legacySurfaceLight > 0 ||
					  m.emissionProvenance != EmissionProvenance::None || m.emissionLayerCount > 0;
	if (slot >= 4)
	{
		out.reasons |= InvalidSourceCombination;
		return out;
	}
	out.style = LightStyle(slot, vertex ? s.vertexStyles[slot] : s.styles[slot]);
	if (out.style.status == StyleStatus::Unused)
		out.reasons |= UnusedStyle;
	if (vertex)
	{
		out.path = ObservationPath::VertexColour;
		out.reasons |= VertexEvidenceOnly | VertexPaintOrLightingUnknown;
		if (out.style.status == StyleStatus::Styled)
			out.reasons |= SeparateStyledChannel;
		if (!VertexPath(s))
			out.reasons |= InvalidSourceCombination;
	}
	else if (s.lightmap[slot] < 0)
	{
		out.path = ObservationPath::Other;
		out.reasons |= NoLightmap;
	}
	else if (out.style.status == StyleStatus::Styled)
	{
		out.path = ObservationPath::StyledLightmap;
		out.reasons |= SeparateStyledChannel;
	}
	else
	{
		out.path = ObservationPath::StaticLightmap;
		out.reasons |= RelativeBakedCandidate;
	}
	if (!m.available || (m.signals & MaterialMissing))
		out.reasons |= MissingMaterial;
	if (out.materialUncertainty & (MultipleBaseLayers | MaterialValuesInvalid))
		out.reasons |= UnsupportedMaterial;
	if (m.signals & MaterialSky)
		out.reasons |= SkyBoundary;
	if (out.emitterHint)
		out.reasons |= EmissiveHintOnly;
	if (m.signals & MaterialCutout)
		out.reasons |= AlphaCoverageUnknown;
	if (m.signals & MaterialTranslucent)
		out.reasons |= TranslucentTransport;
	if (m.signals & MaterialUnlit)
		out.reasons |= UnlitPresentation;
	if (m.signals & (MaterialAnimated | MaterialEntityDependent))
		out.reasons |= AnimatedMaterial;
	if (m.signals & MaterialReflection)
		out.reasons |= ReflectionPresentation;
	if (s.motion == SurfaceMotion::BrushModel)
		out.reasons |= MovingGeometry;
	if (s.motion == SurfaceMotion::Deformed || (m.signals & MaterialDeformed))
		out.reasons |= DeformedGeometry;
	if (s.motion == SurfaceMotion::Unknown)
		out.reasons |= UnknownGeometryOwnership;
	if (s.excludedMaterial || (m.signals & MaterialNoDraw))
		out.reasons |= IntentionallyExcluded;
	if (s.kind == BakedSurfaceKind::Unsupported)
		out.reasons |= MalformedGeometry;
	if (!vertex && s.lightmap[slot] >= 0 && m.available && !(m.signals & MaterialLightmapped))
		out.reasons |= InvalidSourceCombination;
	out.supplementaryEvidence = !(out.reasons & (Rejections() | UnusedStyle | NoLightmap));
	out.diffuseFitCandidate =
		!vertex && out.path == ObservationPath::StaticLightmap && out.supplementaryEvidence;
	return out;
}
Error ClassifyLuxel(const LuxelTile &tile, size_t ordinal, ObservationClassification &out)
{
	out = ObservationClassification();
	if (!tile.geometry || !tile.images)
		return Failure(Code::Unavailable, "Missing luxel observation leases");
	if (tile.geometry->Map().sha256 != tile.images->Map().sha256 ||
		tile.geometry->Map().path != tile.images->Map().path)
		return Failure(Code::InvalidInput, "Luxel geometry/colour map identities differ");
	if (ordinal >= tile.samples.size() || tile.image >= tile.images->Images().size())
		return Failure(Code::InvalidInput, "Invalid luxel observation ordinal");
	const auto &s = tile.samples[ordinal];
	if (s.coverage == LuxelCoverage::Uncovered)
	{
		out.reasons = NoCoveredCentre;
		return Error();
	}
	// Unsupported masks may come from several conservative UV bounds; the
	// mapper deliberately supplies no authoritative surface/material identity.
	if (s.coverage == LuxelCoverage::Unsupported)
	{
		out.reasons = UnsupportedChart | NoCoveredCentre;
		return Error();
	}
	if (s.surface >= tile.geometry->Surfaces().size())
		return Failure(Code::InvalidInput, "Invalid luxel surface reference");
	out = ClassifySurface(tile.geometry->Surfaces()[s.surface], s.styleSlot, false);
	const auto &im = tile.images->Images()[tile.image];
	if (im.missing)
		out.reasons |= MissingColourImage;
	if (im.role != LightmapRole::Lighting || im.logicalIndex != s.logicalLightmap)
		out.reasons |= InvalidSourceCombination;
	if (s.coverage == LuxelCoverage::Ambiguous)
		out.reasons |= AmbiguousChart;
	if (s.coverage == LuxelCoverage::Unsupported)
		out.reasons |= UnsupportedChart;
	if (s.coverage == LuxelCoverage::FootprintOnly || !s.positionKnown)
		out.reasons |= NoCoveredCentre;
	if (s.issues & LuxelMultipleCharts)
		out.reasons |= MultipleChartReferences;
	if (im.encoding == LightmapEncoding::ExternalRgb32F && im.floats)
	{
		const uint64_t first = (uint64_t(s.y) * im.width + s.x) * 3;
		if (first > im.floats->size() || im.floats->size() - size_t(first) < 3)
			out.reasons |= InvalidColourData;
		else
			for (size_t channel = 0; channel < 3; ++channel)
			{
				float value = (*im.floats)[size_t(first) + channel];
				if (!std::isfinite(value) || value < 0)
					out.reasons |= InvalidColourData;
			}
	}
	if (out.reasons & (Rejections() | InvalidColourData))
		out.diffuseFitCandidate = out.supplementaryEvidence = false;
	return Error();
}
const char *DeluxeStateName(DeluxeState s)
{
	switch (s)
	{
#define STATE(x)                                                                                   \
	case DeluxeState::x:                                                                           \
		return #x
		STATE(Available);
		STATE(Missing);
		STATE(Unpaired);
		STATE(Ambiguous);
		STATE(DimensionsMismatch);
		STATE(UnsupportedEncoding);
		STATE(ZeroSentinel);
		STATE(Neutral);
		STATE(Invalid);
#undef STATE
	}
	return "Invalid";
}
DeluxeDirection DecodeDeluxeRgb8(const uint8_t raw[4])
{
	DeluxeDirection out;
	if (!raw)
	{
		out.state = DeluxeState::Invalid;
		return out;
	}
	std::memcpy(out.raw, raw, 4);
	if (!(raw[0] | raw[1] | raw[2]))
	{
		out.state = DeluxeState::ZeroSentinel;
		return out;
	}
	double x = double(raw[0]) / 255 - .5, y = double(raw[1]) / 255 - .5,
		   z = double(raw[2]) / 255 - .5;
	out.signedEncoded = Vec3(float(x), float(y), float(z));
	double magnitude = std::sqrt(x * x + y * y + z * z), delta = std::sqrt(3.) / 255;
	// Unknown bake rounding: a conservative +/-one code in each component.
	// Around the neutral code, that box includes zero and has no reliable angle.
	if (magnitude <= delta)
	{
		out.state = DeluxeState::Neutral;
		return out;
	}
	out.state = DeluxeState::Available;
	out.toSource = Vec3(float(x / magnitude), float(y / magnitude), float(z / magnitude));
	out.angularBoundRadians = float(std::asin(std::min(1., delta / magnitude)));
	return out;
}
Error BindDeluxe(std::shared_ptr<const LightmapObservations> owner, size_t lighting,
				 DeluxeBinding &out, size_t candidate)
{
	out = DeluxeBinding();
	out.owner = owner;
	out.lighting = lighting;
	if (!owner)
		return Failure(Code::Unavailable, "Missing lightmap snapshot");
	const auto &images = owner->Images();
	if (lighting >= images.size() || images[lighting].role != LightmapRole::Lighting)
		return Failure(Code::InvalidInput, "Deluxe binding requires a colour image");
	const auto &base = images[lighting];
	if (base.missing)
		return Error();
	if (candidate != SIZE_MAX && candidate >= images.size())
		return Failure(Code::InvalidInput, "Invalid deluxe candidate ordinal");
	size_t chosen = SIZE_MAX, matches = 0;
	bool preferred = false, unpaired = false;
	for (size_t i = 0; i < images.size(); ++i)
	{
		if (candidate != SIZE_MAX && i != candidate)
			continue;
		const auto &im = images[i];
		if (im.role != LightmapRole::DeluxeDirection)
			continue;
		if (im.pairedLightingIndex == UINT32_MAX)
		{
			unpaired = true;
			continue;
		}
		if (im.pairedLightingIndex != base.logicalIndex || im.missing)
			continue;
		bool sameRepresentation = (base.encoding == LightmapEncoding::BspRgb8) ==
								  (im.encoding == LightmapEncoding::BspRgb8);
		if (preferred && !sameRepresentation)
			continue;
		if (sameRepresentation && !preferred)
		{
			matches = 0;
			preferred = true;
		}
		chosen = i;
		++matches;
	}
	if (!matches)
	{
		out.state = unpaired ? DeluxeState::Unpaired : DeluxeState::Missing;
		return Error();
	}
	if (matches > 1)
	{
		out.state = DeluxeState::Ambiguous;
		return Error();
	}
	out.deluxe = chosen;
	out.alternativeFallback = !preferred;
	const auto &im = images[chosen];
	if (im.width != base.width || im.height != base.height)
	{
		out.state = DeluxeState::DimensionsMismatch;
		return Error();
	}
	if (im.encoding == LightmapEncoding::ExternalRgb32F)
	{
		out.state = DeluxeState::UnsupportedEncoding;
		return Error();
	}
	out.state = DeluxeState::Available;
	return Error();
}
Error ReadDeluxe(const DeluxeBinding &binding, uint32_t x, uint32_t y, const ReadContext &ctx,
				 DeluxeSample &out)
{
	out = DeluxeSample();
	out.binding = binding;
	out.x = x;
	out.y = y;
	out.direction.state = binding.state;
	if (Cancelled(ctx))
		return Failure(Code::Cancelled, "Deluxe read cancelled");
	if (!binding.owner)
		return Failure(Code::Unavailable, "Missing deluxe lease");
	const auto &images = binding.owner->Images();
	if (binding.lighting >= images.size())
		return Failure(Code::InvalidInput, "Invalid deluxe binding");
	const auto &base = images[binding.lighting];
	if (base.role != LightmapRole::Lighting || base.missing)
		return Failure(Code::InvalidInput, "Deluxe binding does not reference present colour data");
	if (x >= base.width || y >= base.height)
		return Failure(Code::InvalidInput, "Deluxe texel outside base image");
	if (binding.state != DeluxeState::Available)
		return Error();
	if (binding.deluxe >= images.size())
		return Failure(Code::InvalidInput, "Invalid deluxe image reference");
	const auto &im = images[binding.deluxe];
	if (im.role != LightmapRole::DeluxeDirection || im.pairedLightingIndex != base.logicalIndex ||
		im.width != base.width || im.height != base.height || im.missing ||
		(im.channels != 3 && im.channels != 4) || im.encoding == LightmapEncoding::ExternalRgb32F)
		return Failure(Code::InvalidInput, "Inconsistent deluxe binding");
	Chunk<uint8_t> chunk;
	auto error = binding.owner->ReadBytes(
		binding.deluxe, (uint64_t(y) * im.width + x) * im.channels, im.channels, ctx, chunk);
	if (!error)
		return error;
	uint8_t raw[4] = {0, 0, 0, 255};
	std::memcpy(raw, chunk.data, im.channels);
	out.direction = DecodeDeluxeRgb8(raw);
	return Error();
}
Error ReadLuxelDeluxe(const LuxelTile &tile, size_t sample, const ReadContext &ctx,
					  DeluxeSample &out)
{
	out = DeluxeSample();
	if (sample >= tile.samples.size())
		return Failure(Code::InvalidInput, "Invalid luxel ordinal for deluxe read");
	if (tile.samples[sample].coverage != LuxelCoverage::Mapped)
		return Failure(Code::Unavailable, "Deluxe evidence requires an unambiguous mapped centre");
	DeluxeBinding binding;
	auto error = BindDeluxe(tile.images, tile.image, binding);
	if (!error)
		return error;
	return ReadDeluxe(binding, tile.samples[sample].x, tile.samples[sample].y, ctx, out);
}
Error ReadVertexObservation(const Decode::Decoder &decoder,
							std::shared_ptr<const SurfaceGeometry> geometry, uint32_t surface,
							uint32_t vertex, uint32_t slot, const ReadContext &ctx,
							VertexObservation &out)
{
	out = VertexObservation();
	if (!geometry)
		return Failure(Code::Unavailable, "Missing original surface geometry");
	if (surface >= geometry->Surfaces().size() || slot >= 4)
		return Failure(Code::InvalidInput, "Invalid vertex observation reference");
	const auto &s = geometry->Surfaces()[surface];
	if (vertex >= s.vertices.size())
		return Failure(Code::InvalidInput, "Invalid surface vertex ordinal");
	const auto &v = s.vertices[vertex];
	if (!VertexPath(s) || !v.colourPresent || s.vertexStyles[slot] == 255)
		return Failure(Code::Unavailable,
					   "No source vertex-lighting channel (no dense lightmap is fabricated)");
	if (Cancelled(ctx))
		return Failure(Code::Cancelled, "Vertex observation cancelled");
	if (ctx.maxBytes < sizeof(VertexObservation))
		return Failure(Code::MemoryLimit, "Vertex observation exceeds read budget");
	out.owner = geometry;
	out.surface = surface;
	out.vertex = vertex;
	out.sourceVertex = v.sourceVertex;
	out.positionWorld = v.position;
	out.shadingNormal = v.shadingNormal;
	double n = double(v.shadingNormal.x) * v.shadingNormal.x +
			   double(v.shadingNormal.y) * v.shadingNormal.y +
			   double(v.shadingNormal.z) * v.shadingNormal.z;
	out.shadingNormalKnown = Finite(v.shadingNormal) && n > 1e-20;
	if (out.shadingNormalKnown)
	{
		n = std::sqrt(n);
		out.shadingNormal = Vec3(float(v.shadingNormal.x / n), float(v.shadingNormal.y / n),
								 float(v.shadingNormal.z / n));
	}
	else
		out.shadingNormal = Vec3();
	out.style = LightStyle(slot, s.vertexStyles[slot]);
	out.classification = ClassifySurface(s, slot, true);
	Decode::Raw raw;
	raw.encoding = Decode::Encoding::Rgba8;
	raw.component = Decode::Component::Vertex;
	std::memcpy(raw.bytes, v.colour[slot], 4);
	auto error = decoder.Run(raw, out.colour);
	if (!error)
	{
		out.classification.reasons |= InvalidColourData;
		out.classification.supplementaryEvidence = false;
		return error;
	}
	out.hdrState = geometry->HdrState();
	Decode::Raw hdr;
	hdr.component = Decode::Component::Vertex;
	hdr.encoding = Decode::Encoding::LinearRgb32F;
	hdr.hdrOrigin = Decode::HdrOrigin::VertexRawFloat32;
	hdr.missing = true;
	if (out.hdrState == VertexHdrState::Present)
	{
		uint64_t first = uint64_t(v.sourceVertex) * 3;
		const auto &values = geometry->VertexHdr();
		if (v.sourceVertex == UINT32_MAX || first > values.size() ||
			values.size() - size_t(first) < 3)
			out.hdrState = VertexHdrState::Invalid;
		else
		{
			hdr.hdr =
				Vec3(values[size_t(first)], values[size_t(first) + 1], values[size_t(first) + 2]);
			hdr.missing = false;
		}
	}
	// Optional HDR alternative can be invalid independently of valid original LDR.
	decoder.Run(hdr, out.hdrAlternative);
	return Error();
}
Error SummarizeObservations(std::shared_ptr<const SurfaceGeometry> geometry,
							std::shared_ptr<const LightmapObservations> images,
							const ClassificationOptions &options, const ReadContext &ctx,
							ObservationSummary &out)
{
	out = ObservationSummary();
	if (!geometry || !images)
		return Failure(Code::Unavailable, "Missing observation snapshots");
	if (geometry->Map().sha256 != images->Map().sha256 ||
		geometry->Map().path != images->Map().path)
		return Failure(Code::InvalidInput, "Observation map identities differ");
	if (images->Images().size() > options.maxImages)
		return Failure(Code::MemoryLimit, "Too many observation images");
	if (Cancelled(ctx))
		return Failure(Code::Cancelled, "Classification cancelled");
	try
	{
		ObservationSummary summary;
		{
			std::set<std::tuple<uint64_t, uint32_t, uint8_t>> vertices;
			for (const auto &s : geometry->Surfaces())
			{
				if (Cancelled(ctx))
					return Failure(Code::Cancelled, "Classification cancelled");
				++summary.surfaces;
				const uint32_t f = s.materialFacts.signals;
				summary.emissiveSurfaces += ClassifySurface(s, 0, false).emitterHint;
				summary.skySurfaces += (f & MaterialSky) != 0;
				summary.cutoutSurfaces += (f & MaterialCutout) != 0;
				summary.translucentSurfaces += (f & MaterialTranslucent) != 0;
				summary.unlitSurfaces += (f & MaterialUnlit) != 0;
				summary.movingSurfaces += s.motion == SurfaceMotion::BrushModel;
				summary.animatedSurfaces +=
					(f & (MaterialAnimated | MaterialDeformed | MaterialEntityDependent)) != 0;
				bool usable = false;
				for (uint32_t slot = 0; slot < 4; ++slot)
				{
					if (s.styles[slot] == 255)
						++summary.unusedChannels;
					else if (s.lightmap[slot] >= 0)
					{
						++summary.lightmappedCharts;
						++summary.styles[s.styles[slot]];
						if (s.styles[slot] == 0)
							++summary.staticCharts;
						else
							++summary.styledCharts;
						usable = usable || ClassifySurface(s, slot, false).supplementaryEvidence;
					}
					if (VertexPath(s) && s.vertexStyles[slot] != 255)
					{
						++summary.vertexStyles[s.vertexStyles[slot]];
						usable = usable || ClassifySurface(s, slot, true).supplementaryEvidence;
						for (size_t v = 0; v < s.vertices.size(); ++v)
							if (s.vertices[v].colourPresent)
							{
								uint64_t id = s.vertices[v].sourceVertex == UINT32_MAX
												  ? (1ull << 63) | (uint64_t(s.ordinal) << 32) | v
												  : s.vertices[v].sourceVertex;
								if (vertices.emplace(id, slot, s.vertexStyles[slot]).second &&
									vertices.size() > ctx.maxBytes / 128)
									return Failure(Code::MemoryLimit,
												   "Vertex identity summary exceeds budget");
							}
					}
				}
				if (!usable)
					++summary.excludedSurfaces;
			}
			summary.vertexSamples = vertices.size();
		}
		std::set<uint32_t> selected;
		for (size_t image = 0; image < images->Images().size(); ++image)
		{
			const auto &im = images->Images()[image];
			if (im.missing)
			{
				++summary.missingImages;
				continue;
			}
			if (im.role != LightmapRole::Lighting)
				continue;
			if (!selected.insert(im.logicalIndex).second)
			{
				++summary.alternativeImages;
				continue;
			}
			++summary.selectedLightingImages;
			DeluxeBinding binding;
			auto error = BindDeluxe(images, image, binding);
			if (!error)
				return error;
			if (binding.state == DeluxeState::Available)
				++summary.pairedDeluxeImages;
			else
				++summary.unavailableDeluxeImages;
			if (!options.scanLuxels)
				continue;
			uint64_t pixels = uint64_t(im.width) * im.height;
			if (pixels > options.maxTexels - summary.texels)
				return Failure(Code::WorkLimit, "Classification scan texel budget exceeded");
			summary.texels += pixels;
			for (uint32_t y = 0; y < im.height; y += 128)
				for (uint32_t x = 0; x < im.width; x += 128)
				{
					LuxelTile tile;
					error = MapLuxels(geometry, images, image, x, y, std::min(128u, im.width - x),
									  std::min(128u, im.height - y), MappingOptions(), ctx, tile);
					if (!error)
						return error;
					for (size_t i = 0; i < tile.samples.size(); ++i)
					{
						if (Cancelled(ctx))
							return Failure(Code::Cancelled, "Classification cancelled");
						const auto &sample = tile.samples[i];
						ObservationClassification c;
						error = ClassifyLuxel(tile, i, c);
						if (!error)
							return error;
						if (sample.coverage == LuxelCoverage::Mapped)
						{
							++summary.validLuxels;
							if (c.path == ObservationPath::StyledLightmap)
								++summary.styledLuxels;
							DeluxeSample direction;
							error = ReadDeluxe(binding, sample.x, sample.y, ctx, direction);
							if (!error)
								return error;
							if (direction.direction.state == DeluxeState::Available)
								++summary.deluxeAvailable;
							else
								++summary.deluxeUnavailable;
						}
						if (c.diffuseFitCandidate)
							++summary.fitCandidates;
						else
						{
							++summary.excludedLuxels;
							for (size_t bit = 0; bit < kObservationReasonCount; ++bit)
								if (c.reasons &
									(Rejections() | UnusedStyle | SeparateStyledChannel |
									 InvalidColourData) &
									(1ull << bit))
									++summary.exclusionReasons[bit];
						}
					}
				}
		}
		summary.luxelsScanned = options.scanLuxels;
		out = summary;
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Classification allocation failed");
	}
}
} // namespace Core
} // namespace Rend2LightRecon
