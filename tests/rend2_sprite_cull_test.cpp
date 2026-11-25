#include "tr_spritecull.h"
#include <cassert>
#include <cstdint>
#include <cstdio>

int main()
{
	using namespace SpriteCull;
	Cursor cursor = {};
	assert(Reserve(cursor, 5, 60, 12));
	assert(cursor.lists == 30 && cursor.commands == 6);
	assert(!Reserve(cursor, 6, 60, 12));
	assert(cursor.lists == 30 && cursor.commands == 6);
	assert(Reserve(cursor, 5, 60, 12)); // exact end is valid
	assert(cursor.lists == 60 && cursor.commands == 12);
	assert(!Reserve(cursor, 1, 60, 12));
	assert(!Reserve(cursor, 0, 60, 12));
	Cursor commands = {0, 7};
	assert(!Reserve(commands, 1, 60, 12));
	assert(commands.lists == 0 && commands.commands == 7);
	Cursor invalid = {61, 13};
	assert(!Reserve(invalid, UINT32_MAX, 60, 12));
	assert(invalid.lists == 61 && invalid.commands == 13);
	Cursor large = {};
	assert(!Reserve(large, UINT32_MAX, UINT32_MAX, UINT32_MAX));
	assert(large.lists == 0 && large.commands == 0);
	const uint32_t maximum = UINT32_MAX / BinCount;
	assert(Reserve(large, maximum, UINT32_MAX, BinCount));
	assert(large.lists == maximum * BinCount && large.commands == BinCount);
	assert(!Reserve(large, 1, UINT32_MAX, BinCount));
	// Exhaustive small arenas: reservation succeeds iff both complete
	// worst-case allocations fit; the failure state is never partial.
	for (uint32_t capacity = 0; capacity < 90; ++capacity)
		for (uint32_t used = 0; used <= capacity + 1; ++used)
			for (uint32_t plants = 0; plants < 25; ++plants)
			{
				Cursor value = {used, 0};
				const bool expected = plants > 0 && uint64_t(used) + uint64_t(plants) * 6 <= capacity;
				assert(Reserve(value, plants, capacity, 6) == expected);
				assert(value.lists == (expected ? used + plants * 6 : used));
				assert(value.commands == (expected ? 6u : 0u));
			}
	std::puts("PASS complete-surface reservation, capacity boundaries and integer overflow");
}
