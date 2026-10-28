// S02 deterministic CPU fixtures. Assertions remain active in Release.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/bake_math.h"
#include "lightrecon/sun_fit.h"
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace S = Rend2LightRecon::Core::Solar;
namespace B = Rend2LightRecon::Core::Bake;
using namespace B::Detail;
int checks = 0, failures = 0;
double Angle(V a, V b) { return std::acos(Clamp(Dot(a, b), -1, 1)); }
#define CHECK(x)                                                                                             \
	do                                                                                                       \
	{                                                                                                        \
		++checks;                                                                                            \
		if (!(x))                                                                                            \
		{                                                                                                    \
			++failures;                                                                                      \
			std::printf("FAIL %d: %s\n", __LINE__, #x);                                                      \
		}                                                                                                    \
	} while (0)
template <class T> Chunk<T> Lease(std::shared_ptr<std::vector<T>> p)
{
	return VectorChunk<T>(p, 0, p->size());
}
class FreeDomain final : public PointClassifier
{
  public:
	Error Classify(Vec3, double, PointDomain &out) const override
	{
		out = PointDomain::Free;
		return {};
	}
};
class Cancel final : public Cancellation
{
  public:
	bool IsCancelled() const noexcept override { return true; }
};
class ClearWithoutSky final : public VisibilityProvider
{
  public:
	Error Query(const RayQuery &, const ReadContext &, RayResult &r) const override
	{
		r.state = VisibilityState::Visible;
		r.sky = SkyReach::NotReached;
		return {};
	}
};
struct Rig
{
	ReadContext read;
	S::Request request;
	std::shared_ptr<std::vector<S::Sample>> train = std::make_shared<std::vector<S::Sample>>();
	std::shared_ptr<std::vector<S::Sample>> validation = std::make_shared<std::vector<S::Sample>>();
	std::shared_ptr<const StaticVisibility> scene;
	std::vector<BakedSurface> surfaces;
	Rig(bool aperture = false, bool sealed = false)
	{
		read.maxBytes = 64ull * 1024 * 1024;
		request.mapKey[0] = 17;
		request.lineage = "generated-solar-test";
		request.options.sphereDirections = 96;
		request.options.refineLevels = 4;
		request.options.refineSeeds = 4;
		request.options.allowAssumedProfiles = true;
		request.profiles = {B::NetRadiantProfile()};
		Quad(Vec3(-20000, -20000, 1000), Vec3(20000, -20000, 1000), Vec3(20000, 20000, 1000),
			 Vec3(-20000, 20000, 1000), MaterialSky);
		Quad(Vec3(-20000, -20000, -100), Vec3(20000, -20000, -100), Vec3(20000, 20000, -100),
			 Vec3(-20000, 20000, -100));
		if (aperture || sealed)
		{
			// Partial roof blocks the northern half of the scene. A real opening
			// still reaches the separate sky polygon, not merely empty space.
			float south = sealed ? -20000.f : 0.f;
			Quad(Vec3(-20000, south, 350), Vec3(20000, south, 350), Vec3(20000, 20000, 350),
				 Vec3(-20000, 20000, 350));
		}
		Build();
		const V normals[] = {V(0, 0, 1),		Unit(V(1, 0, .3)), Unit(V(-1, 0, .8)),
							 Unit(V(0, 1, .5)), Unit(V(0, -1, 1)), Unit(V(.5, .5, 1))};
		for (int part = 0; part < 2; ++part)
			for (int block = 0; block < 4; ++block)
				for (int j = 0; j < 6; ++j)
				{
					S::Sample sample;
					sample.eligible = sample.encodingVerifiedForProfile = true;
					sample.areaWorldSquared = 16;
					sample.footprintRadiusWorld = 3;
					sample.receiver.sample = Id(1 + part * 100 + block * 6 + j);
					sample.receiver.surface = 0; // synthetic receiver, no invented occluder ordinal
					sample.receiver.world = Vec3(float((part ? 600 : -1200) + block * 160),
												 float(block % 2 ? 100 : -200), float(60 + j * 15));
					sample.receiver.geometricNormal = sample.receiver.shadingNormal = normals[j].Float();
					sample.receiver.mappedPlanar = sample.receiver.compilerPlacementCertified = true;
					(part ? validation : train)->push_back(sample);
				}
	}
	void Quad(Vec3 a, Vec3 b, Vec3 c, Vec3 d, uint32_t flags = 0)
	{
		BakedSurface s;
		s.ordinal = uint32_t(surfaces.size());
		s.kind = BakedSurfaceKind::Face;
		s.motion = SurfaceMotion::StaticWorld;
		s.material = "generated";
		s.materialFacts.available = true;
		s.materialFacts.signals = flags;
		s.materialFacts.cull = SurfaceCull::None;
		for (Vec3 p : {a, b, c, d})
		{
			BakedVertex v;
			v.position = p;
			s.vertices.push_back(v);
		}
		s.indices = {0, 1, 2, 0, 2, 3};
		surfaces.push_back(s);
	}
	void Build()
	{
		SurfaceCapture c;
		c.Begin(BuildPolicy::ForceRebuild);
		LightmapMapIdentity map;
		map.path = "maps/generated-solar.bsp";
		map.sha256 = request.mapKey;
		map.bytes = 1;
		CHECK(c.SetMap(map));
		for (auto s : surfaces)
			CHECK(c.Add(s));
		CHECK(c.Freeze());
		VisibilityBuildOptions o;
		CHECK(StaticVisibility::Build(c.Acquire(), o, read, scene, std::make_shared<FreeDomain>()));
		request.geometry.visibility = scene;
		request.geometry.geometryKey = scene->CertificationKey();
	}
	void Illuminate(V sun, V rgb, V sky, V fill = V(), bool extraLamp = false, V secondSun = V(0, 0, 1),
					V secondRgb = V())
	{
		// Independent analytic sun/sky kernel, production BVH for explicit sky.
		std::vector<V> skyDirections;
		double angle = 0;
		for (double elevation : {Pi / 12, Pi / 4})
		{
			for (int j = 0; j < 8; ++j)
			{
				skyDirections.push_back(V(std::cos(angle) * std::cos(elevation),
										  std::sin(angle) * std::cos(elevation), std::sin(elevation)));
				angle += Pi / 4;
			}
			angle += Pi / 8;
		}
		skyDirections.push_back(V(0, 0, 1));
		for (auto samples : {train, validation})
			for (auto &sample : *samples)
			{
				V normal(sample.receiver.geometricNormal), start = V(sample.receiver.world) + normal,
														   total = fill;
				auto ray = [&](V d) {
					RayQuery q;
					q.startWorld = start.Float();
					q.endWorld = (start + d * 65536).Float();
					q.purpose = RayPurpose::Sky;
					RayResult hit;
					CHECK(scene->Query(q, read, hit));
					return hit.state == VisibilityState::Visible && hit.sky == SkyReach::Reached;
				};
				if (ray(sun))
					total = total + rgb * std::max(0., Dot(normal, sun));
				if (Peak(secondRgb) > 0 && ray(secondSun))
					total = total + secondRgb * std::max(0., Dot(normal, secondSun));
				for (V direction : skyDirections)
					if (ray(direction))
						total = total + sky * (std::max(0., Dot(normal, direction)) / 17);
				if (extraLamp && sample.receiver.world.y > 0)
				{
					total = total + V(150, 20, 10);
					sample.otherPreencoding = Vec3(150, 20, 10);
				}
				double gain = Peak(total) > 255 ? 255 / Peak(total) : 1;
				total = total * gain;
				for (int c = 0; c < 3; ++c)
					sample.rgb[c] = uint8_t(total[c]);
			}
	}
	Error Fit(S::FitResult &fit)
	{
		request.training = Lease(train);
		return S::Fit(request, read, fit);
	}
	Error Run(S::FitResult &fit, S::Result &result)
	{
		auto e = Fit(fit);
		if (!e)
			return e;
		return S::ValidateAndPublish(fit, request.mapKey, Lease(validation), request.geometry, read, result);
	}
};
struct Witness
{
	std::string name, state;
	double angle = -1, rmse = -1;
	double sunRgbRelativeError=-1, skyRgbRelativeError=-1;
	uint64_t rays = 0, alternatives = 0;
};
std::vector<Witness> witnesses;
void Record(const char *name, const S::Result &r, V expected,V expectedSunRgb=V(),V expectedSkyRgb=V())
{
	Witness w;
	w.name = name;
	w.state = S::StateName(r.state);
	w.rays = r.work.rays;
	w.alternatives = r.hypotheses.size();
	for (const auto &h : r.hypotheses)
		if (h.fitted.model == S::Model::SunAndSky)
		{
			w.angle = Angle(V(h.fitted.toSource), expected) * 180 / Pi;
			w.rmse = h.validation.intervalRmse;
			if(Peak(expectedSunRgb)>0)w.sunRgbRelativeError=Length(V(h.fitted.sunRgb)-expectedSunRgb)/Length(expectedSunRgb);
			break;
		}
	if (w.rmse < 0 && !r.hypotheses.empty())
		w.rmse = r.hypotheses[0].validation.intervalRmse;
	if(!r.hypotheses.empty()&&Peak(expectedSkyRgb)>0)w.skyRgbRelativeError=Length(V(r.hypotheses[0].fitted.skyRgb)-expectedSkyRgb)/Length(expectedSkyRgb);
	witnesses.push_back(w);
	std::printf("%s %s alternatives=%llu angular=%.4f rmse=%.4f\n", name, w.state.c_str(),
				(unsigned long long)w.alternatives, w.angle, w.rmse);
}
// S03 reuses the S02 estimator. Exercise the real captured-evidence adapters,
// rather than silently replacing their vectors with a renderer sun fallback.
void CapturedDirectionalEvidence()
{
	EmitterEvidenceInput input;
	input.map.path = "maps/s03-evidence.bsp";
	input.map.sha256[0] = 37;
	input.map.bytes = 1;
	ShaderEvidenceText shader;
	shader.material = "textures/s03/sky";
	shader.text = "{\nq3map_sun 1 .5 .2 300 90 30\n"
				  "q3map_sunExt 1 1 1 100 0 90 2 16\n"
				  "q3gl2_sun 1 1 1 80 180 0\nsun 1 1 1 20 0 0\n"
				  "q3map_skylight 40 4\nskyparms - - -\n}";
	input.shaders.push_back(shader);
	ReadContext read;
	read.maxBytes = 64ull * 1024 * 1024;
	std::shared_ptr<const EmitterEvidenceSet> evidence;
	auto status = ExtractEmitterEvidence(input, EvidenceOptions(), read, evidence);
	CHECK(status);
	CHECK(evidence);
	if (!status || !evidence)
		return;
	auto authored = S::AuthoredHints(*evidence);
	CHECK(authored.size() == 4);
	if (authored.empty())
		return;
	std::set<Id> ids;
	for (const auto &h : authored)
	{
		CHECK(h.kind == S::HintKind::AuthoredSun);
		CHECK(h.staticEligible);
		CHECK(h.id != 0 && ids.insert(h.id).second);
		CHECK(h.provenance.find("textures/s03/sky#shader") != std::string::npos);
		CHECK(Normal(V(h.toSource)));
	}
	// Evidence publication sorts by stable ID, not shader declaration order.
	auto declaration = std::find_if(authored.begin(), authored.end(), [](const S::DirectionHint &h) {
		return h.provenance.find("q3map_sun 1 .5 .2 300 90 30") != std::string::npos;
	});
	CHECK(declaration != authored.end());
	if (declaration != authored.end())
	{
		CHECK(std::abs(declaration->toSource.x) < 1e-6);
		CHECK(std::abs(declaration->toSource.y - std::sqrt(.75)) < 1e-6);
		CHECK(std::abs(declaration->toSource.z - .5) < 1e-6);
	}
	// Skylight/sky geometry supplies no fabricated authored sun direction.
	input.shaders[0].text = "{\nq3map_skylight 40 4\nskyparms - - -\n}";
	status = ExtractEmitterEvidence(input, EvidenceOptions(), read, evidence);
	CHECK(status);
	if (status)
		CHECK(S::AuthoredHints(*evidence).empty());

	GridCell cell;
	cell.presence = GridPresence::Present;
	cell.styleStatus = GridStyle::Static;
	cell.direction = DecodeGridDirection(0, 0);
	auto grid = S::GridHint(cell, 801);
	CHECK(grid.staticEligible);
	CHECK(grid.kind == S::HintKind::GridDominant);
	CHECK(Length(V(grid.toSource) - V(0, 0, 1)) < 1e-6);
	CHECK(grid.quantizationRadians.known && grid.quantizationRadians.value > 0);
	CHECK(std::abs(grid.quantizationRadians.value - cell.direction.errorBoundRadians) < 1e-6);
	cell.styleStatus = GridStyle::Styled;
	CHECK(!S::GridHint(cell, 802).staticEligible);
	cell.styleStatus = GridStyle::Static;
	cell.presence = GridPresence::InvalidIndex;
	CHECK(!S::GridHint(cell, 803).staticEligible);
	cell.presence = GridPresence::Present;
	cell.direction.quality = GridDirectionQuality::Unavailable;
	CHECK(!S::GridHint(cell, 804).staticEligible);

	const uint8_t pixels[4] = {128, 128, 255, 255};
	auto decoded = DecodeDeluxeRgb8(pixels);
	auto deluxe = S::DeluxeHint(decoded, 805);
	CHECK(deluxe.staticEligible);
	CHECK(deluxe.kind == S::HintKind::DeluxeDominant);
	CHECK(Length(V(deluxe.toSource) - V(decoded.toSource)) < 1e-6);
	CHECK(deluxe.toSource.z > .999);
	CHECK(deluxe.quantizationRadians.known && deluxe.quantizationRadians.value > 0);
	decoded.state = DeluxeState::DimensionsMismatch;
	CHECK(!S::DeluxeHint(decoded, 806).staticEligible);
	const uint8_t absent[4] = {0, 0, 0, 255};
	CHECK(!S::DeluxeHint(DecodeDeluxeRgb8(absent), 807).staticEligible);

	V sun = Unit(V(.4, .25, 1));
	Rig rig;
	rig.Illuminate(sun, V(110, 90, 65), V(20, 25, 35));
	// Two contaminated dominant directions are merely seeds; surface evidence
	// must still decide the sun. No mean vector or physical confidence is inferred.
	grid.toSource = Unit(V(-1, -.8, .1)).Float();
	deluxe.toSource = Unit(V(.8, -.8, .05)).Float();
	rig.request.hints = {grid, deluxe};
	S::FitResult fit;
	S::Result result;
	CHECK(rig.Run(fit, result));
	CHECK(!result.hypotheses.empty());
	for (const auto &h : result.hypotheses)
	{
		CHECK(h.fitted.model == S::Model::SunAndSky);
		CHECK(Angle(V(h.fitted.toSource), sun) < 3 * Pi / 180);
		CHECK(!h.asset.emitters[0].source.evidence.physicalEmitterConfidence.known);
	}
	CHECK(std::count_if(fit.diagnostics.begin(), fit.diagnostics.end(), [](const std::string &d) {
		return d.find("seed-only") != std::string::npos;
	}) == 2);
	Record("captured_grid_deluxe_contamination", result, sun);

	// A non-static style is not silently activated as an authored sun.
	rig.request.hints.clear();
	S::FitResult withoutStyle;
	CHECK(rig.Fit(withoutStyle));
	S::DirectionHint styled = authored[0];
	styled.style = 2;
	rig.request.hints = {styled};
	S::FitResult withStyle;
	CHECK(rig.Fit(withStyle));
	CHECK(withStyle.work.directions == withoutStyle.work.directions);
	CHECK(withStyle.proposals.size() == withoutStyle.proposals.size());
	for (size_t i = 0; i < std::min(withStyle.proposals.size(), withoutStyle.proposals.size()); ++i)
	{
		CHECK(Length(V(withStyle.proposals[i].toSource) - V(withoutStyle.proposals[i].toSource)) < 1e-9);
		CHECK(Length(V(withStyle.proposals[i].sunRgb) - V(withoutStyle.proposals[i].sunRgb)) < 1e-9);
		CHECK(!withStyle.proposals[i].authoredSeed);
	}

	// Strong input hints cannot replace missing positive training evidence.
	rig.Illuminate(sun, V(), V());
	rig.request.hints = authored;
	CHECK(rig.Fit(fit).code == Code::Unavailable);
	CHECK(fit.proposals.empty());
	CHECK(fit.state == S::State::Unavailable);

	// Conversely positive training does not bypass independent positive validation.
	rig.Illuminate(sun, V(110, 90, 65), V(20, 25, 35));
	rig.request.hints.clear();
	CHECK(rig.Fit(fit));
	for (auto &s : *rig.validation)
		for (auto &v : s.rgb)
			v = 0;
	CHECK(S::ValidateAndPublish(fit, rig.request.mapKey, Lease(rig.validation), rig.request.geometry,
							  rig.read, result).code == Code::Unavailable);
	CHECK(result.hypotheses.empty());
	Record("positive_training_empty_validation", result, sun);
}
int main(int argc, char **argv)
{
	V sun = Unit(V(.4, .25, 1));
	{
		Rig rig;
		rig.Illuminate(sun, V(110, 90, 65), V(20, 25, 35));
		S::FitResult fit;
		S::Result result;
		auto e = rig.Run(fit, result);
		if (!e)
			std::printf("sun error: %s\n", e.message.c_str());
		CHECK(e);
		CHECK(!result.hypotheses.empty());
		if (!result.hypotheses.empty())
		{
			const auto &h = result.hypotheses.front();
			CHECK(Angle(V(h.fitted.toSource), sun) < 3 * Pi / 180);
			CHECK(Length(V(h.fitted.sunRgb) - V(110, 90, 65)) / Length(V(110, 90, 65)) < .1);
			CHECK(h.validation.intervalRmse < 1.5);
			CHECK(h.coverage.blackRmse > 4);
			CHECK(h.asset.suns.size() == 1);
			CHECK(!h.asset.emitters[0].geometry.angularRadiusRadians.known);
			CHECK(h.asset.emitters[0].emission.unit == PhotometricUnit::Relative);
			CHECK(!h.asset.emitters[0].source.evidence.physicalEmitterConfidence.known);
			CHECK(Length(V(h.asset.emitters[0].frame.forward) + V(h.fitted.toSource)) < 1e-5);
		}
		Record("known_sun_multinormal", result, sun,V(110,90,65),V(20,25,35));
		S::FitResult twice;
		CHECK(rig.Fit(twice));
		CHECK(twice.frozenKey == fit.frozenKey);
		auto before = fit.frozenKey;
		S::Result leak;
		CHECK(!S::ValidateAndPublish(fit, rig.request.mapKey, Lease(rig.train), rig.request.geometry,
									 rig.read, leak));
		CHECK(leak.hypotheses.empty());
		CHECK(before == fit.frozenKey);
		if (!fit.proposals.empty())
		{
			fit.proposals[0].skyRgb.x += 1;
			CHECK(!S::ValidateAndPublish(fit, rig.request.mapKey, Lease(rig.validation), rig.request.geometry,
										 rig.read, leak));
		}
	}
	{
		Rig rig(true);
		rig.Illuminate(sun, V(), V(100, 120, 140));
		S::FitResult fit;
		S::Result result;
		auto e = rig.Run(fit, result);
		if (!e)
			std::printf("sky error: %s\n", e.message.c_str());
		CHECK(e);
		CHECK(result.state == S::State::SkyOnly);
		for (const auto &h : result.hypotheses)
		{
			CHECK(h.asset.suns.empty());
			CHECK(!h.asset.environments.empty());
			CHECK(h.asset.environments[0].sunCoverage == SunCoverage::ExcludesExplicitSun);
			CHECK(h.environment.size() == 17);
		}
		Record("positive_skylight_no_sun", result, sun,V(),V(100,120,140));
	}
	{
		Rig rig(true);
		rig.Illuminate(sun, V(120, 110, 100), V(15, 15, 15), V(), true);
		S::FitResult fit;
		S::Result result;
		auto e = rig.Run(fit, result);
		CHECK(e);
		Record("sun_opening_known_indoor_lamp", result, sun);
	}
	{
		Rig rig;
		rig.Illuminate(sun, V(), V(), V(50, 50, 50));
		S::FitResult fit;
		S::Result result;
		auto e = rig.Run(fit, result);
		CHECK(!e);
		CHECK(result.hypotheses.empty());
		Record("ambient_only", result, sun);
	}
	{
		Rig rig(false, true);
		rig.Illuminate(sun, V(), V());
		S::FitResult fit;
		CHECK(!rig.Fit(fit));
		CHECK(fit.proposals.empty());
		CHECK(fit.state == S::State::Unavailable);
	}
	{
		Rig rig;
		rig.Illuminate(sun, V(2000, 2000, 2000), V(1000, 1000, 1000));
		S::FitResult fit;
		S::Result result;
		CHECK(!rig.Run(fit, result));
		CHECK(result.hypotheses.empty());
		Record("saturated_sun", result, sun);
	}
	{
		Rig rig;
		rig.Illuminate(sun, V(110, 90, 65), V(20, 25, 35));
		rig.train->resize(2);
		S::FitResult fit;
		CHECK(!rig.Fit(fit));
		CHECK(fit.proposals.empty());
	}
	{
		Rig rig;
		rig.Illuminate(sun, V(110, 90, 65), V(20, 25, 35));
		rig.request.profiles = {B::ProfileHypothesis()};
		S::FitResult fit;
		CHECK(!rig.Fit(fit));
		CHECK(fit.proposals.empty());
	}
	{
		Rig rig;
		rig.Illuminate(sun, V(110, 90, 65), V(20, 25, 35));
		S::DirectionHint hint;
		hint.id = 14;
		hint.kind = S::HintKind::AuthoredSun;
		hint.toSource = sun.Float();
		hint.staticEligible = true;
		hint.provenance = "test q3map_sun";
		rig.request.hints = {hint};
		S::FitResult fit;
		S::Result result;
		CHECK(rig.Run(fit, result));
		Record("authored_matching", result, sun);
		hint.toSource = Unit(V(-1, -1, .1)).Float();
		rig.request.hints = {hint};
		CHECK(rig.Run(fit, result));
		CHECK(!result.hypotheses.empty());
		CHECK(Angle(V(result.hypotheses[0].fitted.toSource), sun) < 3 * Pi / 180);
		Record("authored_conflicting", result, sun);
	}
	{
		Rig rig;
		rig.Illuminate(sun, V(100, 100, 100), V(10, 10, 10));
		S::FitResult fit;
		Cancel cancel;
		ReadContext read = rig.read;
		read.cancellation = &cancel;
		rig.request.training = Lease(rig.train);
		CHECK(S::Fit(rig.request, read, fit).code == Code::Cancelled);
		CHECK(fit.proposals.empty());
		rig.request.options.maxRays = 1;
		CHECK(rig.Fit(fit).code == Code::WorkLimit);
		CHECK(fit.proposals.empty());
	}
	{
		Rig rig;
		rig.Illuminate(sun, V(100, 100, 100), V());
		rig.request.geometry.visibility.reset();
		S::FitResult fit;
		CHECK(rig.Fit(fit).code == Code::MissingGeometry);
		rig.request.geometry.visibility = rig.scene;
		(*rig.train)[0].receiver.world.x = std::numeric_limits<float>::infinity();
		CHECK(rig.Fit(fit).code == Code::InvalidInput);
	}
	{
		Rig rig;
		rig.Illuminate(sun, V(110, 90, 65), V(20, 25, 35));
		rig.request.geometry.visibility = std::make_shared<ClearWithoutSky>();
		S::FitResult fit;
		CHECK(!rig.Fit(fit));
		CHECK(fit.proposals.empty());
		CHECK(!fit.profileCoverage.empty());
		if (!fit.profileCoverage.empty())
			CHECK(fit.profileCoverage[0].excludedSky == rig.train->size());
	}
	{
		Rig rig;
		rig.Illuminate(sun, V(110, 90, 65), V(20, 25, 35));
		rig.Quad(Vec3(-20000, -20000, 300), Vec3(20000, -20000, 300), Vec3(20000, 20000, 300),
				 Vec3(-20000, 20000, 300), MaterialCutout);
		rig.Build();
		S::FitResult fit;
		CHECK(!rig.Fit(fit));
		CHECK(fit.proposals.empty());
	}
	{
		Rig rig;
		rig.Illuminate(sun, V(110, 90, 65), V(20, 25, 35));
		for (auto samples : {rig.train, rig.validation})
			for (auto &s : *samples)
				s.receiver.geometricNormal = s.receiver.shadingNormal = Vec3(0, 0, 1);
		S::FitResult fit;
		S::Result result;
		auto e = rig.Run(fit, result);
		for (const auto &h : result.hypotheses)
			CHECK(h.asset.suns.empty());
		Record("single_normal_insufficient", result, sun);
	}
	{
		Rig rig(true);
		rig.Illuminate(sun, V(), V(100, 100, 100)); // localized unmodelled reflected fill
		for (auto samples : {rig.train, rig.validation})
			for (auto &s : *samples)
			{
				V delta = V(0, 0, 500) - V(s.receiver.world);
				double radius = Length(delta);
				double response = 60e6 * std::max(0., Dot(V(s.receiver.geometricNormal), Unit(delta))) *
								  delta.z / (radius * radius * radius);
				for (int c = 0; c < 3; ++c)
					s.rgb[c] = uint8_t(Clamp(response * (c == 0 ? 1 : c == 1 ? .3 : .2), 0, 254));
			}
		S::FitResult fit;
		S::Result result;
		CHECK(!rig.Run(fit, result));
		CHECK(result.hypotheses.empty());
		Record("unmodelled_reflection", result, sun);
	}
	{
		Rig rig;
		rig.Illuminate(sun, V(110, 20, 10), V(10, 10, 10), V(), false, Unit(V(-.7, .8, 1)), V(10, 30, 110));
		S::FitResult fit;
		S::Result result;
		auto e = rig.Run(fit, result);
		CHECK(!e || result.state == S::State::AmbiguousSun);
		Record("multiple_coloured_suns", result, sun);
	}
	{
		Rig rig;
		rig.Illuminate(sun, V(110, 90, 65), V(20, 25, 35));
		auto alternate = rig.request.profiles[0];
		alternate.id = "explicit-assumed-alternative";
		alternate.compiler = B::CompilerIdentification::Unidentified;
		alternate.applicability = B::Applicability::AssumedForDataset;
		rig.request.profiles.push_back(alternate);
		S::FitResult fit;
		S::Result result;
		CHECK(rig.Run(fit, result));
		std::set<std::string> profiles;
		for (const auto &h : result.hypotheses)
			profiles.insert(h.fitted.profile.id);
		CHECK(profiles.size() == 2);
		Record("competing_assumed_profiles", result, sun);
	}
	{
		Rig rig;
		Decode::Decoder decoder;
		Decode::Raw raw;
		raw.bytes[0] = 255;
		raw.bytes[1] = 127;
		raw.bytes[2] = 0;
		Decode::Observation observation;
		CHECK(decoder.Run(raw, observation));
		S::Sample sample;
		CHECK(S::FromObservation((*rig.train)[0].receiver, observation, 16, 3, true, true, sample));
		CHECK(!sample.encodingVerifiedForProfile);
		CHECK(sample.rgb[0] == 255 && sample.rgb[1] == 127);
		observation.raw.directionData = true;
		CHECK(S::FromObservation((*rig.train)[0].receiver, observation, 16, 3, true, true, sample).code ==
			  Code::Unavailable);
		GridCell missing;
		auto h = S::GridHint(missing, 123);
		CHECK(!h.staticEligible);
		DeluxeDirection deluxe;
		CHECK(!S::DeluxeHint(deluxe, 124).staticEligible);
	}
	CapturedDirectionalEvidence();
	if (argc > 1)
	{
		std::ofstream out(argv[1]);
		out << std::setprecision(12) << "{\"checks\":" << checks << ",\"failures\":" << failures
			<< ",\"fixtures\":[";
		for (size_t i = 0; i < witnesses.size(); ++i)
		{
			const auto &w = witnesses[i];
			if (i)
				out << ',';
			out << "{\"name\":\"" << w.name << "\",\"state\":\"" << w.state
				<< "\",\"angular_error_degrees\":";
			if (w.angle >= 0)
				out << w.angle;
			else
				out << "null";
			out << ",\"interval_rmse\":";
			if (w.rmse >= 0)
				out << w.rmse;
			else
				out << "null";
			out<<",\"sun_rgb_relative_error\":";if(w.sunRgbRelativeError>=0)out<<w.sunRgbRelativeError;else out<<"null";
			out<<",\"sky_rgb_relative_error\":";if(w.skyRgbRelativeError>=0)out<<w.skyRgbRelativeError;else out<<"null";
			out << ",\"alternatives\":" << w.alternatives << ",\"validation_rays\":" << w.rays << '}';
		}
		out << "]}\n";
	}
	std::printf("S02 %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
