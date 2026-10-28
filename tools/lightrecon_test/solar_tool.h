// Trusted standalone fixture adapter. Training RGB is read before fitting;
// validation RGB is opened only AFTER immutable proposals are frozen.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lightrecon/sun_fit.h"
#include <map>
namespace SolarTool
{
namespace S = Rend2LightRecon::Core::Solar;
inline void Json(std::ostream &out, const std::string &s)
{
	out << '"';
	for (unsigned char c : s)
	{
		if (c == '"' || c == '\\')
			out << '\\' << c;
		else if (c == '\n')
			out << "\\n";
		else if (c >= 32)
			out << c;
	}
	out << '"';
}
inline void Rgb(Vec3 v) { std::cout << '[' << v.x << ',' << v.y << ',' << v.z << ']'; }
inline void Coverage(const S::Coverage &c)
{
	std::cout << "{\"requested\":" << c.requested << ",\"used\":" << c.used << ",\"positive\":" << c.positive
			  << ",\"positive_blocks\":" << c.positiveBlocks << ",\"blocks\":" << c.blocks
			  << ",\"saturated\":" << c.saturated << ",\"excluded_geometry\":" << c.excludedGeometry
			  << ",\"excluded_encoding\":" << c.excludedEncoding << ",\"excluded_sky\":" << c.excludedSky
			  << ",\"normal_eigen_ratio\":" << c.normalEigenRatio << ",\"black_rmse\":" << c.blackRmse << '}';
}
inline void Work(const S::Work &w)
{
	std::cout << "{\"forward_calls\":" << w.forwardCalls << ",\"directions\":" << w.directions
			  << ",\"rays\":" << w.rays << ",\"pairs\":" << w.pairs << ",\"traversal\":" << w.traversal
			  << ",\"encoding_calls\":" << w.encodingCalls
			  << ",\"counters_complete\":" << (w.complete ? "true" : "false") << '}';
}
inline std::shared_ptr<std::vector<S::Sample>> Read(const char *path,
													const std::shared_ptr<std::vector<Receiver>> &receivers)
{
	std::ifstream input(path);
	std::string magic;
	input >> magic;
	if (magic != "JKSOLAR1")
		throw std::runtime_error("Solar sample protocol");
	auto samples = std::make_shared<std::vector<S::Sample>>(Count(input, 256));
	std::map<Id, Receiver> geometry;
	for (const auto &r : *receivers)
		geometry.emplace(r.sample, r);
	for (auto &s : *samples)
	{
		Id id = 0;
		input >> id;
		auto g = geometry.find(id);
		if (g == geometry.end())
			throw std::runtime_error("Solar sample missing geometry");
		s.receiver = g->second;
		for (auto &b : s.rgb)
		{
			unsigned x = 0;
			input >> x;
			if (x > 255)
				throw std::runtime_error("Solar RGB8");
			b = uint8_t(x);
		}
		input >> s.areaWorldSquared >> s.footprintRadiusWorld;
		Vec(input, s.otherPreencoding);
		s.eligible = s.encodingVerifiedForProfile = true;
	}
	std::string extra;
	if (!input || (input >> extra))
		throw std::runtime_error("Truncated/extra solar samples");
	return samples;
}
inline int Run(const Recipe &recipe, const std::shared_ptr<std::vector<SourceHypothesis>> &hints,
			   const std::shared_ptr<std::vector<Receiver>> &receivers, const EvaluationContext &context,
			   const ReadContext &read, const Disk::Digest &map, const char *trainingPath,
			   const char *validationPath, const char *heldoutPath)
{
	S::Request request;
	request.mapKey = map;
	request.lineage = "controlled-compiled-solar";
	request.profiles = {NetRadiantProfile(recipe)};
	request.geometry = context;
	request.options.allowAssumedProfiles = true;
	request.options.partitionOriginWorld = Vec3(64, 64, 64);
	for (const auto &s : *hints)
	{
		if (s.kind != SourceKind::Directional || s.compilerRgbAmplitude.x != 0 ||
			s.compilerRgbAmplitude.y != 0 || s.compilerRgbAmplitude.z != 0)
			throw std::runtime_error("Solar priors contain only directional XYZ with ZERO amplitude");
		S::DirectionHint h;
		h.id = s.id;
		h.kind = S::HintKind::AuthoredSun;
		h.toSource = Vec3(-s.orientation.forward.x, -s.orientation.forward.y, -s.orientation.forward.z);
		h.staticEligible = true;
		h.provenance = "Explicit caller supplied authored sun directive; intensity withheld";
		request.hints.push_back(h);
	}
	auto training = Read(trainingPath, receivers);
	request.training = Lease(training);
	S::FitResult fit;
	S::Result result;
	auto start = std::chrono::steady_clock::now();
	auto e = S::Fit(request, read, fit);
	bool validationRead = false;
	if (e)
	{
		std::cerr << "TrainingFrozen " << Disk::Hex(fit.frozenKey) << '\n';
		auto validation = Read(validationPath, receivers);
		validationRead = true;
		e = S::ValidateAndPublish(fit, map, Lease(validation), context, read, result);
	}
	else
	{
		result.state = fit.state;
		result.reason = fit.reason;
		result.diagnostics = fit.diagnostics;
	}
	double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	std::cout << std::setprecision(12)
			  << "{\"schema\":\"org.openjk.solar-fit-diagnostic.v1\",\"code\":" << int(e.code)
			  << ",\"state\":\"" << S::StateName(result.state) << "\",\"reason\":";
	Json(std::cout, result.reason);
	std::cout << ",\"map_sha256\":\"" << Disk::Hex(map) << "\",\"frozen_key\":\"" << Disk::Hex(fit.frozenKey)
			  << "\",\"validation_read_after_freeze\":" << (validationRead ? "true" : "false")
			  << ",\"seconds\":" << seconds << ",\"training_coverage\":[";
	for (size_t i = 0; i < fit.profileCoverage.size(); ++i)
	{
		if (i)
			std::cout << ',';
		Coverage(fit.profileCoverage[i]);
	}
	std::cout << "],\"fit_work\":";
	Work(fit.work);
	std::cout << ",\"validation_work\":";
	Work(result.work);
	std::cout << ",\"diagnostics\":[";
	for (size_t i = 0; i < result.diagnostics.size(); ++i)
	{
		if (i)
			std::cout << ',';
		Json(std::cout, result.diagnostics[i]);
	}
	std::cout << "],\"decisions\":[";
	for (size_t i = 0; i < result.decisions.size(); ++i)
	{
		if (i)
			std::cout << ',';
		const auto &d = result.decisions[i];
		std::cout << "{\"profile\":";
		Json(std::cout, d.profile);
		std::cout << ",\"model\":" << int(d.model) << ",\"to_source\":";
		Rgb(d.toSource);
		std::cout << ",\"admitted_before_alternative_pruning\":" << (d.admitted ? "true" : "false")
				  << ",\"validation_rmse\":";
		if (d.evaluated)
			std::cout << d.validation.intervalRmse;
		else
			std::cout << "null";
		std::cout << ",\"reason\":";
		Json(std::cout, d.reason);
		std::cout << '}';
	}
	std::cout << "],\"hypotheses\":[";
	for (size_t i = 0; i < result.hypotheses.size(); ++i)
	{
		if (i)
			std::cout << ',';
		const auto &h = result.hypotheses[i];
		std::cout << "{\"profile\":";
		Json(std::cout, h.fitted.profile.id);
		std::cout << ",\"profile_key\":\"" << Disk::Hex(h.fitted.profile.contentKey)
				  << "\",\"model\":" << int(h.fitted.model) << ",\"to_source\":";
		Rgb(h.fitted.toSource);
		std::cout << ",\"sun_compiler_rgb\":";
		Rgb(h.fitted.sunRgb);
		std::cout << ",\"sky_compiler_rgb\":";
		Rgb(h.fitted.skyRgb);
		std::cout << ",\"train_rmse\":" << h.fitted.training.intervalRmse
				  << ",\"validation_rmse\":" << h.validation.intervalRmse
				  << ",\"validation_max_error\":" << h.validation.maximumError
				  << ",\"sky_only_validation_rmse\":" << h.validationSkyOnlyRmse
				  << ",\"fill_validation_rmse\":" << h.validationFillRmse
				  << ",\"sampled_angular_ambiguity_radians\":" << h.sampledAngularAmbiguityRadians
				  << ",\"coverage\":";
		Coverage(h.coverage);
		std::cout << ",\"sky_component_supported\":" << (h.skySupported ? "true" : "false")
				  << ",\"numerical_status\":\"AvailableConditional\",\"canonical_suns\":"
				  << h.asset.suns.size() << ",\"canonical_environments\":" << h.asset.environments.size()
				  << ",\"canonical_unresolved_transport_descriptors\":" << h.asset.transport.size()
				  << ",\"photometric_unit\":\"UncalibratedRelative\",\"angular_extent\":null,\"existence_"
					 "probability\":null,\"exact_light_suitability\":\"Unsuitable\",\"sky_excludes_direct_"
					 "sun\":true,\"source_id\":"
				  << (h.asset.emitters.empty() ? 0 : h.asset.emitters[0].source.id)
				  << ",\"sun_intensity_slice\":{\"bounded\":"
				  << (h.sunIntensitySlice.bounded ? "true" : "false")
				  << ",\"lower\":" << h.sunIntensitySlice.lower << ",\"upper\":" << h.sunIntensitySlice.upper
				  << ",\"probability\":null},\"receivers\":[";
		// Receiver payload follows the already written JSON array opener.
		for (size_t j = 0; j < h.receivers.size(); ++j)
		{
			if (j)
				std::cout << ',';
			const auto &r = h.receivers[j];
			std::cout << "{\"sample\":" << r.sample << ",\"exclusion\":" << int(r.exclusion)
					  << ",\"sky_response\":" << r.skyResponse << ",\"sun_response\":" << r.sunResponse
					  << ",\"sun_facing\":" << (r.sunFacing ? "true" : "false") << '}';
		}
		std::cout << "],\"environment_payload\":[";
		for (size_t j = 0; j < h.environment.size(); ++j)
		{
			if (j)
				std::cout << ',';
			const auto &s = h.environment[j];
			std::cout << "{\"to_source\":";
			Rgb(s.toSource);
			std::cout << ",\"relative_rgb\":";
			Rgb(s.relativeRgb);
			std::cout << ",\"response_weight\":" << s.responseWeight << '}';
		}
		std::cout << "]}";
	}
	std::cout << "],\"heldout\":[";
	if (heldoutPath && e)
	{
		auto heldout = Read(heldoutPath, receivers);
		for (size_t i = 0; i < result.hypotheses.size(); ++i)
		{
			if (i)
				std::cout << ',';
			S::Assessment a;
			auto status =
				S::Assess(result.hypotheses[i].fitted, fit.options, Lease(heldout), context, read, a);
			std::cout << "{\"hypothesis\":" << i << ",\"code\":" << int(status.code) << ",\"rmse\":";
			if (status)
				std::cout << a.score.intervalRmse;
			else
				std::cout << "null";
			std::cout << ",\"maximum_error\":";
			if (status)
				std::cout << a.score.maximumError;
			else
				std::cout << "null";
			std::cout << ",\"coverage\":";
			Coverage(a.coverage);
			std::cout << ",\"work\":";
			Work(a.work);
			std::cout << '}';
		}
	}
	std::cout << "],\"runtime_publication\":false}\n";
	return 0; // explicit status is data, not fabricated success
}
} // namespace SolarTool
