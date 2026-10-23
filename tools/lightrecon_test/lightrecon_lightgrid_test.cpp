// Synthetic original evidence only. No engine/filesystem/GL/geometry dependencies.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/lightgrid.h"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244) // original sin(double) -> float table assignment, test oracle only
#endif
#ifdef __GNUC__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion" // original renderer table narrowing, oracle only
#pragma GCC diagnostic ignored "-Wfloat-conversion"
#endif
#include "grid_renderer_reference.h"
#ifdef __GNUC__
#pragma GCC diagnostic pop
#endif
#ifdef _MSC_VER
#pragma warning(pop)
#endif
using namespace Rend2LightRecon;
using namespace Rend2LightRecon::Core;
namespace
{
int checks = 0, failures = 0;
#define CHECK(x)                                                                                   \
	do                                                                                             \
	{                                                                                              \
		++checks;                                                                                  \
		if (!(x))                                                                                  \
		{                                                                                          \
			++failures;                                                                            \
			std::printf("FAIL %d: %s\n", __LINE__, #x);                                            \
		}                                                                                          \
	} while (0)
static_assert(std::is_const<decltype(LightgridCapture().Acquire())::element_type>::value,
			  "immutable publication");
static_assert(std::is_same<decltype(std::declval<const LightgridObservations>().Records()),
						   const std::vector<uint8_t> &>::value,
			  "read-only raw storage");
bool close(float a, float b, float tolerance = 1e-5f)
{
	return std::abs(a - b) < tolerance;
}
LightmapMapIdentity identity()
{
	LightmapMapIdentity m;
	m.path = "maps/synthetic.bsp";
	m.bytes = 4096;
	m.sha256 = Disk::Hash("synthetic", 9);
	return m;
}
GridLayout layout(uint32_t x = 2, uint32_t y = 2, uint32_t z = 2)
{
	GridLayout l;
	l.originWorld.x = -64;
	l.originWorld.y = 32;
	l.originWorld.z = -128;
	l.spacingWorld.x = 64;
	l.spacingWorld.y = 32;
	l.spacingWorld.z = 128;
	l.bounds[0] = x;
	l.bounds[1] = y;
	l.bounds[2] = z;
	return l;
}
std::vector<uint8_t> records()
{
	std::vector<uint8_t> r(3 * kGridRecordBytes);
	for (size_t i = 0; i < r.size(); ++i)
		r[i] = static_cast<uint8_t>(i + 1);
	for (size_t i = 0; i < 3; ++i)
	{
		auto p = r.data() + i * kGridRecordBytes;
		std::memset(p + 24, 255, 4);
		p[24] = 0;
		p[28] = 64;
		p[29] = 0;
	}
	r[30 + 24] = 7;
	r[30 + 25] = 0;
	r[30 + 28] = 64;
	r[30 + 29] = 64;
	std::memset(r.data() + 60 + 24, 255, 4);
	r[60 + 28] = 0;
	r[60 + 29] = 0;
	return r;
}
std::vector<uint8_t> indices()
{
	return {0, 0, 1, 0, 0, 0, 2, 0, 1, 0, 0, 0, 2, 0, 1, 0};
}
void source(LightgridCapture &c, const std::vector<uint8_t> &r, const std::vector<uint8_t> &i)
{
	c.Begin(BuildPolicy::ForceRebuild);
	CHECK(c.SetSource(identity(), r.data(), r.size(), 128, i.data(), i.size(), 1024));
}
void normal()
{
	auto r = records(), i = indices();
	auto original = r;
	LightgridCapture c;
	source(c, r, i);
	CHECK(!c.Acquire());
	CHECK(c.SetLayout(layout()));
	std::vector<float> hdr(8 * 6);
	for (size_t n = 0; n < hdr.size(); ++n)
		hdr[n] = static_cast<float>(n) * .25f;
	CHECK(c.SetHdr(GridHdrState::Present, "maps/synthetic/lightgrid.raw", hdr.data(),
				   hdr.size() * 4));
	CHECK(c.Freeze());
	auto o = c.Acquire();
	CHECK(!!o);
	CHECK(o->Map().sha256 == identity().sha256);
	CHECK(o->RecordOffset() == 128);
	CHECK(o->IndexOffset() == 1024);
	CHECK(o->Records() == r);
	CHECK(o->Indices() == i);
	CHECK(o->Hdr() == hdr);
	CHECK(o->Report().bytes == 90 + 16 + 192);
	CHECK(o->Report().cells == 8);
	CHECK(o->Report().records == 3);
	CHECK(o->Report().noStyle == 2);
	std::memset(r.data(), 0, r.size());
	std::memset(i.data(), 0, i.size());
	std::fill(hdr.begin(), hdr.end(), -1.0f);
	for (uint64_t n = 0; n < 8; ++n)
	{
		GridCell cell;
		CHECK(o->Cell(n, cell));
		CHECK(cell.ordinal == n);
		CHECK(cell.coordinates[0] == n % 2);
		CHECK(cell.coordinates[1] == (n / 2) % 2);
		CHECK(cell.coordinates[2] == n / 4);
		CHECK(close(cell.centreWorld.x, -64 + 64 * float(n % 2)));
		CHECK(close(cell.centreWorld.y, 32 + 32 * float((n / 2) % 2)));
		CHECK(close(cell.centreWorld.z, -128 + 128 * float(n / 4)));
		CHECK(cell.presence == GridPresence::Present);
		CHECK(cell.indexPresent);
		CHECK(cell.sourceRecord == indices()[size_t(n) * 2]);
		CHECK(std::memcmp(cell.ambient, original.data() + cell.sourceRecord * 30, 12) == 0);
		CHECK(std::memcmp(cell.directed, original.data() + cell.sourceRecord * 30 + 12, 12) == 0);
		CHECK(std::memcmp(cell.styles, original.data() + cell.sourceRecord * 30 + 24, 4) == 0);
		CHECK(cell.hdrPresent);
		for (size_t ch = 0; ch < 6; ++ch)
			CHECK(close(cell.hdr[ch], float(n * 6 + ch) * .25f));
		CHECK(cell.geometry == GridGeometry::Unknown);
		CHECK(!cell.suspectedInvalid);
		CHECK(!cell.direction.confidenceKnown);
	}
	GridCell wall;
	CHECK(o->Cell(0, wall)); // fixture declared inside a wall by external geometry
	// Independent synthetic solid box enclosing this centre. The extractor has
	// no geometry provider and must not claim this bright/styled cell is free.
	CHECK(wall.centreWorld.x > -80 && wall.centreWorld.x < -48);
	CHECK(wall.centreWorld.y > 16 && wall.centreWorld.y < 48);
	CHECK(wall.centreWorld.z > -140 && wall.centreWorld.z < -112);
	CHECK(wall.styles[0] == 0 && wall.ambient[0][0] > 0 && wall.directed[0][0] > 0);
	CHECK(wall.geometry == GridGeometry::Unknown);
	CHECK(wall.presence == GridPresence::Present);
	CHECK(!wall.suspectedInvalid);
	GridCell none;
	CHECK(o->Cell(3, none));
	CHECK(none.styleStatus == GridStyle::None);
	CHECK(none.direction.quality == GridDirectionQuality::QuantizedUncalibrated);
	CHECK(close(none.direction.toSource.z, 1)); // 0,0 is north, NOT missing
	CHECK(o->Cell(8, none).code == Code::InvalidInput);
	CHECK(none.presence == GridPresence::Absent);
	c.Reset();
	CHECK(!c.Acquire());
	CHECK(o->Cell(7, none));
	CHECK(none.hdrPresent);
	InputDataset input;
	input.lightgrid = o;
	InputReader reader(input);
	CHECK(reader.Lightgrid() == o);
}
void malformed()
{
	auto r = records(), i = indices();
	i[2] = 255;
	i[3] = 255;
	i.resize(9);
	r.push_back(42);
	r[25] = 255;
	r[26] = 5; // active slot following sentinel: keep all bytes
	LightgridCapture c;
	source(c, r, i);
	CHECK(c.SetLayout(layout()));
	float hdr[] = {1, 2, 3, 4, std::numeric_limits<float>::infinity(), 6, 1, 2, 3, -1, 5, 6, 42};
	CHECK(c.SetHdr(GridHdrState::Present, "partial.raw", hdr, sizeof(hdr)));
	CHECK(c.Freeze());
	auto o = c.Acquire();
	CHECK(o->Records().back() == 42);
	CHECK(o->Indices().size() == 9);
	CHECK(o->Hdr().back() == 42);
	CHECK(o->Report().issues & GridRecordTail);
	CHECK(o->Report().issues & GridIndexSize);
	CHECK(o->Report().issues & GridHdrSize);
	CHECK(o->Report().invalidIndices == 1);
	CHECK(o->Report().missingCells == 5);
	CHECK(o->Report().nonFiniteHdr == 1);
	CHECK(o->Report().negativeHdr == 1);
	GridCell cell;
	CHECK(o->Cell(0, cell));
	CHECK(cell.styleStatus == GridStyle::NonContiguous);
	CHECK(cell.styles[2] == 5);
	CHECK(cell.issues & GridStyleGap);
	CHECK(cell.suspectedInvalid);
	CHECK(cell.geometry == GridGeometry::Unknown);
	CHECK(cell.hdrPresent);
	Decode::Decoder decoder;
	Decode::Observation value;
	CHECK(!DecodeGridHdr(decoder, cell, Decode::Component::GridDirected, value));
	CHECK(value.channels[1] & Decode::NonFinite);
	CHECK(std::isinf(value.raw.hdr.y));
	CHECK(o->Cell(1, cell));
	CHECK(cell.presence == GridPresence::InvalidIndex);
	CHECK(cell.sourceRecord == 65535);
	CHECK(cell.hdrPresent);
	CHECK(cell.direction.quality == GridDirectionQuality::Unavailable);
	CHECK(DecodeGridLdr(decoder, cell, 0, Decode::Component::GridAmbient, value).code ==
		  Code::Unavailable);
	CHECK(value.encoded.reason == Decode::Reason::Missing);
	CHECK(!DecodeGridHdr(decoder, cell, Decode::Component::GridDirected, value));
	CHECK(value.channels[0] & Decode::Negative);
	CHECK(o->Cell(4, cell));
	CHECK(cell.presence == GridPresence::MissingIndex);
	CHECK(!cell.hdrPresent);
	CHECK(DecodeGridHdr(decoder, cell, Decode::Component::GridAmbient, value).code ==
		  Code::Unavailable);
	CHECK(value.source.reason == Decode::Reason::Missing);
}
void decoding()
{
	auto r = records(), i = indices();
	r[0] = 255;
	r[1] = 128;
	r[2] = 0;
	LightgridCapture c;
	source(c, r, i);
	CHECK(c.SetLayout(layout()));
	CHECK(c.SetHdr(GridHdrState::Missing, "missing.raw", nullptr, 0));
	CHECK(c.Freeze());
	GridCell cell;
	CHECK(c.Acquire()->Cell(0, cell));
	Decode::Decoder unknown;
	Decode::Observation v;
	CHECK(DecodeGridLdr(unknown, cell, 0, Decode::Component::GridAmbient, v));
	CHECK(v.source.reason == Decode::Reason::UnknownTransfer);
	CHECK(v.channels[0] & Decode::Saturated);
	CHECK(v.encoded.censoredChannels == 1);
	CHECK(v.raw.bytes[1] == 128);
	Decode::Profile p;
	p.source.assumedTransfer = Decode::Transfer::Linear;
	p.source.assumptionRationale = "synthetic linear fixture";
	Decode::Decoder linear(p);
	CHECK(linear.Validation());
	CHECK(DecodeGridLdr(linear, cell, 0, Decode::Component::GridAmbient, v));
	CHECK(close(v.source.rgb.y, 128.f / 255));
	CHECK(v.absoluteScale == Decode::AbsoluteScale::Unknown);
	CHECK(DecodeGridLdr(linear, cell, 1, Decode::Component::GridDirected, v));
	CHECK(v.raw.bytes[0] == r[15]);
	CHECK(!DecodeGridLdr(linear, cell, 4, Decode::Component::GridAmbient, v));
	CHECK(!DecodeGridLdr(linear, cell, 0, Decode::Component::Lightmap, v));
	// Preserve invalid HDR bits as evidence; never publish them as decoded lighting.
	float raw[6] = {};
	const uint32_t nanBits = 0x7fc12345;
	std::memcpy(raw, &nanBits, sizeof(nanBits));
	source(c, r, i);
	CHECK(c.SetLayout(layout(1, 1, 1)));
	CHECK(c.SetHdr(GridHdrState::Present, "nan.raw", raw, sizeof(raw)));
	CHECK(c.Freeze());
	CHECK(std::memcmp(c.Acquire()->Hdr().data(), raw, sizeof(raw)) == 0);
	CHECK(c.Acquire()->Cell(0, cell));
	CHECK(cell.hdrPresent && (cell.issues & GridHdrNonFinite));
	CHECK(!DecodeGridHdr(unknown, cell, Decode::Component::GridAmbient, v));
	CHECK(v.channels[0] & Decode::NonFinite);
}
void directions()
{
	const uint8_t pairs[][2] = {{64, 0}, {64, 64}, {0, 0}, {64, 128}, {64, 192}, {128, 0}};
	const float axes[][3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}, {-1, 0, 0}, {0, -1, 0}, {0, 0, -1}};
	for (size_t n = 0; n < 6; ++n)
	{
		auto d = DecodeGridDirection(pairs[n][0], pairs[n][1]);
		CHECK(close(d.toSource.x, axes[n][0]));
		CHECK(close(d.toSource.y, axes[n][1]));
		CHECK(close(d.toSource.z, axes[n][2]));
	}
	constexpr double pi = 3.14159265358979323846;
	for (unsigned p = 0; p < 256; ++p)
		for (unsigned a = 0; a < 256; ++a)
		{
			auto d = DecodeGridDirection(static_cast<uint8_t>(p), static_cast<uint8_t>(a));
			const auto &v = d.toSource;
			CHECK(close(v.x * v.x + v.y * v.y + v.z * v.z, 1));
			const auto &e = d.entityToSource;
			const uint8_t encoded[2] = {static_cast<uint8_t>(p), static_cast<uint8_t>(a)};
			float reference[3];
			GridReference::Decode(encoded, reference);
			CHECK(close(e.x, reference[0], 2e-6f) && close(e.y, reference[1], 2e-6f) &&
				  close(e.z, reference[2], 2e-6f));
			CHECK(close(e.x * e.x + e.y * e.y + e.z * e.z, 1));
			CHECK(v.x * e.x + v.y * e.y + v.z * e.z > std::cos(.02));
			// Encode canonical unit vector to equivalent polar/azimuth pair. Pole and p>128
			// aliases are equivalent directions rather than necessarily identical bytes.
			const double polar =
				std::acos(std::fmax(-1., std::fmin(1., double(v.z)))) * 256 / (2 * pi);
			double az = std::atan2(v.y, v.x) * 256 / (2 * pi);
			if (az < 0)
				az += 256;
			auto again = DecodeGridDirection(static_cast<uint8_t>(std::lround(polar)),
											 static_cast<uint8_t>(std::lround(az) & 255));
			CHECK(v.x * again.toSource.x + v.y * again.toSource.y + v.z * again.toSource.z >
				  1 - 1e-5);
			CHECK(d.errorBoundRadians > d.stepRadians);
			CHECK(d.quality == GridDirectionQuality::QuantizedUncalibrated);
		}
}
void limitsAndLifecycle()
{
	auto r = records(), i = indices();
	for (int policy = 0; policy < 4; ++policy)
		for (bool hit : {false, true})
		{
			LightgridCapture c;
			c.Begin(static_cast<BuildPolicy>(policy), hit);
			CHECK(c.Needed() == (policy == 3 || (policy == 2 && !hit)));
			CHECK(!c.Acquire());
			CHECK(c.Report().bytes == 0);
		}
	for (unsigned n = 0; n < 100; ++n)
	{
		LightgridCapture c;
		source(c, r, i);
		CHECK(c.SetLayout(layout()));
		CHECK(c.Freeze());
		auto old = c.Acquire();
		c.Begin(BuildPolicy::Disabled);
		CHECK(!c.Acquire());
		GridCell cell;
		CHECK(old->Cell(0, cell));
		CHECK(old->Map().path == identity().path);
	}
	LightgridCapture c;
	c.Begin(BuildPolicy::ForceRebuild);
	CHECK(!c.Freeze());
	CHECK(c.Report().state == CaptureState::Failed);
	source(c, {}, {});
	CHECK(c.SetLayout(layout(0, 0, 0)));
	CHECK(c.Freeze());
	CHECK(c.Acquire()->Report().cells == 0);
	source(c, {}, {});
	CHECK(c.SetLayout(layout()));
	CHECK(c.Freeze());
	GridCell cell;
	CHECK(c.Acquire()->Cell(0, cell));
	CHECK(cell.presence == GridPresence::Absent);
	CHECK(cell.geometry == GridGeometry::Unknown);
	GridLimits limit;
	limit.maxBytes = 100;
	LightgridCapture small(limit);
	source(small, r, {});
	CHECK(small.SetLayout(layout()));
	float f[6] = {};
	CHECK(small.SetHdr(GridHdrState::Present, "x", f, sizeof(f)).code == Code::MemoryLimit);
	CHECK(!small.Acquire());
	CHECK(small.Report().bytes == 0);
	limit.maxCells = 7;
	LightgridCapture tiny(limit);
	source(tiny, {}, {});
	CHECK(tiny.SetLayout(layout()).code == Code::MemoryLimit);
	for (unsigned n = 0; n < 6; ++n)
	{
		source(c, r, i);
		auto l = layout();
		if (n == 0)
			l.spacingWorld.x = 0;
		if (n == 1)
			l.spacingWorld.y = -1;
		if (n == 2)
			l.originWorld.z = std::numeric_limits<float>::infinity();
		if (n == 3)
			l.spacingWorld.z = std::numeric_limits<float>::quiet_NaN();
		if (n == 4)
			l.bounds[0] = 0;
		if (n == 5)
		{
			l.spacingWorld.z = std::numeric_limits<float>::max();
			l.bounds[2] = 3;
		}
		CHECK(!c.SetLayout(l));
		CHECK(!c.Acquire());
	}
	source(c, r, i);
	auto l = layout();
	l.bounds[0] = l.bounds[1] = l.bounds[2] = UINT32_MAX;
	CHECK(c.SetLayout(l).code == Code::MemoryLimit);
	c.Begin(BuildPolicy::ForceRebuild);
	CHECK(!c.SetSource(identity(), nullptr, 30, 0, nullptr, 0, 0));
	c.Begin(BuildPolicy::ForceRebuild);
	CHECK(!c.SetSource(identity(), r.data(), r.size(), UINT64_MAX, nullptr, 0, 0));
	source(c, r, i);
	CHECK(c.SetLayout(layout()));
	CHECK(!c.SetLayout(layout()));
	CHECK(c.SetHdr(GridHdrState::NotRequested, "", nullptr, 0));
	CHECK(!c.SetHdr(GridHdrState::Missing, "", nullptr, 0));
	CHECK(c.Freeze());
	CHECK(!c.Freeze());
}
} // namespace
int main()
{
	normal();
	malformed();
	decoding();
	directions();
	limitsAndLifecycle();
	std::printf("lightgrid: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
