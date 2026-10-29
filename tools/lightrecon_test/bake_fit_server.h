// X07 test adapter, not a renderer or a source-discovery solver.
// Every prediction calls the production CpuForwardEvaluator. GPL-2.0-or-later.
#pragma once
#include "lightrecon/amplitude_fit.h"
#include <limits>
#include <sstream>

class FitTraceProvider final : public VisibilityProvider
{
  public:
	std::shared_ptr<const CompilerTrace> tree;
	mutable uint64_t calls = 0, nodes = 0, triangles = 0;
	bool unknown = false;
	Error Query(const RayQuery &q, const ReadContext &read, RayResult &out) const override
	{
		++calls;
		if (unknown || tree->ClassifyOrigin(q.startWorld) == PointDomain::Solid)
		{
			out = RayResult();
			out.issues = IncompleteGeometry;
			return Error();
		}
		auto e = tree->Query(q.startWorld, q.endWorld, q.purpose == RayPurpose::Sky, read, q.maxWork, out);
		nodes += out.nodeTests;
		triangles += out.primitiveTests;
		return e;
	}
};
class FitCancelled final : public Cancellation
{
	bool IsCancelled() const noexcept override { return true; }
};
class AmplitudeRows final : public ChunkSource<Amplitude::Observation>
{
  public:
	std::shared_ptr<std::vector<Amplitude::Observation>> records;
	Error Read(uint64_t first, size_t count, const ReadContext &read,
			   Chunk<Amplitude::Observation> &out) const override
	{
		if (read.cancellation && read.cancellation->IsCancelled())
			return Failure(Code::Cancelled, "Training read cancelled");
		if (first > records->size() || count > records->size() - first)
			return Failure(Code::InvalidInput, "Training range");
		out = VectorChunk<Amplitude::Observation>(records, size_t(first), count);
		return {};
	}
};
inline void RunAmplitudeFrame(std::istream &in, const ProfileHypothesis &profile,
							  const std::shared_ptr<std::vector<SourceHypothesis>> &base,
							  const std::shared_ptr<std::vector<Receiver>> &receivers,
							  const EvaluationContext &geometry, const Options &forward,
							  const ReadContext &read, const Disk::Digest &map)
{
	Amplitude::Request request;
	request.mapKey = map;
	request.profile = profile;
	request.geometry = geometry;
	request.forward = forward;
	request.forward.factors.reset();
	double initial = 0, upper = 0, huber = 0, ridge = 0;
	size_t passes = 0, evaluations = 0;
	unsigned loss = 0;
	in >> initial >> upper >> passes >> evaluations >> loss >> huber >> ridge;
	if (!in || loss > 2 || passes > 64 || evaluations > 20000 || !std::isfinite(initial) ||
		!std::isfinite(upper) || initial < 0 || initial > upper || upper > 10000)
		throw std::runtime_error("S06 fixed-amplitude work/bounds frame");
	for (const auto &s : *base)
	{
		Amplitude::Candidate c;
		c.source = s;
		c.initialQ = Vec3(float(initial), float(initial), float(initial));
		c.upperQ = Vec3(float(upper), float(upper), float(upper));
		request.candidates.push_back(c);
	}
	auto rows = std::make_shared<AmplitudeRows>();
	rows->records = std::make_shared<std::vector<Amplitude::Observation>>();
	unsigned ambient = 0;
	in >> ambient;
	if (ambient > 1)
		throw std::runtime_error("S06 model selector");
	if (ambient)
	{
		request.candidates.clear();
		request.options.baselineUpper = Vec3(float(upper), float(upper), float(upper));
		request.options.baselinePenalty = 1e-8;
	}
	size_t n = Count(in, 512);
	if (n != receivers->size())
		throw std::runtime_error("S06 training receiver count");
	for (size_t i = 0; i < n; ++i)
	{
		unsigned raw[3];
		double area = 0, reliability = 0;
		in >> raw[0] >> raw[1] >> raw[2] >> area >> reliability;
		if (!in || raw[0] > 255 || raw[1] > 255 || raw[2] > 255)
			throw std::runtime_error("S06 encoded training RGB");
		Amplitude::Observation a;
		a.id = (*receivers)[i].sample;
		a.region = (*receivers)[i].surface;
		a.receiver = (*receivers)[i];
		a.type = Amplitude::Type::SurfaceEncoded;
		a.measure = area;
		a.reliability = reliability;
		a.encodingAttested = a.primariesComparable = a.comparisonScaleKnown = true;
		for (size_t k = 0; k < 3; ++k)
			a.channels[k] = Amplitude::Rgb8(uint8_t(raw[k]));
		rows->records->push_back(a);
	}
	std::string extra;
	if (!in || (in >> extra))
		throw std::runtime_error("S06 frame dimensions");
	request.count = n;
	request.observations = rows;
	request.observationKey = Disk::Hash("blind-training-stream", 21);
	request.options.maxIterations = passes;
	request.options.nonlinearPasses = passes;
	request.options.maxEvaluations = evaluations;
	request.options.loss = Amplitude::Loss(loss);
	request.options.huber = huber;
	request.options.ridge = ridge;
	request.options.restarts = 2;
	request.options.batchRows = 128;
	auto start = std::chrono::steady_clock::now();
	Amplitude::Result result;
	auto error = Amplitude::Fit(request, read, result);
	double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
	std::cout << std::setprecision(12) << "{\"type\":\"production_fit\",\"code\":" << int(error.code)
			  << ",\"fitted\":" << (result.fitted ? "true" : "false")
			  << ",\"converged\":" << (result.converged ? "true" : "false") << ",\"stop\":\""
			  << Amplitude::StopName(result.stop) << "\",\"linear\":" << (result.linear ? "true" : "false")
			  << ",\"strict_difference\":" << result.strictDifference
			  << ",\"maximum_interval_error\":" << result.maximumIntervalError
			  << ",\"objective\":" << result.objective.Total()
			  << ",\"kkt_available\":" << (result.projectedKktAvailable ? "true" : "false")
			  << ",\"kkt\":" << result.projectedKkt << ",\"rank\":" << result.spectrum.rank
			  << ",\"parameters\":" << result.spectrum.parameters << ",\"condition\":";
	if (result.spectrum.conditionKnown)
		std::cout << result.spectrum.condition;
	else
		std::cout << "null";
	std::cout << ",\"coverage\":{\"requested\":" << result.coverage.requested
			  << ",\"used\":" << result.coverage.used << ",\"channels\":" << result.coverage.channels
			  << ",\"censored\":" << result.coverage.censored
			  << ",\"unknown_visibility\":" << result.coverage.visibility
			  << "},\"work\":{\"evaluations\":" << result.work.objectiveEvaluations
			  << ",\"forward_calls\":" << result.work.forwardCalls
			  << ",\"prepare_calls\":" << result.work.prepareCalls << ",\"pairs\":" << result.work.pairs
			  << ",\"rays\":" << result.work.rays << ",\"traversal\":" << result.work.traversal
			  << ",\"cache_hits\":" << result.work.cacheHits << ",\"row_reads\":" << result.work.rowReads
			  << ",\"accounted_bytes\":" << result.work.accountedBytes << "},\"seconds\":" << seconds
			  << ",\"key\":\"" << Disk::Hex(result.key) << "\",\"profile_key\":\""
			  << Disk::Hex(result.profileKey) << "\",\"rgb\":[";
	if (ambient && result.fitted)
		std::cout << '[' << result.baseline.x << ',' << result.baseline.y << ',' << result.baseline.z << ']';
	for (size_t j = 0; j < result.sources.size(); ++j)
	{
		if (j)
			std::cout << ',';
		const auto &q = result.sources[j].compilerQ;
		std::cout << '[' << q.x << ',' << q.y << ',' << q.z << ']';
	}
	std::cout << "],\"singular\":[";
	for (size_t j = 0; j < result.spectrum.singular.size(); ++j)
	{
		if (j)
			std::cout << ',';
		std::cout << result.spectrum.singular[j];
	}
	std::cout << "],\"conditional_slices\":[";
	for (size_t j = 0; j < result.sources.size(); ++j)
		for (size_t k = 0; k < 3; ++k)
		{
			if (j || k)
				std::cout << ',';
			const auto &s = result.sources[j].conditional[k];
			std::cout << "{\"available\":" << (s.available ? "true" : "false") << ",\"lower\":" << s.lower
					  << ",\"upper\":" << s.upper
					  << ",\"search_limited\":" << (s.upperIsSearchLimit ? "true" : "false") << '}';
		}
	std::cout << "]}\n" << std::flush;
}

inline int RunFitServer(const Recipe &recipe, const std::shared_ptr<std::vector<SourceHypothesis>> &base,
						const std::shared_ptr<std::vector<Receiver>> &receivers, EvaluationContext context,
						const ReadContext &read, const Disk::Digest &mapHash)
{
	if (base->empty() || base->size() > 8 || receivers->empty() || receivers->size() > 512)
		throw std::runtime_error("X07 bounded source/receiver span");
	for (const auto &s : *base)
		if (s.kind != SourceKind::Point || s.compilerRgbAmplitude.x != 0 || s.compilerRgbAmplitude.y != 0 ||
			s.compilerRgbAmplitude.z != 0)
			throw std::runtime_error("Server accepts fixed point geometry with ZERO seed amplitudes only");
	auto provider = std::make_shared<FitTraceProvider>();
	provider->tree = context.compilerTrace;
	context.visibility = provider;
	Options options;
	options.allowAssumed = true;
	options.trace = TraceMode::StaticGeometry;
	options.maxPairs = 4096;
	options.maxRays = 4096;
	options.maxWork = 1048576;
	auto profile = NetRadiantProfile(recipe);
	CpuForwardEvaluator evaluator;
	std::shared_ptr<const PreparedFactors> factors;
	auto e = evaluator.Prepare(profile, Lease(base), Lease(receivers), context, options, read, factors);
	if (!e)
		throw std::runtime_error(e.message);
	options.factors = factors;
	std::cout << std::setprecision(12)
			  << "{\"type\":\"ready\",\"protocol\":\"FixedGeometryBlindIntensityFit\",\"profile_id\":\""
			  << profile.id << "\",\"profile_key\":\"" << Disk::Hex(profile.contentKey)
			  << "\",\"source_lock\":\"" << Disk::Hex(profile.sourceLockKey) << "\",\"map_sha256\":\""
			  << Disk::Hex(mapHash) << "\",\"rows\":" << receivers->size() << ",\"sources\":" << base->size()
			  << ",\"preparation_rays\":" << provider->calls << ",\"preparation_nodes\":" << provider->nodes
			  << ",\"preparation_triangles\":" << provider->triangles << ",\"visibility\":[";
	for (size_t j = 0; j < base->size(); ++j)
	{
		if (j)
			std::cout << ',';
		uint64_t v = 0, b = 0, u = 0;
		for (size_t i = 0; i < receivers->size(); ++i)
		{
			auto state = factors->Records()[i * base->size() + j].visibility;
			if (state == VisibilityState::Visible)
				++v;
			else if (state == VisibilityState::Blocked)
				++b;
			else
				++u;
		}
		std::cout << "{\"source\":" << (*base)[j].id << ",\"visible\":" << v << ",\"blocked\":" << b
				  << ",\"unknown\":" << u << '}';
	}
	std::cout << "],\"receiver_geometry\":[";
	for (size_t i = 0; i < receivers->size(); ++i)
	{
		if (i)
			std::cout << ',';
		const auto &r = (*receivers)[i];
		std::cout << '[' << r.sample << ',' << r.surface << ',' << r.world.x << ',' << r.world.y << ','
				  << r.world.z << ',' << r.geometricNormal.x << ',' << r.geometricNormal.y << ','
				  << r.geometricNormal.z << ']';
	}
	std::cout << "],\"candidate_geometry\":[";
	for (size_t i = 0; i < base->size(); ++i)
	{
		if (i)
			std::cout << ',';
		const auto &s = (*base)[i];
		std::cout << '[' << s.id << ',' << s.position.world.x << ',' << s.position.world.y << ','
				  << s.position.world.z << ']';
	}
	std::cout << "]}\n" << std::flush;
	std::string line;
	uint64_t calls = 0;
	while (std::getline(std::cin, line))
	{
		if (line == "quit")
			return 0;
		if (line.size() > (line.empty() || line[0] != 'F' ? 4096 : 65536) || ++calls > 8192)
			throw std::runtime_error("Server call/line budget");
		std::istringstream input(line);
		char command = 0;
		input >> command;
		auto sources = std::make_shared<std::vector<SourceHypothesis>>(*base);
		Recipe activeRecipe = recipe;
		Options active = options;
		ReadContext activeRead = read;
		FitCancelled cancel;
		provider->unknown = false;
		if (command == 'F')
		{
			RunAmplitudeFrame(input, profile, base, receivers, context, options, read, mapHash);
			continue;
		}
		if (command == 'A')
		{
			Vec(input, activeRecipe.explicitSurfaceAmbient);
			sources->clear();
			active.factors.reset();
		}
		else if (command == 'E' || command == 'R' || command == 'V' || command == 'C' || command == 'B' ||
				 command == 'U')
		{
			for (auto &s : *sources)
				Vec(input, s.compilerRgbAmplitude);
			if (command == 'V')
			{
				provider->unknown = true;
				active.factors.reset();
			}
			if (command == 'R')
				active.factors.reset(); // strict full production trace reevaluation
			if (command == 'C')
				activeRead.cancellation = &cancel;
			if (command == 'B')
				active.maxPairs = 0;
			if (command == 'U')
				active.required = {Feature::BounceFill};
		}
		else
			throw std::runtime_error("Unknown server operation");
		std::string extra;
		if (!input || (input >> extra))
			throw std::runtime_error("Amplitude frame dimensions");
		auto activeProfile = command == 'A' ? NetRadiantProfile(activeRecipe) : profile;
		Bake::Result result;
		auto start = std::chrono::steady_clock::now();
		e = evaluator.Evaluate(activeProfile, Lease(sources), Lease(receivers), Decode::Profile(), context,
							   active, activeRead, result);
		double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
		bool available = bool(e) && result.predictions.count == receivers->size();
		uint64_t unavailable = 0;
		for (size_t i = 0; i < result.predictions.count; ++i)
			if (!result.predictions.data[i].available)
			{
				available = false;
				++unavailable;
			}
		std::cout << "{\"type\":\"prediction\",\"call\":" << calls << ",\"code\":" << int(e.code)
				  << ",\"available\":" << (available ? "true" : "false") << ",\"unavailable\":" << unavailable
				  << ",\"profile_key\":\"" << Disk::Hex(result.profileKey)
				  << "\",\"pairs\":" << result.work.pairs << ",\"rays\":" << result.work.rays
				  << ",\"cache_hits\":" << result.work.cacheHits << ",\"nodes\":" << result.work.nodeTests
				  << ",\"triangles\":" << result.work.triangleTests << ",\"seconds\":" << seconds
				  << ",\"status\":[";
		for (size_t i = 0; i < result.predictions.count; ++i)
		{
			if (i)
				std::cout << ',';
			std::cout << int(result.predictions.data[i].status);
		}
		std::cout << "],\"rgb\":";
		if (!available)
			std::cout << "null";
		else
		{
			std::cout << '[';
			for (size_t i = 0; i < result.predictions.count; ++i)
			{
				if (i)
					std::cout << ',';
				const auto &p = result.predictions.data[i];
				std::cout << '[' << p.rgb.x << ',' << p.rgb.y << ',' << p.rgb.z << ']';
			}
			std::cout << ']';
		}
		std::cout << "}\n" << std::flush;
	}
	return 0;
}
