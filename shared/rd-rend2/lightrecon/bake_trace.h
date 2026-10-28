// Restricted, source-locked light_trace.c replay; NOT gameplay collision.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "bake_forward.h"
#include "regions.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace Bake
{
struct TraceLimits
{
	bool completeOpaqueSkyWorldGeometry =
		false; // explicit adapter coverage assertion, never inferred from triangles
	uint64_t maxNodes = 131072, maxFragments = 1048576, maxWork = 32ull * 1024 * 1024,
			 maxBytes = 128ull * 1024 * 1024;
};
class CompilerTrace
{
  public:
	// Verified scope is tied to content, not a user-assigned profile name.
	static Error Build(const BspSpaceInput &, const Chunk<TracePrimitive> &, const Disk::Digest &bspHash,
					   const TraceLimits &, const ReadContext &, std::shared_ptr<const CompilerTrace> &);
	Error Query(Vec3 start, Vec3 end, bool sky, const ReadContext &, uint64_t maxWork, RayResult &) const;
	PointDomain ClassifyOrigin(Vec3) const; // original BSP leaves, before trace-tree subdivision
	bool ScopedSunReplay() const { return scoped_; }
	const Disk::Digest &Key() const { return key_; }
	uint64_t OwnedBytes() const;
	size_t NodeCount() const;
	size_t TriangleCount() const;

  private:
	struct Data;
	std::shared_ptr<const Data> data_;
	Disk::Digest key_{};
	bool scoped_ = false;
};
} // namespace Bake
} // namespace Core
} // namespace Rend2LightRecon
