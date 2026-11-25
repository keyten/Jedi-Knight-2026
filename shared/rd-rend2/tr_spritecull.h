#ifndef REND2_SPRITECULL_H
#define REND2_SPRITECULL_H
#include <cstdint>

// A surface reserves all six worst-case lists before dispatch. Failed
// reservations leave the cursor unchanged, so fallback can draw it in full.
namespace SpriteCull
{
constexpr uint32_t BinCount = 6;
struct Cursor { uint32_t lists, commands; };
inline bool Reserve(Cursor &cursor, uint32_t plants, uint32_t listCapacity, uint32_t commandCapacity)
{
	if (!plants || cursor.lists > listCapacity || cursor.commands > commandCapacity ||
		plants > (listCapacity - cursor.lists) / BinCount ||
		commandCapacity - cursor.commands < BinCount)
		return false;
	cursor.lists += plants * BinCount;
	cursor.commands += BinCount;
	return true;
}
}
#endif
