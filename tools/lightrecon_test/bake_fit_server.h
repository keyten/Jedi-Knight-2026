// X07 test adapter, not a renderer or a source-discovery solver.
// Every prediction calls the production CpuForwardEvaluator. GPL-2.0-or-later.
#pragma once
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
		if (line.size() > 4096 || ++calls > 8192)
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
