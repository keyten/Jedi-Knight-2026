// Shared trusted owned-scene producer adapter; no answers enter Producer::Run.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "joint_test_scene.h"
#include "lightrecon/producer.h"
namespace ProducerTest
{
using namespace JointTest;
namespace P = Rend2LightRecon::Core::Producer;
class Validation final : public J::Validation
{
	const Rig &rig;

  public:
	mutable unsigned calls = 0;
	explicit Validation(const Rig &r) : rig(r) {}
	Disk::Digest Key() const override
	{
		return JointFixture::Key("S13 independent spatial validation fold v1");
	}
	Error Evaluate(const J::Model &m, const ReadContext &read, J::Score &score) const override
	{
		++calls;
		return J::EvaluateFrozen(rig.request, m, rig.validation, rig.validation->data->size(),
								 J::Role::Validation, Key(), read, score);
	}
};
class Final final : public P::FinalEvaluation
{
	const Rig &rig;

  public:
	mutable unsigned calls = 0;
	bool fail = false;
	explicit Final(const Rig &r) : rig(r) {}
	Disk::Digest Key() const override { return JointFixture::Key("S13 untouched final spatial fold v1"); }
	Error Assess(const J::Model &m, const ReadContext &read, P::FinalReceipt &out) const override
	{
		++calls;
		if (fail)
			return Failure(Code::Unavailable, "Trusted final evaluator rejected incomplete predictions");
		out.evidence =
			"Controlled owned scene; fixed or declared candidate priors, no stock XYZ ground truth";
		return J::EvaluateFrozen(rig.request, m, rig.held, rig.held->data->size(), J::Role::Heldout, Key(),
								 read, out.score);
	}
};
P::Request Request(Rig &rig)
{
	P::Request r;
	r.map = rig.scene.map;
	r.fitting = rig.request;
	r.validationKey = Key("S13 independent spatial validation fold v1");
	r.heldoutKey = Key("S13 untouched final spatial fold v1");
	r.metadata.lineage = "owned/S13/" + Disk::Hex(r.map.sha256);
	r.metadata.revision = "owned-generation-v1";
	r.metadata.units = rig.request.profiles[0].model.units;
	r.metadata.provenance = 1;
	Provenance p;
	p.id = 1;
	p.kind = ProvenanceKind::External;
	p.producer = "S13 independent controlled fixture controller/1";
	p.contentHash = Disk::Hex(r.map.sha256);
	r.metadata.provenanceRecords = {p};
	r.decoder = "Specified controlled relative integral; no historical compiler assertion";
	r.settingsIdentity = "S13 frozen compact scene work/coverage/admission policy v1";
	r.coverage.primariesKnown = r.coverage.sourceTransferKnown = r.coverage.derivedComplete =
		r.coverage.rawComplete = true;
	r.coverage.geometryComplete = true;
	r.coverage.mappedSamples = rig.scene.patches.size();
	r.coverage.mappedAreaWorldSquared.known = true;
	for (const auto &patch : rig.scene.patches)
		r.coverage.mappedAreaWorldSquared.value += float(patch.areaWorldSquared);
	r.options.uncertainty.positionLevels = 0;
	r.options.uncertainty.colourProfiles = 0;
	r.options.uncertainty.deleteBlocks = 0;
	r.options.uncertainty.subsampleReplicates = 0;
	r.options.uncertainty.restarts = 0;
	r.options.uncertainty.materialProfiles = false;
	r.options.uncertainty.trainingRegret = r.options.uncertainty.validationRegret = .01;
	r.fingerprint.units = r.metadata.units;
	r.fingerprint.complete.fill(true);
	for (auto kind : {Disk::InputKind::Bsp, Disk::InputKind::Settings, Disk::InputKind::UnitCalibration})
	{
		Disk::InputContent c;
		c.kind = kind;
		c.name = kind == Disk::InputKind::Bsp ? r.map.path : "owned/" + std::to_string(unsigned(kind));
		c.bytes = kind == Disk::InputKind::Bsp ? r.map.bytes : c.name.size();
		c.sha256 = kind == Disk::InputKind::Bsp ? r.map.sha256 : Key(c.name);
		r.fingerprint.inputs.push_back(c);
	}
	return r;
}

} // namespace ProducerTest
