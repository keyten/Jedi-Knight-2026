// No GL, engine allocators, filesystem or startup. Fixtures are the exact
// original/decoded pixel representations handed off by the existing loaders.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "lightrecon/lightmaps.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>

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
			std::printf("FAIL line %d: %s\n", __LINE__, #x);                                       \
		}                                                                                          \
	} while (0)
static_assert(std::is_const<decltype(LightmapImage().bytes)::element_type>::value,
			  "frozen bytes are immutable");
static_assert(std::is_const<decltype(LightmapCapture().Acquire())::element_type>::value,
			  "read-only publication");
const uint8_t rgba[] = {0, 1, 255, 3, 128, 4, 9, 255};
const float hdr[] = {0.0f, 1.0f / 256.0f, 1024.0f, 0.5f, 16.0f, 65536.0f};
struct Cancelled : Cancellation
{
	bool IsCancelled() const noexcept override
	{
		return true;
	}
};
std::vector<uint8_t> Bsp()
{
	std::vector<uint8_t> data(16 + 4 * 12, 0);
	for (size_t i = 16; i < data.size(); ++i)
		data[i] = static_cast<uint8_t>((i * 17) & 255);
	data[16] = data[17] = data[18] = 0; // retain raw black; never renderer zero-repair
	return data;
}
void Begin(LightmapCapture &capture, const std::vector<uint8_t> &bsp)
{
	capture.Begin(BuildPolicy::ForceRebuild);
	CHECK(capture.SetMap("maps/synthetic.bsp", bsp.data(), bsp.size()));
}
void Policies()
{
	for (int policy = 0; policy != 4; ++policy)
		for (bool hit : {false, true})
		{
			const bool expected = policy == 3 || (policy == 2 && !hit);
			LightmapCapture c;
			c.Begin(static_cast<BuildPolicy>(policy), hit);
			CHECK(c.Needed() == expected);
			CHECK(!c.Acquire());
			CHECK(c.Report().pixelBytes == 0);
			CHECK(c.Report().accountedBytes == 0);
			if (!expected)
			{
				CHECK(c.SetMap("ignored", nullptr, 0).code == Code::InvalidState);
				CHECK(!c.Acquire());
				CHECK(c.Report().images == 0);
			}
		}
	CHECK(!NeedLightmapObservations(static_cast<BuildPolicy>(99), false));
}
void Preservation()
{
	auto bsp = Bsp();
	const auto original = bsp;
	LightmapCapture c;
	Begin(c, bsp);
	CHECK(c.Embedded(bsp.data(), bsp.size(), 16, 48, 2, 2, true));
	// Deliberately add externals in reverse logical order. No atlas dimensions
	// enter capture, including when external HDR overrides embedded RGB8.
	CHECK(c.Missing(6, LightmapRole::Lighting, LightmapEncoding::ExternalRgba8,
					"maps/synthetic/lm_0006.tga"));
	CHECK(c.Rgba8(5, LightmapRole::DeluxeDirection, "maps/synthetic/dm_0005.tga", 2, 1, rgba,
				  sizeof(rgba)));
	float sourceHdr[6];
	std::memcpy(sourceHdr, hdr, sizeof(hdr));
	CHECK(c.Rgb32F(0, "maps/synthetic/lm_0000.hdr", 2, 1, sourceHdr, 6));
	uint8_t sourceRgba[8];
	std::memcpy(sourceRgba, rgba, sizeof(rgba));
	CHECK(c.Rgba8(4, LightmapRole::Lighting, "maps/synthetic/lm_0004.png", 2, 1, sourceRgba,
				  sizeof(sourceRgba)));
	CHECK(bsp == original);							 // never modifies the renderer's source bytes
	std::fill(bsp.begin(), bsp.end(), uint8_t(255)); // FS_FreeFile / scratch overwrite analogue
	std::fill(sourceHdr, sourceHdr + 6, -5.0f);
	std::fill(sourceRgba, sourceRgba + 8, uint8_t(66));
	CHECK(c.Freeze());
	auto set = c.Acquire();
	CHECK(!!set);
	CHECK(set->Map().path == "maps/synthetic.bsp");
	CHECK(set->Map().bytes == original.size());
	CHECK(set->Map().sha256 == Disk::Hash(original.data(), original.size()));
	CHECK(set->Images().size() == 8);
	const auto &images = set->Images();
	CHECK(images[0].logicalIndex == 0 && images[0].encoding == LightmapEncoding::BspRgb8);
	CHECK(images[1].logicalIndex == 0 && images[1].encoding == LightmapEncoding::ExternalRgb32F);
	CHECK(images[2].logicalIndex == 1 && images[2].role == LightmapRole::DeluxeDirection);
	CHECK(images[3].logicalIndex == 2 && images[3].role == LightmapRole::Lighting);
	CHECK(images[4].logicalIndex == 3 && images[4].role == LightmapRole::DeluxeDirection);
	for (const auto &image : images)
	{
		if (image.encoding == LightmapEncoding::BspRgb8)
		{
			CHECK(image.width == 2 && image.height == 2 && image.channels == 3);
			CHECK(image.source == "maps/synthetic.bsp#LUMP_LIGHTMAPS");
			CHECK(image.sourceOffset == 16 + image.logicalIndex * 12);
			CHECK(std::equal(image.bytes->begin(), image.bytes->end(),
							 original.begin() + static_cast<size_t>(image.sourceOffset)));
		}
		else if (image.encoding == LightmapEncoding::ExternalRgb32F)
		{
			CHECK(image.channels == 3 && image.width == 2 && image.height == 1);
			CHECK(!image.bytes && image.floats->size() == 6);
			CHECK(std::memcmp(image.floats->data(), hdr, sizeof(hdr)) == 0);
		}
		else if (!image.missing)
		{
			CHECK(image.channels == 4 && image.sourceOffset == 0);
			CHECK(std::equal(image.bytes->begin(), image.bytes->end(), rgba));
		}
		else
		{
			CHECK(image.logicalIndex == 6 && image.width == 0 && image.height == 0);
			CHECK(!image.bytes && !image.floats);
		}
	}
	CHECK(c.Report().pixels == 22 && c.Report().pixelBytes == 88);
	CHECK(c.Report().rgb8 == 4 && c.Report().rgba8 == 2 && c.Report().hdr == 1);
	CHECK(c.Report().missing == 1 && c.Report().deluxe == 3);
	ReadContext context;
	context.maxBytes = 1024;
	Chunk<uint8_t> bytes;
	Chunk<float> floats;
	CHECK(set->ReadBytes(0, 0, 12, context, bytes));
	CHECK(bytes.owner && bytes.data[0] == 0 && bytes.data[1] == 0 && bytes.data[2] == 0);
	CHECK(set->ReadFloats(1, 0, 6, context, floats));
	CHECK(floats.data[5] == 65536.0f);
	const auto pinned = bytes.data;
	CHECK(set->ReadBytes(0, 0, 12, context, bytes));
	CHECK(bytes.data == pinned);
	CHECK(set->ReadBytes(1, 0, 1, context, bytes).code == Code::Unavailable);
	CHECK(set->ReadBytes(7, 0, 1, context, bytes).code == Code::Unavailable);
	CHECK(set->ReadBytes(88, 0, 1, context, bytes).code == Code::InvalidInput);
	CHECK(set->ReadBytes(0, UINT64_MAX, 1, context, bytes).code == Code::InvalidInput);
	CHECK(set->ReadBytes(0, 1, SIZE_MAX, context, bytes).code == Code::InvalidInput);
	context.maxBytes = 3;
	CHECK(set->ReadFloats(1, 0, 1, context, floats).code == Code::MemoryLimit);
	CHECK(bytes.data == pinned); // failed reads never replace output leases
	Cancelled cancelled;
	context.cancellation = &cancelled;
	CHECK(set->ReadBytes(0, 0, 1, context, bytes).code == Code::Cancelled);
	CHECK(bytes.data == pinned);
	CHECK(c.Rgba8(10, LightmapRole::Lighting, "late", 2, 1, rgba, 8).code == Code::InvalidState);
	c.Reset();
	set.reset();
	CHECK(!c.Acquire());
	CHECK(bytes.data[3] == original[19]);
	CHECK(floats.data[5] == hdr[5]);
	CHECK(c.Report().accountedBytes == 0);
}
void AtlasIndependence()
{
	const auto bsp = Bsp();
	std::shared_ptr<const LightmapObservations> previous;
	// Simulate consumers packing 1/2/4 logical images per atlas row. Capture
	// receives the same original lump, independently of any packing choice.
	for (unsigned perRow : {1u, 2u, 4u})
	{
		LightmapCapture c;
		Begin(c, bsp);
		CHECK(c.Embedded(bsp.data(), bsp.size(), 16, 48, 2, 2, false));
		CHECK(c.Freeze());
		auto current = c.Acquire();
		for (size_t i = 0; i < current->Images().size(); ++i)
		{
			const size_t simulatedAtlasX = (i % perRow) * 2;
			CHECK(simulatedAtlasX < perRow * 2);
			CHECK(current->Images()[i].logicalIndex == i);
			if (previous)
			{
				CHECK(current->Images()[i].pixelHash == previous->Images()[i].pixelHash);
				CHECK(*current->Images()[i].bytes == *previous->Images()[i].bytes);
			}
		}
		previous = current;
	}
}
void ExternalOnly()
{
	const auto bsp = Bsp();
	LightmapCapture c; Begin(c, bsp);
	CHECK(c.Embedded(bsp.data(), bsp.size(), 0, 0, 128, 128, false));
	CHECK(c.Rgba8(42, LightmapRole::Lighting, "maps/external/lm_0042.tga", 2, 1, rgba, 8));
	CHECK(c.Missing(7, LightmapRole::Lighting, LightmapEncoding::ExternalRgb32F, "maps/external/lm_0007.hdr"));
	CHECK(c.Freeze());
	const auto set = c.Acquire();
	CHECK(set->Images().size() == 2 && set->Images()[0].logicalIndex == 7);
	CHECK(set->Images()[1].logicalIndex == 42 && set->Images()[1].sourceOffset == 0);
	CHECK(c.Report().rgb8 == 0 && c.Report().rgba8 == 1 && c.Report().pixelBytes == 8);
	CHECK(std::equal(set->Images()[1].bytes->begin(), set->Images()[1].bytes->end(), rgba));
}
void InvalidAndLifetime()
{
	const auto bsp = Bsp();
	for (int mode = 0; mode < 16; ++mode)
	{
		LightmapLimits limits;
		if (mode == 0)
			limits.maxTotalBytes = 50;
		if (mode == 1)
			limits.maxImageBytes = 7;
		if (mode == 2)
			limits.maxImages = 0;
		if (mode == 3)
			limits.maxDimension = 1;
		if (mode == 4)
			limits.maxPathBytes = 20;
		if (mode == 5)
			limits.maxDimension = UINT32_MAX;
		if (mode == 15)
		{
			limits.maxDimension = UINT32_MAX;
			limits.maxImageBytes = limits.maxTotalBytes = UINT64_MAX;
		}
		LightmapCapture c(limits);
		Begin(c, bsp);
		Error error;
		switch (mode)
		{
		case 5:
			error = c.Rgb32F(0, "huge", UINT32_MAX, UINT32_MAX, hdr, 6);
			break;
		case 6:
			error = c.Embedded(bsp.data(), bsp.size(), UINT64_MAX, 12, 2, 2, false);
			break;
		case 7:
			error = c.Embedded(bsp.data(), bsp.size(), 16, UINT64_MAX, 2, 2, false);
			break;
		case 8:
			error = c.Embedded(bsp.data(), bsp.size(), 16, 13, 2, 2, false);
			break;
		case 9:
			error = c.Embedded(bsp.data(), bsp.size(), 16, 12, 2, 2, true);
			break;
		case 10:
			error = c.Rgba8(0, LightmapRole::Lighting, "bad", 0, 2, rgba, 8);
			break;
		case 11:
			error = c.Rgba8(0, LightmapRole::Lighting, "bad", 2, 1, nullptr, 8);
			break;
		case 12:
			error = c.Rgba8(0, LightmapRole::Lighting, "bad", 2, 1, rgba, 7);
			break;
		case 13:
		{
			float bad[3] = {1, std::numeric_limits<float>::infinity(), 0};
			error = c.Rgb32F(0, "inf", 1, 1, bad, 3);
			break;
		}
		case 14:
		{
			float bad[3] = {1, std::numeric_limits<float>::quiet_NaN(), 0};
			error = c.Rgb32F(0, "nan", 1, 1, bad, 3);
			break;
		}
		case 15:
			error = c.Rgba8(0, LightmapRole::Lighting, "unaddressable", UINT32_MAX, UINT32_MAX,
							rgba, static_cast<size_t>(uint64_t(UINT32_MAX) * UINT32_MAX * 4));
			break;
		default:
			error = c.Rgba8(0, LightmapRole::Lighting,
							mode == 4 ? "maps/synthetic/too-long.tga" : "map.tga", 2, 1, rgba, 8);
			break;
		}
		CHECK(!error);
		CHECK(c.Report().state == CaptureState::Failed);
		CHECK(!c.Acquire() && c.Report().pixelBytes == 0);
		CHECK(!c.Freeze());
	}
	LightmapCapture c;
	Begin(c, bsp);
	CHECK(c.Rgba8(0, LightmapRole::Lighting, "map.tga", 2, 1, rgba, 8));
	CHECK(c.Rgba8(0, LightmapRole::Lighting, "map.tga", 2, 1, rgba, 8).code == Code::InvalidInput);
	CHECK(c.Report().images == 0); // no partial publication after a failure
	for (unsigned cycle = 0; cycle < 50; ++cycle)
	{
		Begin(c, bsp);
		CHECK(c.Freeze());
		auto old = c.Acquire();
		CHECK(old->Images().empty());
		c.Begin(BuildPolicy::Disabled);
		CHECK(!c.Acquire());
		CHECK(old->Map().path == "maps/synthetic.bsp");
	}
	c.Begin(BuildPolicy::ForceRebuild);
	CHECK(c.SetMap("", bsp.data(), bsp.size()).code == Code::InvalidInput);
	c.Begin(BuildPolicy::ForceRebuild);
	CHECK(c.SetMap("different", nullptr, bsp.size()).code == Code::InvalidInput);
	LightmapLimits tiny;
	tiny.maxSourceMapBytes = 4;
	LightmapCapture limited(tiny);
	limited.Begin(BuildPolicy::ForceRebuild);
	CHECK(limited.SetMap("huge", bsp.data(), bsp.size()).code == Code::MemoryLimit);
	Begin(c, bsp);
	CHECK(c.Freeze());
	auto previousMap = c.Acquire();
	c.Begin(BuildPolicy::CacheOrBuild);
	CHECK(c.SetMap("maps/next.bsp", bsp.data(), bsp.size()));
	CHECK(c.Freeze());
	CHECK(c.Acquire()->Map().path == "maps/next.bsp");
	CHECK(previousMap->Map().path == "maps/synthetic.bsp");
	c.Begin(BuildPolicy::ForceRebuild);
	CHECK(c.Freeze().code == Code::InvalidInput);
	CHECK(c.Report().state == CaptureState::Failed && !c.Acquire());
	// Original image-space data cannot accidentally become physical emitters or
	// imply fitted surface samples. InputReader exposes the same read-only lease.
	Begin(c, bsp);
	CHECK(c.Freeze());
	InputDataset input;
	input.lightmaps = c.Acquire();
	InputReader reader(input);
	CHECK(reader.Lightmaps() == input.lightmaps);
	CHECK(input.manifest.surfaces == 0);
	CHECK(AssetMetadata(input.manifest).emitters.empty());
}
} // namespace
int main()
{
	Policies();
	Preservation();
	AtlasIndependence();
	ExternalOnly();
	InvalidAndLifetime();
	std::printf("lightrecon lightmaps: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
