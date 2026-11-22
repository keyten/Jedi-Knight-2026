#include "fx/ParticleOrder.h"
#include <cassert>
#include <cstdio>
#include <random>
#include <vector>
#include <limits>

int main()
{
	using ParticleOrder::Entry;
	const Entry example[] = {{0,0,0}, {20,1,1}, {40,2,0}, {20,1,0}, {0,0,0}};
	uint32_t order[] = {0,1,2,3,4}, scratch[10];
	ParticleOrder::SortLayer(order, 5, scratch, [&](unsigned i){return example[i];});
	assert(order[0] == 0 && order[4] == 4); // ordinary surfaces retain their slots
	assert(order[1] == 2 && order[2] == 3 && order[3] == 1); // whole far particle, then near stages
	std::mt19937 rng(6432);
	for (unsigned trial = 0; trial < 1000; ++trial)
	{
		std::vector<Entry> entries;
		for (unsigned i = 0; i < 64; ++i)
		{
			const float depth = float(rng() % 50);
			for (unsigned stage = 0; stage < 3; ++stage) entries.push_back({depth, i+1, stage});
			if (i % 3 == 0) entries.push_back({0,0,0});
		}
		std::vector<uint32_t> indices(entries.size()), work(entries.size()*2);
		for (unsigned i = 0; i < indices.size(); ++i) indices[i] = i;
		std::shuffle(indices.begin(), indices.end(), rng);
		const auto before = indices;
		ParticleOrder::SortLayer(indices.data(), indices.size(), work.data(), [&](unsigned i){return entries[i];});
		Entry previous{std::numeric_limits<float>::infinity(),0,0};
		for (unsigned slot = 0; slot < indices.size(); ++slot)
		{
			Entry e = entries[indices[slot]];
			if (!e.owner) {assert(indices[slot] == before[slot]); continue;}
			assert(e.depth <= previous.depth);
			if (e.owner == previous.owner) assert(e.stage > previous.stage);
			previous = e;
		}
	}
	Entry invalid{std::numeric_limits<float>::quiet_NaN(),3,0};
	assert(!ParticleOrder::Before(invalid, invalid));
	std::puts("PASS: 1000 shuffled multi-pass layers, stable ordinary slots and depth order");
}
