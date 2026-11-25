#include "WorldSurfaceList.h"
#include "ModelOcclusionPolicy.h"
#undef NDEBUG
#include <cassert>
#include <chrono>
#include <cstdio>
#include <random>
#include <numeric>

int main()
{
	using namespace ModelOcclusionPolicy;
	View allowed = {true,true,false,true,true,false,false,false,false,false,false};
	assert(Eligible(allowed));
	for (bool View::* field : {&View::enabled, &View::prepass, &View::conditionalSupported, &View::main})
	{ View view = allowed; view.*field = false; assert(!Eligible(view)); }
	for (bool View::* field : {&View::comparison, &View::shadow, &View::portal, &View::sky,
		&View::skyOccluders, &View::refraction, &View::noWorld})
	{ View view = allowed; view.*field = true; assert(!Eligible(view)); }
	Surface expensive = {true,false,false,true,true,false,false,false,true,true,false,false,true,1024};
	assert(Eligible(expensive));
	for (bool Surface::* field : {&Surface::ghoul, &Surface::opaque, &Surface::simpleDepth,
		&Surface::matchingCull, &Surface::safeFlags, &Surface::buffers})
	{ Surface surface = expensive; surface.*field = false; assert(!Eligible(surface)); }
	for (bool Surface::* field : {&Surface::world, &Surface::post, &Surface::deforms, &Surface::offset,
		&Surface::distortion, &Surface::tessellation, &Surface::gore})
	{ Surface surface = expensive; surface.*field = true; assert(!Eligible(surface)); }
	expensive.indices = 1023; assert(!Eligible(expensive));
	WorldSurfaceList list;
	std::mt19937 random(0x51FACE);
	for (unsigned count : {0u, 1u, 63u, 64u, 65u, 4099u, 100003u})
	{
		list.Begin(count);
		std::vector<bool> expected(count, false);
		for (unsigned i = 0; count && i < count * 3; ++i)
		{
			const unsigned id = random() % count;
			list.Mark(id);
			list.Mark(id); // marks from overlapping leaves must not duplicate IDs
			expected[id] = true;
		}
		std::vector<unsigned> actual, reference;
		list.ForEach([&](unsigned id) { actual.push_back(id); });
		for (unsigned id = 0; id < count; ++id)
		{
			assert(list.Contains(id) == expected[id]);
			if (expected[id]) reference.push_back(id);
		}
		assert(actual == reference);
		// Preliminary bounds walks can mark more of the same view after listing.
		if (count) list.Mark(count / 2);
		list.Begin(count);
		actual.clear();
		list.ForEach([&](unsigned id) { actual.push_back(id); });
		assert(actual.empty());
		for (unsigned id = 0; id < count; ++id) assert(!list.Contains(id));
	}
	// Enumeration-only microbenchmark, including word sorting. Includes leaf
	// marking separately so dense-view bookkeeping cost is not hidden.
	const unsigned count = 200000, iterations = 1000;
	volatile uint64_t sink = 0;
	for (unsigned visible : {200u, 20000u, 180000u})
	{
		std::vector<unsigned> ids(count), stamps(count, 0);
		std::iota(ids.begin(), ids.end(), 0);
		std::shuffle(ids.begin(), ids.end(), random);
		ids.resize(visible);
		auto start = std::chrono::steady_clock::now();
		for (unsigned pass = 1; pass <= iterations; ++pass)
		{
			for (unsigned id : ids) stamps[id] = pass;
			for (unsigned id = 0; id < count; ++id) if (stamps[id] == pass) sink += id;
		}
		double reference = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / iterations;
		start = std::chrono::steady_clock::now();
		for (unsigned pass = 1; pass <= iterations; ++pass)
		{
			list.Begin(count);
			for (unsigned id : ids) list.Mark(id);
			list.ForEach([&](unsigned id) { sink += id; });
		}
		double compact = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start).count() / iterations;
		std::printf("%u/%u visible: reference %.2f us, compact %.2f us (mark + enumerate)\n", visible, count, reference, compact);
	}
	std::puts("PASS: occlusion view/surface admission, membership, deduplication, ordering, view/map reset and boundary sizes");
	return sink == 1;
}
