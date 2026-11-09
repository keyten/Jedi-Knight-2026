// Interpret a source polygon's side from captured geometry, never winding alone.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "surfaces.h"
#include "bake_math.h"
namespace Rend2LightRecon
{
namespace Core
{
namespace SourceOrientation
{
constexpr uint32_t kVersion = 2; // semantic identity; no archive/storage layout change
inline bool Resolve(const BakedSurface &s, const uint32_t *indices, Bake::Detail::V &out)
{
	using namespace Bake::Detail;
	out = V();
	for (unsigned k = 0; k < 3; ++k)
		if (indices[k] >= s.vertices.size())
			return false;
	V raw = Unit(Cross(V(s.vertices[indices[1]].position) - V(s.vertices[indices[0]].position),
					   V(s.vertices[indices[2]].position) - V(s.vertices[indices[0]].position)));
	if (!Normal(raw))
		return false;
	int sign = 0;
	auto agree = [&](V n) {
		if (!Finite(n) || Length(n) < .5)
			return false;
		double d = Dot(raw, Unit(n));
		if (std::abs(d) <= 1e-4)
			return false;
		int side = d > 0 ? 1 : -1;
		if (sign && side != sign)
			return false;
		sign = side;
		return true;
	};
	// As in MapLuxels, a face basis can supply missing vertex orientation.
	// For source support, contradictory records are never averaged away.
	V basis(s.lightmapBasis[2]);
	bool basisKnown = s.kind == BakedSurfaceKind::Face && Finite(basis) && Length(basis) >= .5;
	if (basisKnown && !agree(basis))
		return false;
	for (unsigned k = 0; k < 3; ++k)
	{
		V n(s.vertices[indices[k]].shadingNormal);
		if (!Finite(n))
			return false;
		if (Length(n) < .5)
		{
			if (!basisKnown)
				return false;
		}
		else if (!agree(n))
			return false;
	}
	// Cull names are the faces REMOVED by the captured renderer convention.
	if ((s.materialFacts.cull == SurfaceCull::Front && sign > 0) ||
		(s.materialFacts.cull == SurfaceCull::Back && sign < 0))
		return false;
	out = raw * double(sign);
	return sign != 0;
}
} // namespace SourceOrientation
} // namespace Core
} // namespace Rend2LightRecon
