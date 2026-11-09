// Owned ASR22 witnesses. Hidden answers are used only by the observation generator/audit.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "iterative_discovery.h"
#include "joint_test_scene.h"
#include <fstream>
#include <iomanip>
#include <sstream>
using namespace JointTest;
namespace D = Rend2LightRecon::Discovery;
int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL %d: %s\n", __LINE__, #x); } } while (0)

J::Candidate SupportedPoint(Rig &rig, Id id, V position, bool spot = false) {
	auto c = spot ? Extended(id, EmitterKind::Spot, position) : Point(id, position);
	c.region = 1;
	c.shape.initial.support.kind = spot ? SourceSupport::Kind::FreeSpot : SourceSupport::Kind::FreePoint;
	c.shape.initial.support.fixture = id;
	c.shape.initial.support.map = rig.scene.map.sha256;
	c.shape.initial.support.geometry = rig.request.geometry.geometryKey;
	c.shape.initial.support.provenance = "Owned free emitter hypothesis in captured fixture volume";
	c.provenance = "Geometry-only hidden fixture search prior; existence and RGB withheld";
	return c;
}
D::Request Policy(Rig &rig, const std::vector<J::Candidate> &candidates,
	const std::vector<J::Source> &initial = {}) {
	D::Request r;
	r.fit = rig.request;
	r.fit.options.maxObservations = 512;
	r.fit.options.maxBytes = 64ull << 20;
	r.fit.options.maxWork = 100000000;
	r.fit.options.maxEvaluations = 5000;
	r.fit.options.maxRays = 2000000;
	r.fit.options.maximumRequiredBlockRmse = 0;
	r.fit.options.amplitudeSweeps = 12;
	r.fit.options.amplitudeLineSteps = 24;
	r.fit.options.complexityPerSource = 0;
	r.fit.options.complexityPerParameter = 0;
	r.fit.options.worstBlockWeight = 0;
	r.fit.profiles[0].contract.physicalTransportAssumed = true;
	r.fit.candidates.clear(); // candidates do not enter an automatic Joint search
	r.initial.profile = 0;
	r.initial.geometryKey = r.fit.geometry.geometryKey;
	r.initial.transferKey = r.fit.transfer->Key();
	r.initial.materials = r.fit.materials;
	r.initial.sources = initial;
	r.candidates = candidates;
	r.comparisonSupported = r.profileSeparated = true;
	r.provenance = "Owned predeclared ASR22 policy: fixed comparison, geometric priors, disjoint folds";
	r.options.cellWorld = 128;
	r.options.minimumBaselineWorld = .2;
	r.options.positiveThreshold = .001;
	r.options.minimumTrainingGain = r.options.minimumValidationGain = 1e-8;
	r.options.complexityPerSource = 1e-6;
	r.options.maximumValidationBlockRegression = 1e-6;
	for (Id region : {Id(1), Id(2)}) {
		D::Evidence e; e.region = region;
		e.localDirectCue = e.sunSeparated = e.indirectSeparated = true;
		e.provenance = "Owned independent fixture aperture prior; no solar mixture; known black/one-bounce materials";
		r.evidence.push_back(e);
	}
	return r;
}
D::Experiment Run(const D::Request &r, const Rig &rig) {
	D::Experiment e;
	Must(D::Discover(r, *rig.validation->data, Key("ASR22 validation v1"), Read(), e));
	return e;
}
size_t Accepted(const D::Experiment &e) {
	size_t n = 0;
	for (const auto &it : e.Receipt().iterations) for (const auto &a : it.attempts) n += a.accepted;
	return n;
}
bool Classified(const D::Experiment &e, D::Classification c) {
	for (const auto &it : e.Receipt().iterations) for (const auto &r : it.regions) if (r.classification == c) return true;
	return false;
}
bool PositiveResidual(const D::Experiment &e) {
	for (const auto &it : e.Receipt().iterations) for (const auto &r : it.regions) if (r.positiveRows) return true;
	return false;
}
std::string Quote(const std::string &s) {
	std::string out = "\"";
	for (char c : s) {
		if (c == '\\' || c == '"') out += '\\';
		if (c == '\n') out += "\\n"; else out += c;
	}
	return out + '"';
}
std::string Receipt(const D::Experiment &e) {
	std::ostringstream out; out << std::setprecision(17);
	out << "{\"stop\":\"" << e.Receipt().stop << "\",\"provenance\":" << Quote(e.Receipt().provenance)
		<< ",\"candidate_count\":" << e.Receipt().candidateCount
		<< ",\"comparison_supported\":" << (e.Receipt().comparisonSupported ? "true" : "false")
		<< ",\"profile_separated\":" << (e.Receipt().profileSeparated ? "true" : "false")
		<< ",\"direct_only_attested\":" << (e.Receipt().directOnlyAttested ? "true" : "false") << ",\"sources\":[";
	for (size_t i = 0; i < e.Selected().sources.size(); ++i) {
		const auto &s = e.Selected().sources[i];
		out << (i ? "," : "") << "{\"id\":" << s.candidate.id << ",\"rgb\":[" << s.value.rgbAmplitude.x << ',' << s.value.rgbAmplitude.y << ',' << s.value.rgbAmplitude.z << "]}";
	}
	out << "],\"iterations\":[";
	for (size_t i = 0; i < e.Receipt().iterations.size(); ++i) {
		const auto &it = e.Receipt().iterations[i];
		CHECK(it.calls <= 4 + it.limits.fitsPerIteration + it.limits.proposalsPerIteration);
		CHECK(it.attempts.size() <= it.limits.fitsPerIteration);
		CHECK(it.work.evaluations <= it.calls * it.evaluationLimits.maxEvaluations);
		CHECK(it.work.rays <= it.calls * it.evaluationLimits.maxRays);
		CHECK(!it.stop.empty() && !it.provenance.empty());
		const auto &o = it.limits; const auto &q = it.evaluationLimits;
		out << (i ? "," : "") << "{\"index\":" << it.index << ",\"stop\":" << Quote(it.stop)
			<< ",\"provenance\":" << Quote(it.provenance) << ",\"incumbent\":" << Quote(Disk::Hex(it.incumbent))
			<< ",\"selected\":" << Quote(Disk::Hex(it.selected))
			<< ",\"limits\":{\"iterations\":" << o.iterations << ",\"candidates\":" << o.maxCandidates
			<< ",\"fits\":" << o.fitsPerIteration << ",\"proposals\":" << o.proposalsPerIteration
			<< ",\"sources\":" << o.maxSources << ",\"minimum_rows\":" << o.minimumRows << ",\"minimum_blocks\":" << o.minimumBlocks
			<< ",\"cell_world\":" << o.cellWorld << ",\"minimum_baseline_world\":" << o.minimumBaselineWorld
			<< ",\"positive_threshold\":" << o.positiveThreshold << ",\"training_gain\":" << o.minimumTrainingGain
			<< ",\"validation_gain\":" << o.minimumValidationGain << ",\"complexity\":" << o.complexityPerSource
			<< ",\"minimum_power_fraction\":" << o.minimumPowerFraction << ",\"block_regression\":" << o.maximumValidationBlockRegression
			<< ",\"evaluations_per_call\":" << q.maxEvaluations << ",\"work_per_call\":" << q.maxWork
			<< ",\"rays_per_call\":" << q.maxRays << ",\"bytes_per_call\":" << q.maxBytes << '}'
			<< ",\"candidate_limited\":" << (it.candidateLimited ? "true" : "false")
			<< ",\"calls\":" << it.calls << ",\"evaluations\":" << it.work.evaluations << ",\"rays\":" << it.work.rays << ",\"regions\":[";
		for (size_t j = 0; j < it.regions.size(); ++j) {
			const auto &a = it.regions[j];
			CHECK(!a.reason.empty());
			out << (j ? "," : "") << "{\"region\":" << a.region << ",\"cell\":[" << a.cell[0] << ',' << a.cell[1] << ',' << a.cell[2]
				<< "],\"class\":\"" << D::Name(a.classification) << "\",\"positive_rows\":" << a.positiveRows
				<< ",\"blocks\":" << a.blocks << ",\"positive_discrepancy\":" << a.positiveDiscrepancy
				<< ",\"receiver_baseline_world\":" << a.receiverBaselineWorld
				<< ",\"reason\":" << Quote(a.reason) << ",\"provenance\":" << Quote(a.provenance) << '}';
		}
		out << "],\"attempts\":[";
		for (size_t j = 0; j < it.attempts.size(); ++j) {
			const auto &a = it.attempts[j]; CHECK(!a.provenance.empty());
			out << (j ? "," : "") << "{\"candidate\":" << a.candidate << ",\"accepted\":" << (a.accepted ? "true" : "false") << ",\"training_gain\":" << a.trainingGain << ",\"validation_gain\":" << a.validationGain << ",\"reason\":\"" << a.reason << "\"}";
		}
		out << "]}";
	}
	out << "]}"; return out.str();
}
std::string Audit(const D::Experiment &e, const Rig &rig, bool improved) {
	J::Score initial, selected;
	Must(D::AuditHeldout(e, *rig.held->data, Key("ASR22 heldout v1"), Read(), initial, selected));
	CHECK(initial.available && selected.available && selected.coverage.complete);
	std::printf("heldout sources %zu -> %zu: %.9g -> %.9g\n", e.Initial().sources.size(), e.Selected().sources.size(), initial.intervalMse, selected.intervalMse);
	if (improved) CHECK(selected.intervalMse < initial.intervalMse * .05);
	else CHECK(selected.intervalMse <= initial.intervalMse + 1e-8);
	CHECK(!e.Selected().physicalExistenceCertified);
	for (const auto &s : e.Selected().sources) CHECK(s.value.geometry.source.state == HypothesisState::Candidate);
	std::ostringstream out; out << std::setprecision(17) << "{\"initial_sources\":" << e.Initial().sources.size()
		<< ",\"selected_sources\":" << e.Selected().sources.size() << ",\"initial_interval_mse\":" << initial.intervalMse
		<< ",\"selected_interval_mse\":" << selected.intervalMse << '}'; return out.str();
}
void Capture(Rig &rig) { Rebuild(rig.scene); rig.UpdateGeometry(); }
void Observe(Rig &rig, const std::vector<Vec3> &values) {
	rig.Observations(values);
	// The S11 fixture uses one block per face; ASR22 separates the existing
	// negative/positive-Y folds into spatial blocks with a receiver-free gap.
	for (auto stream : {rig.train, rig.validation, rig.held})
		for (auto &o : *stream->data) o.block = o.block * 100 +
			(o.receiver.world.y < -.6f ? 1 : 2) + 100000 * unsigned(o.role);
}

int main(int argc, char **argv) {
	try {
		std::ostringstream report; report << "{\"schema\":\"org.openjk.iterative-discovery.v1\",\"scenes\":[";
		// A housing roof hides the lamp from above while its opening illuminates the room.
		Rig hidden;
		hidden.scene.Quad(V(-.95, -.4, 1.1), V(-.35, -.4, 1.1), V(-.35, .2, 1.1), V(-.95, .2, 1.1));
		Capture(hidden);
		auto lamp = SupportedPoint(hidden, 10, V(-.65, -.1, .8));
		auto rival = SupportedPoint(hidden, 30, V(-1.05, .5, 1.25));
		Observe(hidden, hidden.Truth({Answer(lamp, V(2.4, .8, .3))}));
		auto hp = Policy(hidden, {rival, lamp}); auto h = Run(hp, hidden);
		CHECK(Accepted(h) == 1 && h.Selected().sources.size() == 1);
		CHECK(h.Selected().sources[0].candidate.id == 10);
		auto ha = Audit(h, hidden, true);
		report << "{\"name\":\"hidden_housing\",\"heldout\":" << ha << ",\"receipt\":" << Receipt(h) << '}';
		CHECK(Receipt(h) == Receipt(Run(hp, hidden)));
		std::reverse(hp.candidates.begin(), hp.candidates.end());
		CHECK(Receipt(h) == Receipt(Run(hp, hidden)));
		// The finite volume comes from housing geometry, not an answer position.
		auto lattice = Policy(hidden, {}); D::SearchVolume volume;
		volume.prototype = lamp; volume.positions = Box(V(-.9, -.35, .55), V(-.4, .15, 1.05));
		lattice.volumes = {volume}; lattice.options.fitsPerIteration = 32;
		auto l = Run(lattice, hidden); CHECK(Accepted(l) == 1); auto la = Audit(l, hidden, true);
		CHECK(Length(V(l.Selected().sources[0].value.geometry.position.world) - V(-.65, -.1, .8)) < .05);
		report << ",{\"name\":\"housing_volume\",\"heldout\":" << la << ",\"receipt\":" << Receipt(l) << '}';
		// A real occluder clips a smooth spot; no visibility bypass or inferred axis.
		Rig spot;
		spot.scene.Quad(V(-.25, -.4, .45), V(.3, -.4, .45), V(.3, .4, .45), V(-.25, .4, .45));
		Capture(spot);
		auto beam = SupportedPoint(spot, 20, V(-.3, 0, 1.25), true);
		Observe(spot, spot.Truth({Answer(beam, V(2, .2, .5))}));
		bool clipped = false;
		for (const auto &o : *spot.held->data) {
			V toReceiver = V(o.receiver.world) - V(beam.shape.initial.geometry.position.world);
			if (Dot(Unit(toReceiver), V(beam.shape.initial.geometry.frame.forward)) <= beam.shape.initial.geometry.angular.cosOuter) continue;
			RayQuery query; query.startWorld = beam.shape.initial.geometry.position.world;
			query.endWorld = (V(o.receiver.world) + V(o.receiver.geometricNormal) * .001).Float();
			RayResult ray; Must(spot.scene.visibility->Query(query, Read(), ray));
			clipped |= ray.state == VisibilityState::Blocked;
		}
		CHECK(clipped); // a within-cone HELDOUT receiver is actually occluded
		auto sp = Policy(spot, {beam}); sp.directOnlyAttested = true;
		sp.fit.profiles[0].contract.physicalTransportAssumed = false;
		auto s = Run(sp, spot);
		CHECK(Accepted(s) == 1); auto sa = Audit(s, spot, true);
		report << ",{\"name\":\"occluded_spot\",\"heldout\":" << sa << ",\"receipt\":" << Receipt(s) << '}';
		// Two colors require a second residual round after the first jointly fitted root.
		Rig two;
		auto a = SupportedPoint(two, 1, V(-.65, -.1, .8)), b = SupportedPoint(two, 2, V(.7, .15, 1.1));
		Observe(two, two.Truth({Answer(a, V(2, .05, .1)), Answer(b, V(.05, .2, 2.5))}));
		auto tp = Policy(two, {b, a}); tp.options.proposalsPerIteration = 1;
		auto t = Run(tp, two); CHECK(Accepted(t) == 2 && t.Selected().sources.size() == 2); auto ta = Audit(t, two, true);
		report << ",{\"name\":\"two_rounds\",\"heldout\":" << ta << ",\"receipt\":" << Receipt(t) << '}';
		// Explicit forward-encoded comparison: sum first, then encode. No RGB8
		// subtraction is used as source power and no inverse transfer is fitted.
		Rig encoded;
		auto encodedLamp = SupportedPoint(encoded, 22, V(-.65, -.1, .8));
		Observe(encoded, encoded.Truth({Answer(encodedLamp, V(2, .7, .3))}));
		Bake::Recipe recipe; recipe.gamma = 2;
		auto profile = Bake::NetRadiantProfile(recipe);
		for (auto stream : {encoded.train, encoded.validation, encoded.held}) for (auto &o : *stream->data) {
			Vec3 value = (V(o.rgb[0].lower, o.rgb[1].lower, o.rgb[2].lower) * 32).Float();
			uint8_t packed[3]; Must(Bake::Encode(profile, value, value, packed));
			for (unsigned c = 0; c < 3; ++c) o.rgb[c] = Amplitude::Rgb8(packed[c]);
		}
		auto ep = Policy(encoded, {encodedLamp}); ep.fit.profiles[0].fixedGain = 32;
		ep.fit.profiles[0].model.profile = ep.fit.profiles[0].contract.profile = profile;
		ep.fit.profiles[0].contract.operation = I::ObservationOperator::NetRadiantEncoded;
		ep.fit.profiles[0].contract.physicalTransportAssumed = false; ep.directOnlyAttested = true;
		auto enc = Run(ep, encoded); CHECK(Accepted(enc) == 1); auto ea = Audit(enc, encoded, true);
		report << ",{\"name\":\"encoded_appearance\",\"heldout\":" << ea << ",\"receipt\":" << Receipt(enc) << '}';
		// Real passive red reflection stays linked to the supplied primary root.
		Rig red;
		red.request.materials[0] = KnownMaterial(1, V(.7, .01, .01));
		red.request.profiles[0].contract.physicalTransportAssumed = true;
		auto primary = SupportedPoint(red, 50, V(-.4, -.1, .8));
		auto ghost = SupportedPoint(red, 51, V(.6, .3, .9));
		Observe(red, red.Truth({Answer(primary, V(2, 2, 2))}));
		auto rp = Policy(red, {ghost}, {Answer(primary, V(.1, .1, .1))});
		for (auto &e : rp.evidence) e.indirectSeparated = false;
		auto r = Run(rp, red); CHECK(Accepted(r) == 0); CHECK(Classified(r, D::Classification::IndirectPossible)); auto ra = Audit(r, red, false);
		I::Result decomposition; J::Work work;
		Must(J::DecomposeFrozen(rp.fit, r.Selected(), Read(), decomposition, work));
		CHECK(decomposition.byRoot.size() == 1 && decomposition.byRoot[0].source == 50);
		bool bounce = false; for (const auto &v : decomposition.values) bounce |= v.indirect.x > v.indirect.y + 1e-6;
		CHECK(bounce);
		report << ",{\"name\":\"red_bounce\",\"heldout\":" << ra << ",\"receipt\":" << Receipt(r) << '}';
		rp.fit.profiles[0].contract.physicalTransportAssumed = false;
		auto omitted = Run(rp, red); CHECK(Accepted(omitted) == 0);
		CHECK(Classified(omitted, D::Classification::IndirectPossible));
		CHECK(PositiveResidual(omitted));
		report << ",{\"name\":\"omitted_red_bounce\",\"receipt\":" << Receipt(omitted) << '}';
		// Reflected doorway: captured jambs and a red ceiling reflector. Missing
		// passive transport remains explicit even when a local proxy could fit it.
		Rig door;
		door.scene.Quad(V(0, -2, -.1), V(0, -.4, -.1), V(0, -.4, 1.5), V(0, -2, 1.5));
		door.scene.Quad(V(0, .4, -.1), V(0, 2, -.1), V(0, 2, 1.5), V(0, .4, 1.5));
		Capture(door); door.request.materials[0] = KnownMaterial(1, V(.6, .1, .05));
		door.request.profiles[0].contract.physicalTransportAssumed = true;
		auto outside = SupportedPoint(door, 60, V(-.7, -.3, 1.1));
		auto proxy = SupportedPoint(door, 61, V(.7, .3, .7));
		Observe(door, door.Truth({Answer(outside, V(3, 1, .5))}));
		auto dp = Policy(door, {proxy}, {Answer(outside, V(.1, .1, .1))});
		dp.fit.profiles[0].contract.physicalTransportAssumed = false;
		for (auto &e : dp.evidence) e.indirectSeparated = false;
		auto d = Run(dp, door); CHECK(Accepted(d) == 0); CHECK(Classified(d, D::Classification::IndirectPossible)); auto da = Audit(d, door, false);
		CHECK(PositiveResidual(d));
		report << ",{\"name\":\"reflected_doorway\",\"heldout\":" << da << ",\"receipt\":" << Receipt(d) << '}';
		// Sun-only directional irradiance is an identified environment root, not a lamp.
		Rig sun;
		I::Root solar; solar.source = 70; solar.kind = TransportRootKind::Sun;
		solar.provenance = "Owned fixed directional environment root, no local source";
		solar.generation = Key("Owned sun v1");
		for (const auto &p : sun.request.transfer->Patches()) {
			V direction = Unit(V(.3, .1, 1));
			RayQuery query; query.startWorld = (V(p.receiver.world) + V(p.receiver.geometricNormal) * .001).Float();
			query.endWorld = (V(query.startWorld) + direction * 100).Float(); query.originSurface = p.receiver.surface;
			RayResult ray; Must(sun.scene.visibility->Query(query, Read(), ray));
			CHECK(ray.state != VisibilityState::Unknown);
			double cosine = ray.state == VisibilityState::Visible ? std::max(0., Dot(V(p.receiver.geometricNormal), direction)) : 0;
			solar.incident.push_back((V(.6, .5, .4) * cosine).Float()); solar.available.push_back(1);
		}
		sun.request.boundaryRoots = {solar};
		auto solarGhost = SupportedPoint(sun, 71, V(-.4, .2, .9));
		Observe(sun, sun.Truth({}));
		auto up = Policy(sun, {solarGhost}); auto u = Run(up, sun);
		CHECK(Accepted(u) == 0 && u.Selected().sources.empty()); auto ua = Audit(u, sun, false);
		report << ",{\"name\":\"sun_only\",\"heldout\":" << ua << ",\"receipt\":" << Receipt(u) << '}';
		up.fit.boundaryRoots.clear(); for (auto &e : up.evidence) e.sunSeparated = false;
		auto ambiguous = Run(up, sun); CHECK(Accepted(ambiguous) == 0); CHECK(Classified(ambiguous, D::Classification::SunAmbiguous));
		CHECK(PositiveResidual(ambiguous));
		report << ",{\"name\":\"sun_ambiguous\",\"receipt\":" << Receipt(ambiguous) << '}';
		// Profile ambiguity never exposes a numerical residual or starts a refit.
		auto mismatch = hp; mismatch.profileSeparated = false;
		auto m = Run(mismatch, hidden); CHECK(m.Receipt().stop == "UnsupportedComparison");
		for (const auto &it : m.Receipt().iterations) { CHECK(it.calls == 0); for (const auto &v : it.regions) CHECK(v.positiveDiscrepancy == 0); }
		report << ",{\"name\":\"profile_ambiguous\",\"receipt\":" << Receipt(m) << '}';
		// Saturated targets are censored evidence, never seed positions.
		auto saved = *hidden.train->data;
		for (auto &o : *hidden.train->data) for (auto &c : o.rgb) c = Amplitude::Rgb8(255);
		auto saturated = Run(hp, hidden); CHECK(Accepted(saturated) == 0);
		report << ",{\"name\":\"saturated\",\"receipt\":" << Receipt(saturated) << '}';
		*hidden.train->data = saved;
		// A training-only local structure cannot pass independent validation.
		auto validSaved = *hidden.validation->data;
		for (auto &o : *hidden.validation->data) for (auto &c : o.rgb) c.lower = c.upper = 0;
		auto negative = Run(hp, hidden); CHECK(Accepted(negative) == 0);
		report << ",{\"name\":\"training_only\",\"receipt\":" << Receipt(negative) << '}';
		*hidden.validation->data = validSaved;
		auto noCue = hp; for (auto &e : noCue.evidence) e.localDirectCue = false;
		auto unexplained = Run(noCue, hidden); CHECK(Accepted(unexplained) == 0);
		CHECK(Classified(unexplained, D::Classification::Unexplained));
		report << ",{\"name\":\"no_independent_cue\",\"receipt\":" << Receipt(unexplained) << '}';
		auto expensive = hp; expensive.options.complexityPerSource = 100;
		CHECK(Accepted(Run(expensive, hidden)) == 0);
		auto oneRound = tp; oneRound.options.iterations = 1;
		auto capped = Run(oneRound, two); CHECK(capped.Receipt().stop == "IterationLimit");
		CHECK(Accepted(capped) == 1);
		// Missing captured geometry is never certified as a clear lamp location.
		Rig unknown; Observe(unknown, unknown.Truth({Answer(Point(90, V(-.65, -.1, .8)), V(2, 1, .2))}));
		BakedSurface unsupported; unsupported.ordinal = uint32_t(unknown.scene.surfaces.size());
		unsupported.kind = BakedSurfaceKind::Unsupported; unsupported.motion = SurfaceMotion::StaticWorld;
		unknown.scene.surfaces.push_back(unsupported); Capture(unknown);
		auto unknownCandidate = SupportedPoint(unknown, 91, V(-.65, -.1, .8));
		auto un = Run(Policy(unknown, {unknownCandidate}), unknown);
		CHECK(Accepted(un) == 0 && Classified(un, D::Classification::UnsupportedGeometry));
		report << ",{\"name\":\"unknown_geometry\",\"receipt\":" << Receipt(un) << '}';
		// Caps, cancellation and fold separation fail atomically.
		D::Experiment rejected; auto bad = hp; bad.options.iterations = 9;
		CHECK(D::Discover(bad, *hidden.validation->data, Key("v"), Read(), rejected).code == Code::WorkLimit);
		CHECK(!rejected.Receipt().frozen);
		bad = hp; bad.fit.options.maxEvaluations = 1;
		CHECK(D::Discover(bad, *hidden.validation->data, Key("v"), Read(), rejected).code == Code::WorkLimit);
		auto leaked = *hidden.validation->data; leaked[0].block = hidden.train->data->at(0).block;
		CHECK(D::Discover(hp, leaked, Key("v"), Read(), rejected).code == Code::InvalidInput);
		bad = lattice; bad.options.maxCandidates = 8;
		CHECK(D::Discover(bad, *hidden.validation->data, Key("v"), Read(), rejected).code == Code::WorkLimit);
		bad = hp; bad.candidates.push_back(bad.candidates[0]);
		CHECK(D::Discover(bad, *hidden.validation->data, Key("v"), Read(), rejected).code == Code::InvalidInput);
		struct CancellationFlag : Cancellation { bool IsCancelled() const noexcept override { return true; } } cancel;
		auto read = Read(); read.cancellation = &cancel;
		CHECK(D::Discover(hp, *hidden.validation->data, Key("v"), read, rejected).code == Code::Cancelled);
		J::Score before, after; auto heldChanged = *hidden.held->data;
		for (auto &o : heldChanged) for (auto &c : o.rgb) c.lower = c.upper = 100;
		auto frozen = Receipt(h);
		Must(D::AuditHeldout(h, heldChanged, Key("changed heldout"), Read(), before, after));
		CHECK(Receipt(h) == frozen);
		size_t ghosts = Accepted(r) + Accepted(omitted) + Accepted(d) + Accepted(u) + Accepted(ambiguous) +
			Accepted(m) + Accepted(saturated) + Accepted(negative) + Accepted(unexplained) + Accepted(un);
		report << "],\"checks\":" << checks << ",\"failures\":" << failures
			<< ",\"supported_positive_sources\":4,\"initial_recalled_sources\":0,\"discovered_sources\":"
			<< Accepted(h) + Accepted(s) + Accepted(t) << ",\"negative_controls\":10,\"added_negative_lights\":" << ghosts
			<< ",\"historical_source_truth\":\"Unavailable\"}";
		if (argc == 2) { std::ofstream file(argv[1]); file << report.str() << '\n'; if (!file) throw std::runtime_error("Discovery report write failed"); }
		std::printf("ASR22: %d checks, %d failures\n", checks, failures); return failures ? 1 : 0;
	} catch (const std::exception &e) { std::fprintf(stderr, "ASR22 exception: %s\n", e.what()); return 2; }
}
