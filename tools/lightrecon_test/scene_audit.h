// Trusted map controller; all numerical search is production C++.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lightrecon/scene_schedule.h"
namespace SC = Rend2LightRecon::Core::SceneSchedule;
struct SceneControl final : SC::Validation, SC::FinalEvaluation
{
	std::array<std::shared_ptr<Rows<J::Observation>>, 3> rows;
	Disk::Digest validation{}, heldout{};
	bool final = false;
	mutable uint64_t calls = 0;
	Disk::Digest Key() const override
	{
		return final ? heldout : validation;
	}
	Error Assess(const J::Request &q, const J::Model &m, const ReadContext &r,
				 J::Score &s) const override
	{
		++calls;
		return J::EvaluateFrozen(q, m, rows[1], rows[1]->v->size(), J::Role::Validation, validation,
								 r, s);
	}
	Error Assess(const J::Request &q, const J::Model &m, const ReadContext &r,
				 P::FinalReceipt &s) const override
	{
		++calls;
		s.evidence = "Independent frozen stock observations; physical source truth and historical "
					 "compiler unknown";
		return J::EvaluateFrozen(q, m, rows[2], rows[2]->v->size(), J::Role::Heldout, heldout, r,
								 s.score);
	}
	bool BlockBounds(Id block, Bounds &out) const override
	{
		out = {};
		for (const auto &r : *rows[1]->v)
			if (r.block == block)
			{
				if (!out.known)
				{
					out.known = true;
					out.minWorld = out.maxWorld = r.receiver.world;
				}
				else
				{
					out.minWorld = Vec3(std::min(out.minWorld.x, r.receiver.world.x),
										std::min(out.minWorld.y, r.receiver.world.y),
										std::min(out.minWorld.z, r.receiver.world.z));
					out.maxWorld = Vec3(std::max(out.maxWorld.x, r.receiver.world.x),
										std::max(out.maxWorld.y, r.receiver.world.y),
										std::max(out.maxWorld.z, r.receiver.world.z));
				}
			}
		return out.known;
	}
};
static Error SceneAttempt(P::Request request, const std::shared_ptr<Hidden::Request> &hidden,
						  const SamplingSelection *sampling,
						  const std::array<std::shared_ptr<Rows<J::Observation>>, 3> &rows,
						  const std::array<Disk::Digest, 3> &keys, const ReadContext &read,
						  const std::string &output, const std::string &name,
						  P::Result &publication)
{
	SC::Request input;
	request.hidden.reset();
	request.solar.reset();
	request.solarValidation = nullptr;
	request.coverage.limitations.push_back("X11 conditional local-direct branch: unknown sun, sky "
										   "and GI remain unmodelled, never verified zero");
	auto &o = request.fitting.options;
	o.maxSources = 8;
	o.maxCandidates = 32;
	o.maxModels = 12;
	o.rounds = 1;
	o.beamWidth = 2;
	o.amplitudeSweeps = 2;
	o.amplitudeLineSteps = 8;
	o.jointSweeps = 1;
	o.geometrySweeps = 1;
	o.geometry = o.materials = o.residualSearch = o.mergeSplit = false;
	o.maxEvaluations = 12000;
	o.maxRays = 400000;
	o.maxWork = 80000000;
	o.timeLimitMilliseconds = 90000;
	o.coarseRows = 64;
	// Preserve every independent evidence block at the coarse stage. Geometry
	// determines this minimum; no validation/held-out RGB enters the setting.
	std::set<std::tuple<unsigned, Id, Id>> evidenceBlocks;
	for (const auto &row : *rows[0]->v)
		evidenceBlocks.emplace(unsigned(row.channel), row.region, row.block);
	o.coarseRows = std::max(o.coarseRows, evidenceBlocks.size());
	o.diagnosticSources = 4;
	o.diagnosticRows = 64;
	auto &u = request.options.uncertainty;
	u.positionLevels = u.colourProfiles = u.deleteBlocks = u.subsampleReplicates = u.restarts = 0;
	u.materialProfiles = false;
	u.maxAttempts = 64;
	u.maxRefits = 64;
	u.maxRays = 500000;
	u.maxWork = 100000000;
	u.maxEvaluations = 24000;
	input.branches = {request};
	input.discovery = *hidden;
	input.discovery.areas.clear();
	if (sampling && sampling->localTraining)
		input.discovery.training = Lease(sampling->localTraining);
	input.discovery.options.coarseDivisions = 2;
	input.discovery.options.maxNodesPerArea = 8;
	input.discovery.options.refinementRounds = 0;
	input.discovery.options.spots = false;
	input.discovery.options.retainedPerChannelProfile = 1;
	input.discovery.options.maxRays = 8192;
	input.discovery.options.maxWork = 1000000;
	input.discovery.options.maxForwardCalls = 256;
	input.limits.maxUnits = 4096;
	input.limits.samplesPerUnit = 48;
	input.limits.candidatesPerUnit = 3;
	input.limits.maxCatalogCandidates = 128;
	input.limits.newCandidatesPerWave = 2;
	input.limits.wavesPerBranch = 4;
	input.limits.maxBytes = 384ull << 20;
	input.limits.maxRays = 4000000;
	input.limits.maxWork = 500000000;
	input.limits.maxForwardCalls = 250000;
	input.limits.wallMilliseconds = 240000;
	input.limits.publish = true;
	input.observationPoolComplete = sampling ? sampling->plan.policy.sourcePoolComplete : false;
	SceneControl validation, final;
	validation.rows = final.rows = rows;
	validation.validation = final.validation = keys[1];
	validation.heldout = final.heldout = keys[2];
	final.final = true;
	SC::Result result;
	Error error;
	do
	{
		auto checkpoint = result.checkpoint;
		auto cache = result.cache;
		error = SC::Run(input, read, result, &validation, &final, checkpoint, cache, 32);
		std::ofstream progress(output + "/scheduler-" + name + "-progress.json");
		progress << SC::ReportJson(result);
		if (!error)
			break;
	} while (std::any_of(result.units.begin(), result.units.end(),
						 [](const SC::Unit &u) { return u.status == SC::Status::Pending; }));
	std::ofstream report(output + "/scheduler-" + name + ".json");
	report << SC::ReportJson(result);
	std::ofstream controller(output + "/scheduler-" + name + "-controller.json");
	controller << "{\"validation_calls\":" << validation.calls << ",\"final_calls\":" << final.calls
			   << ",\"sample_counts\":[" << rows[0]->v->size() << ',' << rows[1]->v->size() << ','
			   << rows[2]->v->size()
			   << "],\"direct_only_conditional\":true,"
				  "\"geometry_refinement_enabled\":false,\"source_count_proposals_enabled\":true,"
				  "\"shadow_weight\":"
			   << request.fitting.shadowWeight << ",\"coarse_rows\":" << o.coarseRows
			   << ",\"training_evidence_blocks\":" << evidenceBlocks.size()
			   << ",\"canonical_asset_available\":"
			   << (result.publication.artifact.asset ? "true" : "false")
			   << ",\"uncertainty_attempts\":" << result.publication.uncertainty.attempts.size()
			   << '}';
	std::ofstream graph(output + "/scheduler-" + name + "-units.tsv");
	graph << "unit\tprofile\tstatus\ttraining\tcandidates\tcomplete\tx0\ty0\tz0\tx1\ty1\tz1\trays\t"
			 "work\n";
	for (const auto &unit : result.units)
		graph << unit.id << '\t' << Disk::Hex(unit.profile) << '\t' << SC::StatusName(unit.status)
			  << '\t' << unit.training.size() << '\t' << unit.candidates << '\t'
			  << unit.searchComplete << '\t' << unit.area.receivers.minWorld.x << '\t'
			  << unit.area.receivers.minWorld.y << '\t' << unit.area.receivers.minWorld.z << '\t'
			  << unit.area.receivers.maxWorld.x << '\t' << unit.area.receivers.maxWorld.y << '\t'
			  << unit.area.receivers.maxWorld.z << '\t' << unit.rays << '\t' << unit.work << '\n';
	publication = result.publication;
	if (!publication.artifact.asset)
	{
		publication.state = error ? P::State::DiagnosticOnly : P::State::Failed;
		publication.reason = result.reason;
		publication.stages.push_back("X11 bounded scene orchestration; see scheduler report for "
									 "searched/partial/deferred coverage");
		if (result.preferred != SIZE_MAX)
			publication.fitted = result.branches[result.preferred].fit;
		else if (!result.branches.empty())
			publication.fitted = result.branches[0].fit;
	}
	return error;
}
