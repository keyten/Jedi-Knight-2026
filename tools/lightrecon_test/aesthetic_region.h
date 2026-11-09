// Geometry-only bounded stock subset selection. Raw RGB is copied after membership freezes.
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lightrecon/geometry_key.h"
#include "source_footprint.h"
#include <array>
#include <map>
static unsigned RegionSplit(Vec3 position, double radius, Id &block)
{
	GeometryKey key;
	key.U(20261109);
	double xyz[3] = {position.x - 17, position.y - 31, position.z - 47};
	for (double x : xyz)
	{
		double c = std::floor(x / 256);
		if (std::min(x - c * 256, (c + 1) * 256 - x) <= 3 * radius + 1)
			return 3;
		key.U(uint64_t(int64_t(c)));
	}
	auto digest = key.Finish();
	block = 0;
	for (unsigned k = 0; k < 8; ++k)
		block |= uint64_t(digest[k]) << (8 * k);
	unsigned role = unsigned(block % 10);
	return role < 6 ? 0 : role < 8 ? 1 : 2;
}
static int ExportRegion(const Offline::Dataset &data, const Offline::Sources &source,
						const A::Request &request, uint32_t surface, double radius,
						const std::string &folder, const ReadContext &read)
{
	Need(std::isfinite(radius) && radius > 0 && radius <= 1024,
		 "Bounded fixture-region radius required");
	ShapeSelection::Request selection;
	selection.scene = request.scene;
	selection.fixture = Id(surface) + 1;
	selection.emittingSurfaces = {surface};
	ShapeSelection::Result ranked;
	Check(ShapeSelection::Rank(selection, read, ranked));
	const ShapeSelection::Rival *fixture = nullptr;
	for (const auto &rival : ranked.ranked)
		if (rival.kind == EmitterKind::Rectangle &&
			(rival.status == ShapeSelection::Status::Supported ||
			 rival.status == ShapeSelection::Status::Approximation))
		{
			fixture = &rival;
			break;
		}
	Need(fixture, "Region has no supported original rectangle bound");
	using Samples = std::map<Disk::Digest, Offline::Luxel>;
	std::map<std::string, std::array<Samples, 3>> materials;
	for (size_t entry = 0; entry < data.Directory().size(); ++entry)
		if (data.Directory()[entry].kind == 15)
		{
			std::vector<Offline::Luxel> tile;
			Check(data.ReadLuxels(entry, tile));
			for (const auto &l : tile)
			{
				const auto &g = l.surface;
				if (!g.positionKnown || !g.normalOrientationKnown ||
					g.coverage != LuxelCoverage::Mapped || !(g.areaWorldSquared > 0) ||
					g.surface == surface || l.colour.raw.encoding != Decode::Encoding::Rgb8)
					continue;
				V delta = V(fixture->emitter.geometry.position.world) - V(g.positionWorld);
				if (Length(delta) > radius || Dot(delta, V(g.geometricNormal)) <= 0)
					continue;
				const auto &s = source.geometry->Surfaces().at(g.surface);
				if (s.kind != BakedSurfaceKind::Face || s.motion != SurfaceMotion::StaticWorld ||
					l.classification.style.id != 0 ||
					(s.materialFacts.signals & (MaterialGlow | MaterialSky)) ||
					(g.issues & (LuxelOverlap | LuxelDuplicate | LuxelMultipleCharts |
								 LuxelMixedFootprint | LuxelPrecisionExceeded)))
					continue;
				double area = 0;
				auto footprint =
					SourceFootprint::Footprint(s, g.styleSlot, g.x, g.y, g.geometricNormal, area);
				double footprintRadius = 0;
				for (size_t t = 0; t < footprint.count; ++t)
					for (auto p : footprint.data[t].world)
						footprintRadius =
							std::max(footprintRadius, Length(V(p) - V(g.positionWorld)));
				Id block = 0;
				unsigned role = RegionSplit(g.positionWorld, footprintRadius, block);
				if (role == 3)
					continue;
				auto key = Offline::SampleIdentity(source.metadata.map,
												   source.images->Images().at(g.image), g,
												   l.classification.style.id);
				auto &samples = materials[s.material][role];
				samples.emplace(key, l);
				if (samples.size() > 48)
					samples.erase(--samples.end());
			}
		}
	const std::array<Samples, 3> *chosen = nullptr;
	std::string material;
	size_t count = 0;
	for (const auto &group : materials)
	{
		size_t minimum =
			std::min({group.second[0].size(), group.second[1].size(), group.second[2].size()});
		if (minimum > count)
		{
			chosen = &group.second;
			material = group.first;
			count = minimum;
		}
	}
	Need(chosen && count >= 4,
		 "No same-material train/validation/held-out receiver coverage in supplied region");
	auto output = [&](std::ostream &out, size_t role) {
		out << chosen->at(role).size() << '\n';
		for (const auto &item : chosen->at(role))
		{
			const auto &l = item.second;
			const auto &g = l.surface;
			Id id = 0;
			for (unsigned k = 0; k < 8; ++k)
				id |= uint64_t(item.first[k]) << (8 * k);
			if (!id)
				id = 1;
			Id block = 0;
			RegionSplit(g.positionWorld, 0, block);
			out << id << ' ' << block << ' ' << g.surface << ' ' << g.positionWorld.x << ' '
				<< g.positionWorld.y << ' ' << g.positionWorld.z << ' ' << g.geometricNormal.x
				<< ' ' << g.geometricNormal.y << ' ' << g.geometricNormal.z << ' '
				<< g.areaWorldSquared;
			for (unsigned c = 0; c < 3; ++c)
				out << ' ' << unsigned(l.colour.raw.bytes[c]);
			out << '\n';
		}
	};
	std::ofstream fit(folder + "/fit.txt"), held(folder + "/held.txt");
	fit << std::setprecision(17);
	held << std::setprecision(17);
	fit << "JKASR20-ENCODED-ASSUMED-MATERIAL-1\n1\n32 32 32\n1\nsurface " << selection.fixture
		<< ' ' << surface << " 100000 100000 100000\n";
	output(fit, 0);
	output(fit, 1);
	output(held, 2);
	Need(bool(fit) && bool(held), "Private stock region files not writable");
	std::cout << "{\"surface\":" << surface << ",\"fixture\":";
	Q(std::to_string(selection.fixture));
	std::cout << ",\"material\":";
	Q(material);
	std::cout << ",\"minimum_fold_rows\":" << count
			  << ",\"selection\":\"Geometry-only radius, facing receivers and stable sample hash; "
				 "same material maximizes minimum fold coverage\",\"mask\":\"Unknown whole-face "
				 "bound\",\"historical_source_truth\":\"Unavailable\"}\n";
	return 0;
}
