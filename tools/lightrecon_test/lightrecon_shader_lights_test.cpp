// Complete shader catalogue, numerical independence and canonical cache replay.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/shader_lights.h"
#include "producer_test_scene.h"
#include <cstdio>
#include <limits>
using namespace JointTest;
using namespace ProducerTest;
namespace SL = Rend2LightRecon::Core::ShaderLights;
int checks = 0, failures = 0;
#define CHECK(x)                                                                                   \
	do                                                                                             \
	{                                                                                              \
		++checks;                                                                                  \
		if (!(x))                                                                                  \
		{                                                                                          \
			++failures;                                                                            \
			std::printf("FAIL %d %s\n", __LINE__, #x);                                             \
		}                                                                                          \
	} while (0)
BakedSurface Quad(uint32_t id)
{
	BakedSurface s;
	s.ordinal = id;
	s.kind = BakedSurfaceKind::Face;
	s.motion = SurfaceMotion::StaticWorld;
	s.material = "textures/authored/panel";
	s.materialFacts.available = true;
	s.materialFacts.cull = SurfaceCull::Front;
	s.materialFacts.legacySurfaceLight = 2000;
	s.materialFacts.emissionTexture = "textures/authored/panel_glow";
	s.lightmapBasis[2] = Vec3(0, 0, 1);
	s.styles[0] = 0;
	for (auto uv : {LightmapUv(0, 0), LightmapUv(1, 0), LightmapUv(0, 1), LightmapUv(1, 1)})
	{
		BakedVertex v;
		v.position = Vec3(float(uv.u * 16), float(uv.v * 8), float(id * 4));
		v.shadingNormal = Vec3(0, 0, 1);
		v.textureUv = uv;
		s.vertices.push_back(v);
	}
	s.indices = {0, 2, 1, 1, 2, 3};
	return s;
}
std::shared_ptr<const SurfaceGeometry> Geometry(const LightmapMapIdentity &map,
												std::vector<BakedSurface> surfaces)
{
	SurfaceCapture c;
	c.Begin(BuildPolicy::ForceRebuild);
	CHECK(c.SetMap(map));
	for (auto &s : surfaces)
		CHECK(c.Add(s));
	CHECK(c.Freeze());
	return c.Acquire();
}
int main(int argc, char **argv)
{
	Rig rig;
	auto request = Request(rig);
	auto a = Quad(0), triangle = Quad(1), patch = Quad(2), moving = Quad(3), sky = Quad(4),
		 glow = Quad(5);
	triangle.kind = BakedSurfaceKind::Triangles;
	triangle.vertices.resize(3);
	triangle.indices = {0, 1, 2};
	patch.kind = BakedSurfaceKind::Patch;
	patch.vertices.clear();
	patch.indices.clear();
	patch.patchWidth = patch.patchHeight = 3;
	for (unsigned y = 0; y < 3; ++y)
		for (unsigned x = 0; x < 3; ++x)
		{
			BakedVertex v;
			v.position = Vec3(float(x), float(y), float(x * y));
			v.shadingNormal = Vec3(0, 0, 1);
			v.textureUv = LightmapUv(x * .5, y * .5);
			patch.vertices.push_back(v);
		}
	moving.model = 2;
	moving.motion = SurfaceMotion::BrushModel;
	sky.materialFacts.signals |= MaterialSky;
	glow.materialFacts.legacySurfaceLight = 0;
	glow.materialFacts.signals |= MaterialGlow;
	auto geometry = Geometry(request.map, {a, triangle, patch, moving, sky, glow});
	CHECK(geometry);
	SL::Catalogue catalogue;
	CHECK(SL::Extract(*geometry, Read(), catalogue));
	CHECK(catalogue.surfaces.size() == 5);
	request.shaderGeometry = geometry;
	request.fitting.profiles.clear();
	request.options.maxSources = 1;
	P::Result out;
	auto error = P::Run(request, Read(), out);
	if (!error)
		std::printf("Run: %s\n", error.message.c_str());
	CHECK(error);
	CHECK(out.state == P::State::DiagnosticOnly);
	CHECK(out.artifact.asset);
	if (!out.artifact.asset)
		return 1;
	auto &asset = *out.artifact.asset;
	CHECK(asset.emitters.size() == 5);
	CHECK(asset.assessment.conditionalSources == 5);
	CHECK(asset.assessment.resolvedSources == 0);
	CHECK(!asset.assessment.solverExecuted);
	CHECK(asset.emitters[0].kind == EmitterKind::Rectangle);
	CHECK(asset.emitters[0].geometry.halfWidthWorld == 8);
	CHECK(asset.emitters[0].frame.forward.z == 1);
	for (unsigned i = 1; i < 5; ++i)
		CHECK(asset.emitters[i].kind == EmitterKind::Extension);
	CHECK(P::ValidateArtifact(out.artifact, Read()));
	for (unsigned i = 0; i < 5; ++i)
	{
		SL::Surface s;
		auto &e = asset.emitters[i];
		CHECK(SL::IsShaderSource(asset, e));
		CHECK(SL::Read(asset, out.artifact.chunks, e, Read(), s));
		CHECK(s.ordinal == i);
		CHECK(s.legacySurfaceLight == 2000);
		CHECK(s.origin == SL::Origin::Shader);
		CHECK(s.material == a.material);
		CHECK(!e.emission.intensity.known);
		CHECK(e.source.evidence.exactLight == Suitability::Unknown);
		if (i == 2)
		{
			CHECK(s.type == BakedSurfaceKind::Patch);
			CHECK(s.patchWidth == 3);
			CHECK(s.vertices[8].position.z == 4);
		}
		if (i == 3)
		{
			CHECK(s.motion == SurfaceMotion::BrushModel);
			CHECK(s.model == 2);
			CHECK(!e.position.known);
		}
		if (i == 4)
			CHECK(s.materialSignals & MaterialSky);
	}
	std::vector<uint8_t> bytes;
	CHECK(Disk::Encode(asset, out.artifact.fingerprint, out.artifact.chunks, bytes));
	if (argc > 1)
	{
		Disk::Cache cache(argv[1]);
		CHECK(P::Store(out.artifact, cache, Read()));
		P::Artifact loaded;
		CHECK(P::Load(cache, out.artifact.fingerprint, Read(), loaded));
		CHECK(loaded.asset && loaded.asset->emitters.size() == 5);
		CHECK(P::ValidateArtifact(loaded, Read()));
		std::vector<uint8_t> replay;
		CHECK(Disk::Encode(*loaded.asset, loaded.fingerprint, loaded.chunks, replay));
		CHECK(bytes == replay);
	}
	// An unchanged repeated build is byte deterministic, with stable source IDs.
	P::Result repeated;
	CHECK(P::Run(request, Read(), repeated));
	std::vector<uint8_t> repeat;
	CHECK(Disk::Encode(*repeated.artifact.asset, repeated.artifact.fingerprint,
					   repeated.artifact.chunks, repeat));
	CHECK(bytes == repeat);
	// Changed shader emission must invalidate the producer key, even with the same BSP.
	a.materialFacts.legacySurfaceLight = 3000;
	auto changed = request;
	changed.shaderGeometry = Geometry(request.map, {a});
	Disk::Digest oldKey, newKey;
	CHECK(Disk::MakeKey(P::DependencyFingerprint(request), oldKey));
	CHECK(Disk::MakeKey(P::DependencyFingerprint(changed), newKey));
	CHECK(oldKey != newKey);
	auto wrong = request;
	auto wrongMap = request.map;
	wrongMap.sha256[0] ^= 1;
	wrong.shaderGeometry = Geometry(wrongMap, {a});
	CHECK(!P::Run(wrong, Read(), repeated));
	CHECK(!repeated.artifact.asset);
	auto corrupt = out.artifact;
	corrupt.chunks[0].bytes.back() ^= 1;
	CHECK(!P::ValidateArtifact(corrupt, Read()));
	corrupt = out.artifact;
	corrupt.chunks.erase(corrupt.chunks.begin());
	CHECK(!P::ValidateArtifact(corrupt, Read()));
	corrupt = out.artifact;
	auto altered = std::make_shared<ReconstructedLightingAsset>(asset);
	altered->emitters[0].geometry.halfWidthWorld += 1;
	corrupt.asset = altered;
	CHECK(!P::ValidateArtifact(corrupt, Read()));
	// Neither contradictory side evidence nor an unknown model transform is
	// silently converted into a geometrically plausible world-space rectangle.
	std::vector<BakedSurface> ambiguous;
	for (unsigned i = 0; i < 7; ++i)
	{
		auto surface = Quad(i);
		if (i < 6)
			surface.materialFacts.legacySurfaceLight = 0;
		else
			surface.vertices[0].shadingNormal = Vec3(0, 0, -1);
		ambiguous.push_back(surface);
	}
	SL::Catalogue uncertain;
	CHECK(SL::Extract(*Geometry(request.map, ambiguous), Read(), uncertain));
	auto retained = asset;
	auto retainedChunks = out.artifact.chunks;
	CHECK(SL::Append(uncertain, retained, retainedChunks, Read()));
	CHECK(retained.emitters.back().kind == EmitterKind::Extension);
	auto count = retained.emitters.size();
	CHECK(!SL::Append(uncertain, retained, retainedChunks, Read()));
	CHECK(retained.emitters.size() == count);
	auto tiny = Read();
	tiny.maxBytes = 16;
	CHECK(!SL::Extract(*geometry, tiny, catalogue));
	struct Stop : Cancellation
	{
		bool IsCancelled() const noexcept override { return true; }
	} stop;
	auto cancelled = Read();
	cancelled.cancellation = &stop;
	CHECK(SL::Extract(*geometry, cancelled, catalogue).code == Code::Cancelled);
	// Numerical reconstruction and shader retention coexist without refitting the
	// authored surfaces or changing the numerical candidates/selected amplitudes.
	auto point = Point(10, V(-.6, 0, .8));
	rig.request.candidates = {point};
	rig.Observations(rig.Truth({Answer(point, V(2.5, .3, .5))}));
	Validation validation(rig);
	Final final(rig);
	auto numerical = Request(rig);
	P::Result fitted;
	CHECK(P::Run(numerical, Read(), fitted, &validation, &final));
	CHECK(fitted.state == P::State::Completed);
	numerical.shaderGeometry = geometry;
	P::Result combined;
	CHECK(P::Run(numerical, Read(), combined, &validation, &final));
	CHECK(combined.state == P::State::Completed);
	CHECK(combined.artifact.asset &&
		  combined.artifact.asset->emitters.size() == fitted.artifact.asset->emitters.size() + 5);
	CHECK(combined.final.score.intervalMse == fitted.final.score.intervalMse);
	CHECK(P::ValidateArtifact(combined.artifact, Read()));
	std::printf("shader lights: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
