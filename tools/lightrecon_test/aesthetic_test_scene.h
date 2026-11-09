// Shared owned ASR20/21 fixture geometry and independent answer generator.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
struct AestheticRig
{
	Scene scene;
	A::Request request;
	std::vector<S::Observation> folds[3];
	AestheticRig() : scene(true)
	{
		request.model.law = S::Law::DiffuseIntegral;
		request.model.output = S::Output::RelativeIncident;
		request.model.allowAssumed = true;
		request.model.units.worldUnitsPerMeter = 1;
		request.model.provenance = "Owned relative modern integral, no historical source claim";
		request.targets.domain = A::Domain::RelativeLinearAttested;
		request.targets.material = 2;
		request.targets.comparableMaterialResponseAttested = true;
		request.targets.provenance = "Owned same receiver response";
		request.geometry.visibility = scene.visibility;
		request.geometry.geometryKey = scene.visibility->CertificationKey();
		request.scene.geometry = scene.visibility;
		request.scene.domain = scene.domain;
		request.scene.map = scene.map;
		request.scene.domainKey =
			Key("Owned declared complete empty volume plus captured visible surfaces");
		request.sampling.compareRefined = false;
		request.sampling.areaDivisions = 4;
		request.options.backgroundUpper = Vec3(.08, .08, .08);
		request.options.huber = .03;
		request.options.tolerance = 1e-10;
		request.options.maxWork = 300000000;
	}
	void Fixture(Id id, V p, bool spot = false)
	{
		auto c = spot ? Extended(id, EmitterKind::Spot, p) : Point(id, p);
		auto e = c.shape.initial;
		e.support.kind = spot ? SourceSupport::Kind::FreeSpot : SourceSupport::Kind::FreePoint;
		e.support.fixture = id;
		e.support.map = scene.map.sha256;
		e.support.geometry = request.geometry.geometryKey;
		e.support.provenance = "Explicit owned fixture location/support";
		A::Fixture f;
		f.variants = {e};
		f.upperPower = Vec3(12, 12, 12);
		f.provenance = "Fixed supplied credible location";
		request.fixtures.push_back(f);
	}
	void Data(std::vector<Vec3> powers, Vec3 background = Vec3(.03, .02, .01), bool outlier = false)
	{
		std::vector<S::Emitter> truth;
		for (size_t j = 0; j < powers.size(); ++j)
			truth.push_back(request.fixtures[j].variants.back());
		for (unsigned fold = 0; fold < 3; ++fold)
		{
			auto receivers = std::make_shared<std::vector<Bake::Receiver>>();
			for (unsigned i = 0; i < 5; ++i)
				for (unsigned j = 0; j < 4; ++j)
				{
					Bake::Receiver r;
					r.sample = 1 + fold * 1000 + i * 4 + j;
					r.material = 2;
					r.world = Vec3(-2.f + i, -2.f + j + fold * .24f, .05f);
					r.geometricNormal = r.shadingNormal = Vec3(0, 0, 1);
					r.mappedPlanar = r.compilerPlacementCertified = true;
					receivers->push_back(r);
				}
			std::shared_ptr<const S::Block> block;
			auto sampling = request.sampling;
			sampling.areaDivisions = 16;
			Must(S::Prepare(request.model, truth, Lease(receivers), request.geometry, sampling,
							Read(), block));
			std::vector<S::Prediction> p;
			S::Work w;
			Must(block->Evaluate(powers, Read(), p, w));
			for (size_t i = 0; i < p.size(); ++i)
			{
				CHECK(p[i].available);
				S::Observation a;
				a.receiver = (*receivers)[i];
				a.id = a.receiver.sample;
				a.spatialBlock = 1 + fold * 1000 + i / 4;
				a.role = S::Role(fold);
				a.comparable = true;
				Vec3 value = ((V(p[i].value) + V(background)) * request.targets.gain).Float();
				if (request.targets.domain == A::Domain::ForwardEncodedRgb8)
				{
					uint8_t b[3];
					Must(Bake::Encode(request.targets.encoding, value, value, b));
				}
				for (unsigned c = 0; c < 3; ++c)
					a.channel[c].lower = a.channel[c].upper = V(value)[c];
				if (outlier && fold == 0 && i == 0)
					for (auto &c : a.channel)
						c.lower = c.upper = 20;
				folds[fold].push_back(a);
			}
		}
	}
	A::Experiment Fit()
	{
		A::Experiment e;
		Must(A::FitAndValidate(request, folds[0], folds[1], Read(), e));
		return e;
	}
	void MountedPanel()
	{
		scene.Quad(V(-1, -.5, 1.2), V(-1, .5, 1.2), V(1, .5, 1.2), V(1, -.5, 1.2), MaterialGlow);
		scene.surfaces.back().materialFacts.cull = SurfaceCull::Back;
		SurfaceCapture capture;
		capture.Begin(BuildPolicy::ForceRebuild);
		Rebuild(scene);
		Must(capture.SetMap(scene.map));
		for (auto s : scene.surfaces)
			Must(capture.Add(s));
		Must(capture.Freeze());
		Must(
			StaticVisibility::Build(capture.Acquire(), {}, Read(), scene.visibility, scene.domain));
		request.geometry.visibility = scene.visibility;
		request.geometry.geometryKey = scene.visibility->CertificationKey();
		request.scene.geometry = scene.visibility;
		request.scene.map = scene.map;
		request.scene.originalSurfaces = capture.Acquire();
		ShapeSelection::Request selection;
		selection.scene = request.scene;
		selection.fixture = 99;
		selection.emittingSurfaces = {uint32_t(scene.surfaces.size() - 1)};
		selection.fullSurfaceEmissionAttested = true;
		selection.emissionProvenance = "Owned exact uniformly luminous panel";
		ShapeSelection::Result ranked;
		Must(ShapeSelection::Rank(selection, Read(), ranked));
		for (const auto &rival : ranked.ranked)
			if (rival.kind == EmitterKind::Rectangle &&
				rival.status == ShapeSelection::Status::Supported)
			{
				A::Fixture f;
				f.variants = {rival.emitter};
				f.upperPower = Vec3(12, 12, 12);
				f.provenance = "Owned fixed mounted panel";
				request.fixtures = {f};
				return;
			}
		throw std::runtime_error("Owned panel admission unavailable");
	}
};
