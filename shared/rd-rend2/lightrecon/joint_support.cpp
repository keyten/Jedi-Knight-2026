// Geometry-only receiver policy shared by fitting and trusted evaluation.
// SPDX-License-Identifier: GPL-2.0-or-later
#include "bake_math.h"
#include "joint.h"
#include <algorithm>
namespace Rend2LightRecon
{
namespace Core
{
namespace Joint
{
ReceiverSupport::Operator ReceiverOperator(const Profile &p,const Shape::Sampling&sampling)
{
	ReceiverSupport::Operator o;
	o.model = p.model;o.sampling=sampling;
	o.bake.output = p.contract.operation == Indirect::ObservationOperator::NetRadiantEncoded
						? Bake::Domain::EncodedRgb8BeforeQuantization
						: Bake::Domain::CompilerLinearRelative255;
	o.bake.allowAssumed = p.model.allowAssumed;o.bake.unpackedGridOnly=true;
	o.transferAttested = p.contract.transferAttested;
	o.primariesAttested = p.contract.primariesAttested;
	o.requiresMaterial = p.contract.operation == Indirect::ObservationOperator::OutgoingLambertian;
	o.scope = "Production Joint surface/unpacked-grid/point-spot-deluxe operators; compiler super1 or separately declared finite "
			  "physical integral; dynamic source paths checked at runtime";
	return o;
}
ReceiverSupport::Geometry ReceiverGeometry(const Request &r, const Observation &x)
{
	ReceiverSupport::Geometry g;
	g.receiver = x.receiver;
	if (x.channel == Channel::Surface && !x.transport)
	{
		for (const auto &p : r.transfer->Patches())
			if (p.id == x.patch)
			{
				g.receiver = p.receiver;
				break;
			}
	}
	g.receiver.sample = x.id;
	g.required = x.required;
	g.channelMask = 0;
	for (unsigned c = 0; c < 3; ++c)
		if (x.rgb[c].present)
			g.channelMask |= uint8_t(1u << c);
	g.region = x.region;
	g.block = x.block;
	g.role = ReceiverSupport::Role(unsigned(x.role));
	g.channel = ReceiverSupport::Channel(unsigned(x.channel));
	g.enabled = r.options.channelWeight[size_t(x.channel)] > 0;
	g.areaWorldSquared = x.receiverAreaWorldSquared;
	if (!g.areaWorldSquared && x.channel == Channel::Surface)
	{
		if (g.receiver.footprint.owner && g.receiver.footprint.data &&
			g.receiver.footprint.count <= 64)
		{
			for (size_t i = 0; i < g.receiver.footprint.count; ++i)
			{
				auto &t = g.receiver.footprint.data[i];
				g.areaWorldSquared +=
					Bake::Detail::Length(Bake::Detail::Cross(
						Bake::Detail::V(t.world[1]) - Bake::Detail::V(t.world[0]),
						Bake::Detail::V(t.world[2]) - Bake::Detail::V(t.world[0]))) *
					.5;
			}
		}
		else if (!x.transport)
			for (const auto &p : r.transfer->Patches())
				if (p.id == x.patch)
				{
					g.areaWorldSquared = p.areaWorldSquared;
					break;
				}
	}
	g.materialAvailable =
		std::any_of(r.materials.begin(), r.materials.end(), [&](const Indirect::Reflectance &m) {
			return m.material == g.receiver.material &&
				   (m.knowledge != Indirect::MaterialKnowledge::Unknown ||
					r.options.conditionalUnknownMaterials);
		});
	g.materialKnown =
		std::any_of(r.materials.begin(), r.materials.end(), [&](const Indirect::Reflectance &m) {
			return m.material == g.receiver.material &&
				   m.knowledge == Indirect::MaterialKnowledge::Known;
		});
	if (r.placement.geometry && g.receiver.surface &&
		g.receiver.surface <= r.placement.geometry->Surfaces().size())
		g.opticsKnown = r.placement.geometry->Surfaces()[size_t(g.receiver.surface - 1)].obstacle ==
						ObstacleKind::Opaque;
	return g;
}
Error FreezeReceiverSupport(const Request &r,
							std::shared_ptr<const ChunkSource<ReceiverSupport::Geometry>> rows,
							uint64_t count, const Disk::Digest &partition, const ReadContext &read,
							std::shared_ptr<const ReceiverSupport::Manifest> &out)
{
	ReceiverSupport::Request q;
	q.map = r.mapKey;
	q.geometry = r.geometry.geometryKey;
	q.partition = partition;
	q.policy = r.policyKey;
	q.context = r.geometry;
	q.domain=r.placement.domain;q.domainKey=r.placement.domainKey;
	q.maxRows = r.options.maxObservations;
	q.batchRows = r.options.batchRows;
	q.maxBytes = r.options.maxBytes;
	for (const auto &p : r.profiles)
		q.operators.push_back(ReceiverOperator(p,r.strictSampling));
	return ReceiverSupport::Build(q, rows, count, read, out);
}
} // namespace Joint
} // namespace Core
} // namespace Rend2LightRecon
