// Compact BSP surface membership, independent of renderer globals for tests.
#ifndef REND2_WORLD_SURFACE_LIST_H
#define REND2_WORLD_SURFACE_LIST_H
#include <algorithm>
#include <cstdint>
#include <vector>
#ifdef _MSC_VER
#include <intrin.h>
#endif

class WorldSurfaceList
{
	std::vector<uint64_t> words;
	std::vector<unsigned> activeWords;
public:
	void Begin(unsigned count)
	{
		for (unsigned word : activeWords) words[word] = 0;
		activeWords.clear();
		words.resize((count + 63u) / 64u, 0);
	}
	bool Mark(unsigned id)
	{
		uint64_t& word = words[id >> 6];
		const uint64_t bit = uint64_t(1) << (id & 63);
		if (word & bit) return false;
		if (!word) activeWords.push_back(id >> 6);
		word |= bit;
		return true;
	}
	bool Contains(unsigned id) const
	{
		return (words[id >> 6] & (uint64_t(1) << (id & 63))) != 0;
	}
	template<class F> void ForEach(F&& add)
	{
		auto wordEntries = [&](unsigned index)
		{
			uint64_t bits = words[index];
			while (bits)
			{
				unsigned shift;
#ifdef _MSC_VER
				unsigned long bit;
#if defined(_M_X64) || defined(_M_ARM64)
				_BitScanForward64(&bit, bits);
#else
				if (uint32_t(bits)) _BitScanForward(&bit, uint32_t(bits));
				else { _BitScanForward(&bit, uint32_t(bits >> 32)); bit += 32; }
#endif
				shift = bit;
#else
				shift = __builtin_ctzll(bits);
#endif
				add((index << 6) + shift);
				bits &= bits - 1;
			}
		};
		if (activeWords.size() > words.size() / 4)
		{
			// A dense view walks the small bitset (1/32 of the stamp array),
			// avoiding a sort of almost every occupied word.
			for (unsigned index = 0; index < words.size(); ++index) wordEntries(index);
		}
		else
		{
			std::sort(activeWords.begin(), activeWords.end());
			for (unsigned index : activeWords) wordEntries(index);
		}
	}
};
#endif
