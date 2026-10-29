// Optional immutable geometry-evidence objective. No RGB readers or renderer state.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "bake_profile.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace FitEvidence
{
struct Source
{
	PhysicalEmitter geometry;
	Vec3 rgb;
	double legacySpotDistance = 64, legacySpotRadius = 32;
	Id emittingSurface = 0;
};
struct Value
{
	bool available = false, informative = false;
	bool approximation = false;
	double loss = 0;
	uint64_t rays = 0, work = 0, evaluations = 0, matched = 0, unresolved = 0;
};
class Objective
{
  public:
	virtual ~Objective() = default;
	virtual Disk::Digest Key() const = 0;
	virtual Disk::Digest MapKey() const = 0;
	virtual Disk::Digest GeometryKey() const = 0;
	virtual Error Evaluate(const std::vector<Source> &, const ReadContext &, Value &) const = 0;
	// Freeze source parameters before calling this. Default is suitable only for
	// objectives whose Evaluate already is their strictest supported value.
	virtual Error EvaluateReference(const std::vector<Source> &s, const ReadContext &r, Value &v) const
	{
		return Evaluate(s, r, v);
	}
};
} // namespace FitEvidence
} // namespace Core
} // namespace Rend2LightRecon
