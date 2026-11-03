// Content identities for offline reconstruction and reusable CPU layers.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "disk.h"
#include <memory>
namespace Rend2LightRecon
{
namespace Disk
{
namespace Dependencies
{
constexpr uint32_t kVersion = 1;
struct Recipe
{
	std::string materialContext, decoder, geometry, profiles, solver, settings, calibration;
	Units units;
};
// Resolves the SAME immutable VFS/material context as the producer. Missing is
// explicit absence; an I/O/unsupported error is never an absent dependency.
class Sources
{
  public:
	virtual ~Sources() = default;
	virtual Error Open(const std::string &, std::unique_ptr<Reader> &) const = 0;
	virtual Error Material(const std::string &, std::string &effectiveDefinition) const = 0;
};
struct Budget
{
	uint64_t maxBspBytes = 256ull << 20, maxContentBytes = 2ull << 30, maxFileBytes = 256ull << 20;
	uint32_t maxFiles = 32768, maxMaterials = 16384, maxLightmaps = 8192;
};
struct Work
{
	uint64_t files = 0, missing = 0, hashedBytes = 0, materials = 0;
	double seconds = 0;
};
// Reads only raw RBSP inventory and content, not luxel derivation/BVH/fitting.
// Output is unchanged on failure. Original BSP entities/geometry/embedded
// lightmaps/grid are covered by the whole BSP digest. Absences are keyed too.
Error Collect(const std::string &map, const Reader &bsp, const Sources &, const Recipe &,
			  Fingerprint &, Work &, const Budget & = Budget());

enum class Layer
{
	Probes,
	Field,
	Visibility,
	Contributions,
	Compression
};
struct LayerRecipe
{
	Layer layer = Layer::Probes;
	uint32_t version = 0;
	std::string algorithm, settings;
	// Named immutable content/dependency hashes. The required dependencies below
	// are checked; callers also include any additional inputs their recipe uses.
	std::vector<InputContent> dependencies;
};
// Separate .jklr identities, never GPU resource identities. Probes require
// geometry/optics; visibility adds source-geometry/scenario; fields require
// canonical-result/probes; contributions require canonical-result/observations;
// compression requires uncompressed. Parent MakeKey must be complete.
Error Derive(const Fingerprint &source, const LayerRecipe &, Fingerprint &);
} // namespace Dependencies
} // namespace Disk
} // namespace Rend2LightRecon
