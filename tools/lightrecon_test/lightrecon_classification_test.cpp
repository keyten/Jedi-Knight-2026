// Synthetic baked evidence; no GL, game assets, renderer or engine functions.
#include "lightrecon/surfaces.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
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
			std::printf("FAIL %d: %s\n", __LINE__, #x);                                            \
		}                                                                                          \
	} while (0)
bool Near(double a, double b, double e = 1e-6)
{
	return std::abs(a - b) <= e;
}
ReadContext Context()
{
	ReadContext c;
	c.maxBytes = 32ull * 1024 * 1024;
	return c;
}
MaterialFacts Diffuse()
{
	MaterialFacts m;
	m.available = true;
	m.signals = MaterialLightmapped;
	m.meaning = BakedMeaning::DiffuseModulationBakeUnknown;
	m.baseTexture = "textures/test/diffuse";
	return m;
}
BakedSurface Quad(uint32_t ordinal = 0)
{
	BakedSurface s;
	s.ordinal = ordinal;
	s.kind = BakedSurfaceKind::Face;
	s.motion = SurfaceMotion::StaticWorld;
	s.material = "textures/test";
	s.lightmap[0] = 0;
	s.styles[0] = 0;
	s.vertexStyles[0] = 0;
	s.materialFacts = Diffuse();
	for (uint32_t y = 0; y < 2; ++y)
		for (uint32_t x = 0; x < 2; ++x)
		{
			BakedVertex v;
			v.position = Vec3(float(x * 4), float(y * 4), 0);
			v.shadingNormal = Vec3(0, 0, 1);
			v.lightmap[0] = v.lightmap[1] = LightmapUv(x, y);
			v.textureUv = LightmapUv(x * 2, y * 2);
			v.sourceVertex = y * 2 + x;
			v.colourPresent = true;
			for (unsigned c = 0; c < 4; ++c)
			{
				v.colour[c][0] = uint8_t(40 + c);
				v.colour[c][1] = 128;
				v.colour[c][2] = 255;
				v.colour[c][3] = 97;
			}
			s.vertices.push_back(v);
		}
	s.indices = {0, 1, 2, 1, 3, 2};
	return s;
}
struct Fixture
{
	LightmapCapture images;
	SurfaceCapture geometry;
	uint8_t bsp[128] = {};
	Fixture()
	{
		images.Begin(BuildPolicy::ForceRebuild);
		CHECK(images.SetMap("maps/test.bsp", bsp, sizeof(bsp)));
		geometry.Begin(BuildPolicy::ForceRebuild);
		CHECK(geometry.SetMap(images.SourceIdentity()));
	}
	void Internal()
	{
		// Four original pages: RGB0, direction1, RGB2, direction3.
		for (unsigned p = 0; p < 4; ++p)
			for (unsigned i = 0; i < 4; ++i)
			{
				bsp[p * 12 + i * 3] = uint8_t(p % 2 ? 128 : 10 + p * 10);
				bsp[p * 12 + i * 3 + 1] = 128;
				bsp[p * 12 + i * 3 + 2] = 255;
			}
		// Source identity must hash the original bytes supplied to Embedded.
		images.Reset();
		images.Begin(BuildPolicy::ForceRebuild);
		CHECK(images.SetMap("maps/test.bsp", bsp, sizeof(bsp)));
		geometry.Reset();
		geometry.Begin(BuildPolicy::ForceRebuild);
		CHECK(geometry.SetMap(images.SourceIdentity()));
		CHECK(images.Embedded(bsp, sizeof(bsp), 0, 48, 2, 2, true));
	}
	void External(uint32_t index, LightmapRole role, uint32_t paired = UINT32_MAX,
				  uint32_t width = 2, const char *path = "maps/test/image.tga")
	{
		std::vector<uint8_t> pixels(size_t(width) * 2 * 4, 128);
		for (size_t i = 0; i < pixels.size(); i += 4)
		{
			pixels[i + 2] = 255;
			pixels[i + 3] = 83;
		}
		CHECK(images.Rgba8(index, role, path, width, 2, pixels.data(), pixels.size(), paired));
	}
	void Finish(std::vector<BakedSurface> surfaces)
	{
		for (auto &s : surfaces)
			CHECK(geometry.Add(std::move(s)));
		CHECK(geometry.Freeze());
		CHECK(images.Freeze());
	}
	LuxelTile Tile(size_t image)
	{
		LuxelTile t;
		const auto &im = images.Acquire()->Images()[image];
		CHECK(MapLuxels(geometry.Acquire(), images.Acquire(), image, 0, 0, im.width, im.height,
						MappingOptions(), Context(), t));
		return t;
	}
};
void Directions()
{
	const uint8_t cardinal[6][4] = {{255, 128, 128, 1}, {0, 128, 128, 2},	{128, 255, 128, 3},
									{128, 0, 128, 4},	{128, 128, 255, 5}, {128, 128, 0, 6}};
	const Vec3 expected[6] = {Vec3(1, 0, 0),  Vec3(-1, 0, 0), Vec3(0, 1, 0),
							  Vec3(0, -1, 0), Vec3(0, 0, 1),  Vec3(0, 0, -1)};
	for (unsigned i = 0; i < 6; ++i)
	{
		auto d = DecodeDeluxeRgb8(cardinal[i]);
		CHECK(d.state == DeluxeState::Available);
		CHECK(d.raw[3] == i + 1);
		CHECK(!d.confidenceKnown);
		double dot = double(d.toSource.x) * expected[i].x + double(d.toSource.y) * expected[i].y +
					 double(d.toSource.z) * expected[i].z;
		CHECK(std::acos(std::min(1., dot)) <= d.angularBoundRadians);
		CHECK(d.angularBoundRadians > 0);
	}
	for (unsigned neutral : {127u, 128u})
	{
		uint8_t p[4] = {uint8_t(neutral), uint8_t(neutral), uint8_t(neutral), 255};
		CHECK(DecodeDeluxeRgb8(p).state == DeluxeState::Neutral);
	}
	uint8_t zero[4] = {};
	CHECK(DecodeDeluxeRgb8(zero).state == DeluxeState::ZeroSentinel);
	CHECK(DecodeDeluxeRgb8(nullptr).state == DeluxeState::Invalid);
	// Deterministic spherical encoding; error bounds include unknown bake rounding.
	for (unsigned i = 0; i < 2048; ++i)
	{
		double z = 1 - 2 * (i + .5) / 2048., angle = i * 2.399963229728653,
			   r = std::sqrt(1 - z * z);
		double source[3] = {r * std::cos(angle), r * std::sin(angle), z};
		uint8_t p[4] = {0, 0, 0, 255};
		for (unsigned c = 0; c < 3; ++c)
			p[c] = uint8_t(std::floor((source[c] * .5 + .5) * 255 + .5));
		auto d = DecodeDeluxeRgb8(p);
		CHECK(d.state == DeluxeState::Available);
		double dot = source[0] * d.toSource.x + source[1] * d.toSource.y + source[2] * d.toSource.z;
		CHECK(std::acos(std::min(1., dot)) <= d.angularBoundRadians + 1e-6);
	}
}
void StylesAndPairs()
{
	Fixture f;
	f.Internal();
	auto s = Quad();
	s.lightmap[1] = 2;
	s.styles[1] = 7;
	float hdr[12] = {2, 3, 4};
	CHECK(f.images.Rgb32F(0, "maps/test/lm_0000.hdr", 2, 2, hdr, 12));
	f.Finish({s});
	CHECK(f.images.Acquire()->Images().size() == 5);
	CHECK(f.images.Acquire()->Images()[2].pairedLightingIndex == 0);
	auto a = f.Tile(0), b = f.Tile(3);
	ObservationClassification c;
	CHECK(ClassifyLuxel(a, 0, c));
	CHECK(c.path == ObservationPath::StaticLightmap && c.diffuseFitCandidate);
	CHECK(c.reasons & UnknownBakeAlbedo);
	CHECK(c.materialUncertainty & TextureIsNotBakeAlbedo);
	CHECK(ClassifyLuxel(b, 0, c));
	CHECK(c.path == ObservationPath::StyledLightmap && !c.diffuseFitCandidate &&
		  c.supplementaryEvidence);
	CHECK(c.style.id == 7 && c.style.slot == 1);
	DecodedSurfaceObservation read;
	CHECK(ReadSurfaceObservation(Decode::Decoder(), a, 0, Context(), read));
	CHECK(read.colour.value.raw.bytes[0] == 10);
	CHECK(read.deluxe.direction.state == DeluxeState::Available);
	CHECK(read.deluxe.binding.owner == a.images && read.deluxe.binding.deluxe == 2);
	CHECK(read.deluxe.x == a.samples[0].x);
	CHECK(read.deluxe.direction.toSource.z > .9999f);
	CHECK(!read.deluxe.direction.confidenceKnown);
	DeluxeBinding binding;
	CHECK(BindDeluxe(a.images, 1, binding));
	CHECK(binding.state == DeluxeState::Available && binding.alternativeFallback);
	ClassificationOptions o;
	o.scanLuxels = true;
	ObservationSummary summary;
	CHECK(SummarizeObservations(f.geometry.Acquire(), a.images, o, Context(), summary));
	CHECK(summary.selectedLightingImages == 2 && summary.alternativeImages == 1 &&
		  summary.lightmappedCharts == 2);
	CHECK(summary.styles[0] == 1 && summary.styles[7] == 1 && summary.staticCharts == 1 &&
		  summary.styledCharts == 1);
	CHECK(summary.texels == 8 && summary.validLuxels == 8 && summary.fitCandidates == 4 &&
		  summary.styledLuxels == 4);
	CHECK(summary.deluxeAvailable == 8 && summary.deluxeUnavailable == 0);
	CHECK(summary.vertexSamples == 0);
	CHECK(LightStyle(2, 255).status == StyleStatus::Unused &&
		  LightStyle(1, 254).status == StyleStatus::Styled);
	CHECK(!ClassifyLuxel(a, a.samples.size(), c));
}
void ConflictingChannels()
{
	Fixture f;
	f.External(0, LightmapRole::Lighting);
	auto s = Quad();
	s.lightmap[1] = 0;
	s.styles[1] = 7;
	f.Finish({s});
	auto t = f.Tile(0);
	CHECK(t.report.mapped == 4);
	ObservationClassification c;
	CHECK(ClassifyLuxel(t, 0, c));
	CHECK(!c.diffuseFitCandidate && (c.reasons & MultipleChartReferences));
	ObservationSummary summary;
	ClassificationOptions options;
	options.scanLuxels = true;
	CHECK(SummarizeObservations(f.geometry.Acquire(), f.images.Acquire(), options, Context(),
								summary));
	CHECK(summary.lightmappedCharts == 2 && summary.selectedLightingImages == 1 &&
		  summary.validLuxels == 4 && summary.fitCandidates == 0);
	Fixture negative;
	float rgb[12] = {-1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
	CHECK(negative.images.Rgb32F(0, "negative.hdr", 2, 2, rgb, 12));
	negative.Finish({Quad()});
	t = negative.Tile(0);
	CHECK(ClassifyLuxel(t, 0, c));
	CHECK(c.reasons & InvalidColourData);
	CHECK(!c.diffuseFitCandidate);
	CHECK(SummarizeObservations(negative.geometry.Acquire(), negative.images.Acquire(), options,
								Context(), summary));
	CHECK(summary.fitCandidates == 3 && summary.exclusionReasons[27] == 1);
}
void MissingAndAmbiguousPairs()
{
	for (unsigned mode = 0; mode < 5; ++mode)
	{
		Fixture f;
		f.External(0, LightmapRole::Lighting);
		if (mode == 1)
			f.External(1, LightmapRole::DeluxeDirection); // no metadata: NEVER infer adjacency
		if (mode == 2)
			f.External(0, LightmapRole::DeluxeDirection, 0, 3);
		if (mode == 3)
		{
			f.External(0, LightmapRole::DeluxeDirection, 0, 2, "a.tga");
			f.External(0, LightmapRole::DeluxeDirection, 0, 2, "b.tga");
		}
		if (mode == 4)
			CHECK(f.images.Missing(0, LightmapRole::DeluxeDirection,
								   LightmapEncoding::ExternalRgba8, "missing.tga", 0));
		f.Finish({Quad()});
		DeluxeBinding d;
		CHECK(BindDeluxe(f.images.Acquire(), 0, d));
		CHECK(d.state ==
			  (mode == 1 ? DeluxeState::Unpaired
						 : mode == 2 ? DeluxeState::DimensionsMismatch
									 : mode == 3 ? DeluxeState::Ambiguous : DeluxeState::Missing));
		DeluxeSample sample;
		CHECK(ReadDeluxe(d, 0, 0, Context(), sample));
		CHECK(sample.direction.state == d.state);
		if (mode == 3)
		{
			CHECK(BindDeluxe(f.images.Acquire(), 0, d, 1));
			CHECK(d.state == DeluxeState::Available);
			CHECK(ReadDeluxe(d, 0, 0, Context(), sample));
			CHECK(sample.direction.raw[3] == 83);
		}
		CHECK(!BindDeluxe(f.images.Acquire(), 999, d));
		auto tile = f.Tile(0);
		DecodedSurfaceObservation out;
		CHECK(ReadSurfaceObservation(Decode::Decoder(), tile, 0, Context(), out));
		CHECK(out.classification
				  .diffuseFitCandidate); // missing directions do not fabricate or invalidate RGB
	}
}
void Materials()
{
	const uint32_t flags[] = {
		MaterialGlow,	 MaterialSky,		 MaterialCutout,		 MaterialTranslucent,
		MaterialUnlit,	 MaterialAnimated,	 MaterialDeformed,		 MaterialNoDraw,
		MaterialMissing, MaterialReflection, MaterialEntityDependent};
	const uint64_t reasons[] = {EmissiveHintOnly,		SkyBoundary,		   AlphaCoverageUnknown,
								TranslucentTransport,	UnlitPresentation,	   AnimatedMaterial,
								DeformedGeometry,		IntentionallyExcluded, MissingMaterial,
								ReflectionPresentation, AnimatedMaterial};
	for (size_t i = 0; i < sizeof(flags) / sizeof(flags[0]); ++i)
	{
		auto s = Quad();
		s.materialFacts.signals |= flags[i];
		auto c = ClassifySurface(s, 0, false);
		CHECK(!c.diffuseFitCandidate && (c.reasons & reasons[i]));
		CHECK(c.emitterHint == (flags[i] == MaterialGlow));
	}
	for (auto motion : {SurfaceMotion::BrushModel, SurfaceMotion::Unknown, SurfaceMotion::Deformed})
	{
		auto s = Quad();
		s.motion = motion;
		auto c = ClassifySurface(s, 0, false);
		CHECK(!c.diffuseFitCandidate);
		CHECK(c.reasons & (MovingGeometry | UnknownGeometryOwnership | DeformedGeometry));
	}
	auto s = Quad();
	s.materialFacts.available = false;
	CHECK(ClassifySurface(s, 0, false).reasons & MissingMaterial);
	s = Quad();
	s.materialFacts.uncertainty |= MultipleBaseLayers;
	CHECK(!ClassifySurface(s, 0, false).diffuseFitCandidate);
	s = Quad();
	s.materialFacts.legacySurfaceLight = 100;
	auto c = ClassifySurface(s, 0, false);
	CHECK(c.emitterHint && !c.diffuseFitCandidate);
	Fixture f;
	f.External(0, LightmapRole::Lighting);
	s = Quad();
	CHECK(f.geometry.Add(s));
	auto facts = Diffuse();
	facts.baseColourKnown = true;
	facts.baseColourLinear = Vec3(.2f, .4f, .8f);
	facts.signals |= MaterialGlow;
	facts.emissionColourKnown = true;
	facts.emissionColour = Vec3(10, 2, 1);
	facts.emissionProvenance = EmissionProvenance::LegacyShader;
	CHECK(f.geometry.Material(0, facts));
	CHECK(f.geometry.Freeze());
	CHECK(f.images.Freeze());
	auto t = f.Tile(0);
	DecodedSurfaceObservation out;
	CHECK(ReadSurfaceObservation(Decode::Decoder(), t, 0, Context(), out));
	CHECK(out.colour.value.raw.bytes[0] == 128 &&
		  out.colour.value.raw.bytes[2] == 255); // no division by albedo, no glow added
	CHECK(out.classification.emitterHint && !out.classification.diffuseFitCandidate);
	CHECK(Near(out.geometry->Surfaces()[0].materialFacts.baseColourLinear.y, .4));
	ObservationSummary sum;
	ClassificationOptions opt;
	opt.scanLuxels = true;
	CHECK(SummarizeObservations(f.geometry.Acquire(), f.images.Acquire(), opt, Context(), sum));
	CHECK(sum.emissiveSurfaces == 1 && sum.validLuxels == 4 && sum.fitCandidates == 0 &&
		  sum.excludedLuxels == 4);
	// Non-finite material values are retained as uncertain, never fit-ready.
	Fixture invalid;
	invalid.External(0, LightmapRole::Lighting);
	CHECK(invalid.geometry.Add(Quad()));
	facts = Diffuse();
	facts.baseColourKnown = true;
	facts.baseColourLinear.x = std::numeric_limits<float>::infinity();
	CHECK(invalid.geometry.Material(0, facts));
	CHECK(invalid.geometry.Freeze());
	CHECK(
		!ClassifySurface(invalid.geometry.Acquire()->Surfaces()[0], 0, false).diffuseFitCandidate);
}
void Vertices()
{
	Fixture f;
	auto s = Quad();
	s.lightmap[0] = -3;
	s.styles[0] = 255;
	s.vertexStyles[1] = 9;
	s.materialFacts.signals = MaterialVertexLit;
	CHECK(f.geometry.Add(s));
	float hdr[12] = {2, 4, 1, 3, 4, 5, 6, 7, 8, 9, 10, 11};
	CHECK(f.geometry.VertexHdr(VertexHdrState::Present, "maps/test/vertlight.raw", hdr, sizeof(hdr),
							   4));
	CHECK(f.geometry.Freeze());
	CHECK(f.images.Freeze());
	auto lease = f.geometry.Acquire();
	VertexObservation v;
	CHECK(ReadVertexObservation(Decode::Decoder(), lease, 0, 0, 0, Context(), v));
	CHECK(v.owner == lease && v.sourceVertex == 0 && v.colour.raw.bytes[3] == 97 &&
		  v.colour.raw.bytes[0] == 40);
	CHECK(v.classification.path == ObservationPath::VertexColour &&
		  !v.classification.diffuseFitCandidate);
	CHECK(v.classification.supplementaryEvidence &&
		  (v.classification.reasons & VertexPaintOrLightingUnknown));
	CHECK(v.hdrState == VertexHdrState::Present && v.hdrAlternative.raw.hdr.x == 2 &&
		  !v.hdrStyleAssociationKnown);
	CHECK(v.colour.absoluteScale == Decode::AbsoluteScale::Unknown && v.shadingNormalKnown);
	CHECK(ReadVertexObservation(Decode::Decoder(), lease, 0, 0, 1, Context(), v));
	CHECK(v.style.id == 9 && v.colour.raw.bytes[0] == 41);
	CHECK(!ReadVertexObservation(Decode::Decoder(), lease, 0, 0, 2, Context(), v));
	CHECK(!v.owner);
	ObservationSummary sum;
	ClassificationOptions opt;
	CHECK(SummarizeObservations(lease, f.images.Acquire(), opt, Context(), sum));
	CHECK(sum.vertexSamples == 8 && sum.vertexStyles[0] == 1 && sum.vertexStyles[9] == 1 &&
		  sum.lightmappedCharts == 0);
	f.geometry.Reset();
	CHECK(ReadVertexObservation(Decode::Decoder(), lease, 0, 3, 0, Context(), v));
	CHECK(v.hdrAlternative.raw.hdr.z == 11);
	Fixture malformed;
	CHECK(malformed.geometry.Add(s));
	CHECK(!malformed.geometry.VertexHdr(VertexHdrState::Present, "bad.raw", hdr, 4, 4));
	CHECK(malformed.geometry.Freeze());
	CHECK(ReadVertexObservation(Decode::Decoder(), malformed.geometry.Acquire(), 0, 0, 0, Context(),
								v));
	CHECK(v.hdrState == VertexHdrState::Invalid);
	Fixture nonfinite;
	CHECK(nonfinite.geometry.Add(s));
	hdr[0] = std::numeric_limits<float>::quiet_NaN();
	CHECK(nonfinite.geometry.VertexHdr(VertexHdrState::Present, "nan.raw", hdr, sizeof(hdr), 4));
	CHECK(nonfinite.geometry.Freeze());
	CHECK(ReadVertexObservation(Decode::Decoder(), nonfinite.geometry.Acquire(), 0, 0, 0, Context(),
								v));
	CHECK(v.hdrAlternative.encoded.state == Decode::State::Invalid &&
		  std::isnan(v.hdrAlternative.raw.hdr.x));
	CHECK(v.colour.encoded.state == Decode::State::Available);
	Fixture lightmapped;
	lightmapped.External(0, LightmapRole::Lighting);
	lightmapped.Finish({Quad()});
	CHECK(!ReadVertexObservation(Decode::Decoder(), lightmapped.geometry.Acquire(), 0, 0, 0,
								 Context(), v));
}
void VertexMath()
{
	Decode::Profile p;
	p.source.assumedTransfer = Decode::Transfer::Linear;
	p.source.assumptionRationale = "Synthetic linear fixture";
	p.renderer.known = true;
	p.renderer.mapOverbrightBits = 1;
	Decode::Raw raw;
	raw.component = Decode::Component::Vertex;
	raw.encoding = Decode::Encoding::Rgba8;
	raw.bytes[0] = 64;
	raw.bytes[1] = 128;
	raw.bytes[2] = 255;
	Decode::Observation v;
	CHECK(Decode::Decoder(p).Run(raw, v));
	CHECK(Near(v.renderer.rgb.x, 64. / 255));
	CHECK(Near(v.renderer.rgb.y, 128. / 255));
	CHECK(v.renderer.flags & Decode::RendererNormalized);
	CHECK(!(v.renderer.flags & Decode::RendererQuantized));
	p.renderer.hdr = true;
	p.renderer.floatLightmaps = true;
	raw.bytes[0] = 0;
	raw.bytes[1] = 1;
	raw.bytes[2] = 2;
	CHECK(Decode::Decoder(p).Run(raw, v));
	CHECK(Near(v.renderer.rgb.x, .998 / 255));
	CHECK(Near(v.renderer.rgb.z, 4. / 255));
	CHECK(!(v.renderer.flags & Decode::RendererDimGrayscale));
	p.renderer.floatLightmaps = false;
	raw.encoding = Decode::Encoding::LinearRgb32F;
	raw.hdrOrigin = Decode::HdrOrigin::VertexRawFloat32;
	raw.hdr = Vec3(2, 4, 1);
	CHECK(Decode::Decoder(p).Run(raw, v));
	CHECK(Near(v.renderer.rgb.x, .5) && Near(v.renderer.rgb.y, 1) && Near(v.renderer.rgb.z, .25));
	CHECK(v.source.rgb.y == 4);
	p.renderer.hdr = true;
	p.renderer.toneMap = true;
	p.renderer.linearLighting = true;
	p.renderer.mapOverbrightBits = 0;
	raw.encoding = Decode::Encoding::Rgba8;
	raw.bytes[0] = raw.bytes[1] = raw.bytes[2] = 128;
	CHECK(Decode::Decoder(p).Run(raw, v));
	CHECK(Near(v.renderer.rgb.x, .2158605001));
}
class Stop : public Cancellation
{
	bool IsCancelled() const noexcept override
	{
		return true;
	}
};
void Boundaries()
{
	Fixture empty;
	empty.Finish({});
	ObservationSummary out;
	ClassificationOptions o;
	o.scanLuxels = true;
	CHECK(
		SummarizeObservations(empty.geometry.Acquire(), empty.images.Acquire(), o, Context(), out));
	CHECK(out.luxelsScanned && out.surfaces == 0 && out.texels == 0);
	Fixture f;
	f.External(0, LightmapRole::Lighting);
	f.Finish({Quad()});
	o.maxTexels = 1;
	CHECK(SummarizeObservations(f.geometry.Acquire(), f.images.Acquire(), o, Context(), out).code ==
		  Code::WorkLimit);
	CHECK(out.surfaces == 0);
	Stop stop;
	auto ctx = Context();
	ctx.cancellation = &stop;
	o.maxTexels = 100;
	CHECK(SummarizeObservations(f.geometry.Acquire(), f.images.Acquire(), o, ctx, out).code ==
		  Code::Cancelled);
	CHECK(!ClassifySurface(Quad(), 4, false).diffuseFitCandidate);
	CHECK(!ClassifySurface(Quad(), 0, true).supplementaryEvidence);
	auto tile = f.Tile(0);
	tile.samples[0].coverage = LuxelCoverage::Unsupported;
	tile.samples[0].surface = UINT32_MAX;
	ObservationClassification c;
	CHECK(ClassifyLuxel(tile, 0, c));
	CHECK(c.reasons & UnsupportedChart);
	static_assert(std::is_const<typename decltype(f.geometry.Acquire())::element_type>::value,
				  "immutable geometry");
}
} // namespace
int main()
{
	Directions();
	StylesAndPairs();
	ConflictingChannels();
	MissingAndAmbiguousPairs();
	Materials();
	Vertices();
	VertexMath();
	Boundaries();
	std::printf("classification: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
