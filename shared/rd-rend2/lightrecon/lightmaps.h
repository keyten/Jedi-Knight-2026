// Original baked images, before renderer colour conversion or atlas packing.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "input.h"
#include "disk.h"
#include "control.h"

namespace Rend2LightRecon
{
namespace Core
{

enum class LightmapRole
{
	Lighting,
	DeluxeDirection
};
enum class LightmapEncoding
{
	BspRgb8,
	ExternalRgba8,
	ExternalRgb32F
};
enum class CaptureState
{
	NotRequested,
	CacheHit,
	Collecting,
	Captured,
	Failed
};
const char *CaptureStateName(CaptureState);
const char *LightmapEncodingName(LightmapEncoding);
// A validated cache hit suppresses observation allocation. ForceRebuild ignores
// hits. An unknown/incomplete cache key is NOT a hit. No consumer flag is used.
bool NeedLightmapObservations(BuildPolicy, bool validatedCacheHit);

struct LightmapLimits
{
	uint64_t maxTotalBytes = 128ull * 1024 * 1024; // pixels + bounded record/string accounting
	uint64_t maxImageBytes = 32ull * 1024 * 1024;
	uint64_t maxSourceMapBytes = 256ull * 1024 * 1024;
	uint32_t maxImages = 8192, maxDimension = 16384, maxPathBytes = 4096;
};
struct LightmapMapIdentity
{
	std::string path;	   // resolved main-world VFS identity, e.g. maps/hoth3.bsp
	Disk::Digest sha256{}; // original complete BSP file, before any in-place byte swapping
	uint64_t bytes = 0;
};
struct LightmapImage
{
	uint32_t logicalIndex = 0; // ORIGINAL BSP lm index, never atlas/page/packed UV index
	LightmapRole role = LightmapRole::Lighting;
	LightmapEncoding encoding = LightmapEncoding::BspRgb8;
	uint32_t width = 0, height = 0, channels = 0;
	std::string source;		   // BSP path + lump identity, or actual resolved external image path
	uint64_t sourceOffset = 0; // byte offset in BSP for embedded images only
	bool missing = false;	   // explicit failed external lookup; no invented dimensions/pixels
	uint32_t pairedLightingIndex = UINT32_MAX; // deluxe only; explicit ORIGINAL colour page, not adjacency inference
	Disk::Digest pixelHash{};  // SHA256 of preserved representation (float bytes are host-native)
	std::shared_ptr<const std::vector<uint8_t>> bytes;
	std::shared_ptr<const std::vector<float>> floats;
};
class LightmapCapture;
class LightmapObservations
{
  public:
	~LightmapObservations() = default;
	const LightmapMapIdentity &Map() const
	{
		return map_;
	}
	// Sorted by logicalIndex, role, encoding, source. Embedded and external images
	// at the same index are ALTERNATIVES, never additive lighting observations.
	const std::vector<LightmapImage> &Images() const
	{
		return images_;
	}
	// Read bounded spans with a strong immutable pixel lease. No disk/GPU work.
	Error ReadBytes(size_t image, uint64_t first, size_t count, const ReadContext &,
					Chunk<uint8_t> &) const;
	Error ReadFloats(size_t image, uint64_t first, size_t count, const ReadContext &,
					 Chunk<float> &) const;

  private:
	friend class LightmapCapture;
	LightmapObservations() = default; // only a successfully frozen builder can create a set
	LightmapMapIdentity map_;
	std::vector<LightmapImage> images_;
};
struct LightmapReport
{
	CaptureState state = CaptureState::NotRequested;
	Code error = Code::Ok;
	std::string message;
	uint64_t images = 0, missing = 0, rgb8 = 0, rgba8 = 0, hdr = 0, deluxe = 0;
	uint64_t pixels = 0, pixelBytes = 0, accountedBytes = 0;
};
// Single writer, synchronous. All Add inputs are borrowed for that call ONLY;
// pixels are copied unchanged into independently owned per-image buffers. A
// failure discards the entire partial set. Freeze transfers ownership, no pixel
// copy. Reset drops the owner's lease; readers may keep old immutable chunks.
class LightmapCapture
{
  public:
	explicit LightmapCapture(LightmapLimits limits = LightmapLimits()) : limits_(limits) {}
	LightmapCapture(const LightmapCapture &) = delete;
	LightmapCapture &operator=(const LightmapCapture &) = delete;
	void Begin(BuildPolicy, bool validatedCacheHit = false);
	bool Needed() const
	{
		return report_.state == CaptureState::Collecting;
	}
	Error SetMap(const std::string &path, const void *originalBsp, size_t bytes);
	Error Embedded(const void *originalBsp, size_t bspBytes, uint64_t offset, uint64_t length,
				   uint32_t width, uint32_t height, bool interleavedDeluxe);
	Error Rgba8(uint32_t logicalIndex, LightmapRole, const std::string &source, uint32_t width,
				uint32_t height, const uint8_t *, size_t bytes, uint32_t pairedLightingIndex = UINT32_MAX);
	Error Rgb32F(uint32_t logicalIndex, const std::string &source, uint32_t width, uint32_t height,
				 const float *, size_t values);
	Error Missing(uint32_t logicalIndex, LightmapRole, LightmapEncoding, const std::string &source,
				uint32_t pairedLightingIndex = UINT32_MAX);
	Error Freeze();
	Error Fail(Code, const std::string &message);
	void Reset() noexcept;
	std::shared_ptr<const LightmapObservations> Acquire() const
	{
		return std::atomic_load(&published_);
	}
	const LightmapReport &Report() const // owner thread only; Acquire/leased data support readers
	{
		return report_;
	}
	const LightmapLimits &Limits() const
	{
		return limits_;
	}
	// Owner-thread staging identity, copied by other raw-observation adapters.
	const LightmapMapIdentity &SourceIdentity() const { return map_; }

  private:
	Error Add(LightmapImage, const void *, size_t elements);
	LightmapLimits limits_;
	LightmapReport report_;
	LightmapMapIdentity map_;
	std::vector<LightmapImage> images_;
	std::shared_ptr<const LightmapObservations> published_;
};
// Coordinates: row-major source-decoder order, x increasing within each row.
// Embedded rows retain BSP order; external rows retain the unflipped loader's
// order. No atlas UVs, flips, gamma, overbright, zero repair or colour scaling.
// RGB8 is historical uncalibrated encoded data, NOT linear radiance/lux. HDR is
// finite original decoded float RGB (Radiance RGBE); its scale is uncalibrated.
// Deluxe RGB stores encoded directions, not colour or physical emitter data.
} // namespace Core
} // namespace Rend2LightRecon
