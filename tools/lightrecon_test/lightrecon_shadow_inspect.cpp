// Bounded training-only archive diagnostic. Supplied source XYZ is a test prior.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/offline.h"
#include "lightrecon/shadow_evidence.h"
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace H = Core::Shadow;
void Check(Error e)
{
	if (!e)
		throw std::runtime_error(e.message);
}
int main(int argc, char **argv)
{
	try
	{
		if (argc != 6)
		{
			std::cerr
				<< "usage: lightrecon_shadow_inspect DATASET SOURCE_X SOURCE_Y SOURCE_Z OUTPUT_PREFIX\n";
			return 2;
		}
		ReadContext read;
		read.maxBytes = 128ull * 1024 * 1024;
		std::shared_ptr<const Offline::Dataset> data;
		Check(Offline::OpenFile(argv[1], {}, data));
		Offline::Sources sources;
		Offline::RegionSnapshot regions;
		Check(data->Load(sources, regions));
		std::shared_ptr<const StaticVisibility> visibility;
		Check(StaticVisibility::Build(sources.geometry, {}, read, visibility, sources.domain));
		auto samples = std::make_shared<std::vector<H::Sample>>();
		uint64_t requested = 0, excluded = 0, withheld = 0;
		Id id = 0;
		for (size_t i = 0; i < data->Directory().size(); ++i)
			if (data->Directory()[i].kind == 15)
			{
				std::vector<Offline::Luxel> tile;
				Check(data->ReadLuxels(i, tile));
				for (const auto &l : tile)
				{
					++requested;
					if (l.partition != Offline::Partition::Train)
					{
						++withheld;
						continue;
					}
					const auto &g = l.surface;
					if (g.coverage != LuxelCoverage::Mapped || !g.positionKnown || !g.shadingNormalKnown ||
						!g.normalOrientationKnown || g.surface >= sources.geometry->Surfaces().size() ||
						l.colour.source.state != Decode::State::Available ||
						l.colour.resolvedTransfer != Decode::Transfer::Linear ||
						l.colour.resolvedPrimaries == Decode::Primaries::Unknown)
					{
						++excluded;
						continue;
					}
					const auto &s = sources.geometry->Surfaces()[g.surface];
					if (s.motion != SurfaceMotion::StaticWorld || s.excludedMaterial ||
						(s.kind != BakedSurfaceKind::Face && s.kind != BakedSurfaceKind::Triangles) ||
						s.styles[g.styleSlot] != 0)
					{
						++excluded;
						continue;
					}
					H::Sample a;
					auto identity = Offline::SampleIdentity(
						sources.metadata.map, sources.images->Images()[g.image], g, s.styles[g.styleSlot]);
					id = 0;
					for (unsigned j = 0; j < 8; ++j)
						id |= Id(identity[j]) << (8 * j);
					if (!id)
						id = 1;
					a.id = id;
					a.chart = Id(g.surface) * 4 + g.styleSlot + 1;
					a.material = Id(g.materialIndex) + 1;
					a.page = g.logicalLightmap;
					a.x = g.x;
					a.y = g.y;
					a.receiver.sample = id;
					a.receiver.surface = Id(g.surface) + 1;
					a.receiver.world = g.positionWorld;
					a.receiver.shadingNormal = g.shadingNormal;
					a.receiver.geometricNormal = g.geometricNormal;
					a.rgb = l.colour.source.rgb;
					a.sourceLinear = a.primariesComparable = true;
					a.filterKnown = false;
					a.sigmaRgb = .5 / 255;
					a.mappingErrorWorld = g.mappingErrorBoundWorld;
					if (l.colour.source.censoredChannels)
						a.issues |= H::SaturationEdge;
					if (g.issues &
						(LuxelOverlap | LuxelMultipleCharts | LuxelNormalSeam | LuxelMixedFootprint))
						a.issues |= H::ChartSeam;
					samples->push_back(a);
					if (samples->size() > 200000)
						throw std::runtime_error("Diagnostic sample cap; select a bounded archive");
				}
			}
		H::DetectionOptions detection;
		std::shared_ptr<const H::Evidence> evidence;
		Check(H::Detect(sources.metadata.map.sha256,
						sources.metadata.decoding.identity.empty()
							? Disk::Hash("archive-decoder", 15)
							: Disk::Hash(sources.metadata.decoding.identity.data(),
										 sources.metadata.decoding.identity.size()),
						VectorChunk<H::Sample>(samples, 0, samples->size()), detection, read, evidence));
		H::Context context;
		context.observations = evidence;
		context.visibility = visibility;
		context.moving = sources.dynamic;
		context.policy.scope = H::Scope::CentreVisibility;
		context.policy.provenance = "Archive source-domain training-only diagnostic. Supplied finite point "
									"prior; physical BVH centre visibility, not compiler shadow replay.";
		context.policy.units = sources.metadata.units;
		context.policy.searchRadiusWorld = 96;
		context.policy.maxRays = 500000;
		context.policy.maxWork = 64000000;
		std::shared_ptr<const H::Objective> objective;
		Check(H::Objective::Build(context, read, objective));
		FitEvidence::Source source;
		source.geometry.source.id = 1;
		source.geometry.kind = EmitterKind::Point;
		source.geometry.position.known = true;
		source.geometry.position.world =
			Vec3(float(std::stod(argv[2])), float(std::stod(argv[3])), float(std::stod(argv[4])));
		source.geometry.angular.kind = AngularKind::Isotropic;
		source.rgb = Vec3(1, 1, 1);
		H::Report report;
		Check(objective->Inspect({source}, read, report));
		std::string text;
		Check(objective->DebugTsv(report, 16 * 1024 * 1024, text));
		std::ofstream(std::string(argv[5]) + ".tsv") << text;
		Check(objective->DebugObj(report, 16 * 1024 * 1024, text));
		std::ofstream(std::string(argv[5]) + ".obj") << text;
		std::ofstream out(std::string(argv[5]) + ".json");
		out << std::setprecision(12)
			<< "{\"schema\":\"org.openjk.shadow-archive-diagnostic.v1\",\"training_only\":true,\"compiler_"
			   "faithful\":false,\"requested\":"
			<< requested << ",\"withheld\":" << withheld << ",\"excluded\":" << excluded
			<< ",\"samples\":" << samples->size() << ",\"gradients\":" << evidence->gradients.size()
			<< ",\"edges\":" << evidence->edges.size() << ",\"eligible\":" << report.eligibleEdges
			<< ",\"matched\":" << report.value.matched << ",\"unassigned\":" << report.unassigned
			<< ",\"unknown\":" << report.unknown << ",\"attempts\":" << report.associationAttempts
			<< ",\"omitted_diagnostics\":" << report.omittedDiagnostics << ",\"rays\":" << report.value.rays
			<< ",\"work\":" << report.value.work << ",\"loss\":" << report.value.loss << "}\n";
		return 0;
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		return 1;
	}
}
