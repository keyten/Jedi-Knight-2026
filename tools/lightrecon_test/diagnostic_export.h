// Bounded V03 trusted-controller export; no final-heldout feedback into fitting.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lightrecon/diagnostics.h"
namespace DD = Rend2LightRecon::Core::Diagnostics;
static void DiagnosticWrite(const DD::Scene &scene, const std::string &stem)
{
	std::string text;
	Check(DD::Encode(scene, text));
	std::ofstream binary(stem + ".jkdbg", std::ios::binary);
	binary.write(text.data(), text.size());
	std::ofstream json(stem + ".json"), csv(stem + ".csv"), obj(stem + ".obj");
	Check(DD::Json(scene, json));
	Check(DD::Csv(scene, csv));
	Check(DD::Obj(scene, obj));
	DD::Heatmap scale;
	scale.maximum = 32;
	scale.signedValues = true;
	if (DD::HeatmapPpm(scene.records, DD::Layer::Residual, 32, scale, text))
	{
		std::ofstream heat(stem + "-interval-residual.ppm", std::ios::binary);
		heat.write(text.data(), text.size());
	}
}
static void ExportDiagnostics(const J::Request &q, const P::Result &result, DD::Scene &scene,
							  const std::vector<DynamicBoundary> &dynamic, const ReadContext &read,
							  const std::string &stem)
{
	DD::Builder b(scene);
	for (auto &d : dynamic)
		b.Dynamic(d);
	const auto &catalog =
		result.fitted.checkpoint.catalog.empty() ? q.candidates : result.fitted.checkpoint.catalog;
	for (const auto &c : catalog)
	{
		auto emitter = c.shape.initial.geometry;
		emitter.source.id = c.id;
		b.Emitter(emitter, DD::State::Deferred,
				  q.profiles.empty() ? "Unknown" : q.profiles[0].model.profile.id,
				  "offered candidate; not accepted original source; provider=" + c.provider + "; " +
					  c.provenance);
	}
	for (size_t row = 0; row < std::min<uint64_t>(q.count, 128); ++row)
	{
		Chunk<J::Observation> span;
		Check(q.training->Read(row, 1, read, span));
		const auto &o = span.data[0];
		DD::Record r;
		r.id = r.sample = o.id;
		r.region = o.region;
		r.layer = DD::Layer::Receivers;
		r.a = o.receiver.world;
		r.b = (V(o.receiver.world) + V(o.receiver.geometricNormal) * 6).Float();
		r.primitive = DD::Primitive::Line;
		r.geometryKnown = true;
		r.state = DD::State::Unknown;
		r.quantity = "receiver-normal-and-preflight";
		r.reason =
			"training block=" + std::to_string(o.block) + "; operator/transfer support unknown";
		if (result.fitted.receiverSupport)
		{
			auto entry = result.fitted.receiverSupport->Find(o.id);
			if (entry)
			{
				r.state = entry->common ? DD::State::Accepted : DD::State::Unavailable;
				r.issues = entry->uncertainty;
				for (auto &d : entry->profiles)
					r.reason += ";" + std::string(ReceiverSupport::ReasonName(d.reason));
			}
		}
		r.domain = "profile-specific-encoded-target-interval";
		r.quantity = "encoded-target-lower-bound; RGB8 is not physical irradiance";
		r.unit = PhotometricUnit::Relative;
		r.valueKnown = o.rgb[0].present && o.rgb[1].present && o.rgb[2].present;
		r.lowerKnown = r.valueKnown;
		r.upperKnown =
			r.valueKnown && !o.rgb[0].censored && !o.rgb[1].censored && !o.rgb[2].censored;
		r.value = r.lower =
			Vec3(float(o.rgb[0].lower), float(o.rgb[1].lower), float(o.rgb[2].lower));
		// Censored +infinity is semantic, never a nonfinite numerical payload.
		r.upper = Vec3(float(o.rgb[0].censored ? o.rgb[0].lower : o.rgb[0].upper),
					   float(o.rgb[1].censored ? o.rgb[1].lower : o.rgb[1].upper),
					   float(o.rgb[2].censored ? o.rgb[2].lower : o.rgb[2].upper));
		for (unsigned ch = 0; ch < 3; ++ch)
			if (o.rgb[ch].censored)
				r.reason += ";censored-channel=" + std::to_string(ch);
		b.Add(r);
	}
	if (q.transportMesh)
		for (const auto &e : q.transportMesh->Elements())
		{
			const auto &p = e.patch;
			DD::Record r;
			r.id = p.id;
			r.layer = DD::Layer::Transport;
			r.state = DD::State::Conditional;
			r.geometryKnown = true;
			r.a = p.receiver.world;
			r.quantity = "passive-reflecting-area";
			r.domain = "world-area-not-observation-weight";
			r.reason = "disjoint transport mesh; geometry support does not establish albedo/GI";
			r.valueKnown = true;
			r.value = Vec3(float(p.areaWorldSquared), 0, 0);
			b.Add(r);
			for (size_t j = 0; j < p.receiver.footprint.count; ++j)
			{
				const auto &t = p.receiver.footprint.data[j];
				r.primitive = DD::Primitive::Triangle;
				r.a = t.world[0];
				r.b = t.world[1];
				r.c = t.world[2];
				b.Add(r);
			}
		}
	// Independent diagnostic work, not solver acceptance. No additional fitting.
	if (result.artifact.asset)
	{
		auto ledger = DD::FrozenObservations(result.artifact, read, scene);
		if (!ledger)
		{
			DD::Record r;
			r.id = 1;
			r.layer = DD::Layer::Residual;
			r.state = DD::State::Unavailable;
			r.reason = ledger.message;
			b.Add(r);
		}
		auto contributions = DD::Contributions(result.artifact, read, scene);
		if (!contributions)
		{
			DD::Record r;
			r.id = 1;
			r.layer = DD::Layer::Unresolved;
			r.state = DD::State::Unavailable;
			r.reason = contributions.message;
			b.Add(r);
		}
	}
	auto pairs = DD::TrainingPairs(q, catalog, 2, 16, read, scene);
	if (!pairs)
	{
		DD::Record r;
		r.id = 1;
		r.layer = DD::Layer::Direct;
		r.state = DD::State::Unavailable;
		r.reason = pairs.message;
		b.Add(r);
	}
	const size_t beforeMissing = scene.records.size();
	for (size_t j = 0; j < beforeMissing; ++j)
		if (scene.records[j].layer == DD::Layer::Direct && !scene.records[j].valueKnown)
		{
			auto missing = scene.records[j];
			missing.layer = DD::Layer::Residual;
			missing.quantity = "signed-observation-model-discrepancy";
			missing.domain = "declared-historical-encoded-observation";
			missing.reason += "; missing prediction is not successful darkness";
			b.Add(missing);
		}
	if (q.geometry.visibility)
	{
		size_t sources = 0;
		for (auto &c : catalog)
		{
			if (c.root != TransportRootKind::LocalEmitter || sources++ >= 2)
				continue;
			for (size_t row = 0; row < std::min<uint64_t>(q.count, 8); ++row)
			{
				Chunk<J::Observation> span;
				Check(q.training->Read(row, 1, read, span));
				auto &o = span.data[0];
				RayQuery ray;
				ray.startWorld = o.receiver.world;
				ray.endWorld = c.shape.initial.geometry.position.world;
				ray.originSurface = o.receiver.surface;
				ray.maxWork = 100000;
				RayResult hit;
				auto e = q.geometry.visibility->Query(ray, read, hit);
				if (e)
					b.Ray(o.id, c.id, ray.startWorld, ray.endWorld, hit,
						  "explicit diagnostic centre segment; NOT finite-source penumbra or "
						  "source feasibility");
				else
					++scene.invalidQueries;
			}
		}
	}
	DiagnosticWrite(scene, stem);
	Manager owner;
	if (result.artifact.asset)
		owner.Publish(*result.artifact.asset, Completeness::Partial);
	std::ofstream report(stem + "-report.json"), human(stem + "-report.txt");
	Check(DD::Report(owner.Acquire(), nullptr, report, &human, &scene));
}
