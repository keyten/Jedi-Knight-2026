// Original JA lightgrid evidence. No renderer, geometry inference or interpolation.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "lightmaps.h"
#include "decode.h"

namespace Rend2LightRecon
{
namespace Core
{
static constexpr size_t kGridStyles = 4, kGridRecordBytes = 30;
enum class GridPresence
{
	Absent,
	Present,
	MissingIndex,
	InvalidIndex
};
enum class GridStyle
{
	Unavailable,
	None,
	Static,
	Styled,
	NonContiguous
};
enum class GridGeometry
{
	Unknown
}; // never derived from style/RGB/direction
enum class GridDirectionQuality
{
	Unavailable,
	QuantizedUncalibrated
};
enum class GridHdrState
{
	NotRequested,
	Missing,
	Present
};
enum GridIssue : uint32_t
{
	GridRecordTail = 1u << 0,
	GridIndexSize = 1u << 1,
	GridIndexOutOfRange = 1u << 2,
	GridHdrSize = 1u << 3,
	GridHdrNonFinite = 1u << 4,
	GridHdrNegative = 1u << 5,
	GridStyleGap = 1u << 6
};
struct GridLayout
{
	Vec3 originWorld, spacingWorld;
	uint32_t bounds[3] = {}; // x fastest; origin is first SAMPLE centre, no half-cell offset
};
struct GridDirection
{
	Vec3 toSource; // exact 256-step mathematical convention; receiver -> emitter, NOT propagation
	Vec3 entityToSource; // normalized legacy 1024-entry /1023 sin-table approximation
	float stepRadians = 0, errorBoundRadians = 0; // conservative encoding bound, not covariance
	GridDirectionQuality quality = GridDirectionQuality::Unavailable;
	bool confidenceKnown = false; // encoding alone does not establish measurement reliability
};
GridDirection DecodeGridDirection(uint8_t polar, uint8_t azimuth);
struct GridCell
{
	uint64_t ordinal = 0; // stable only within Map().sha256 + this layout
	uint32_t coordinates[3] = {};
	Vec3 centreWorld;
	GridPresence presence = GridPresence::Absent;
	bool indexPresent = false;
	uint16_t sourceRecord = 0; // meaningful only if indexPresent; may be out of range
	uint8_t ambient[kGridStyles][3] = {}, directed[kGridStyles][3] = {};
	uint8_t styles[kGridStyles] = {255, 255, 255, 255}, latLong[2] = {};
	GridStyle styleStatus = GridStyle::Unavailable;
	GridGeometry geometry = GridGeometry::Unknown;
	bool suspectedInvalid = false; // malformed data only; NOT solid/free-space classification
	uint32_t issues = 0;
	GridDirection direction;
	bool hdrPresent = false; // complete six-float tuple, independent of mgrid index/style
	float hdr[6] = {}; // original bits retained, including invalid values (decoder rejects them)
};
struct GridLimits
{
	uint64_t maxBytes = 64ull * 1024 * 1024, maxCells = 8ull * 1024 * 1024;
	uint64_t maxRecords = 1024 * 1024;
	size_t maxPathBytes = 4096;
};
struct GridReport
{
	CaptureState state = CaptureState::NotRequested;
	Code error = Code::Ok;
	std::string message;
	uint64_t cells = 0, records = 0, indices = 0, hdrCells = 0, bytes = 0;
	uint64_t missingCells = 0, invalidIndices = 0, noStyle = 0, styleGaps = 0;
	uint64_t nonFiniteHdr = 0, negativeHdr = 0;
	uint32_t issues = 0;
};
class LightgridCapture;
class LightgridObservations
{
  public:
	const LightmapMapIdentity &Map() const
	{
		return map_;
	}
	const GridLayout &Layout() const
	{
		return layout_;
	}
	const GridReport &Report() const
	{
		return report_;
	}
	GridHdrState HdrState() const
	{
		return hdrState_;
	}
	const std::string &HdrSource() const
	{
		return hdrSource_;
	}
	uint64_t RecordOffset() const
	{
		return recordOffset_;
	}
	uint64_t IndexOffset() const
	{
		return indexOffset_;
	}
	// Bounded O(1), no disk/GPU work; successful reads allocate nothing.
	// Output replaced even on failure (diagnostic strings may allocate).
	Error Cell(uint64_t ordinal, GridCell &) const;
	// Original little-endian BSP lump bytes / host IEEE binary32 HDR bits.
	// Const spans live for this snapshot's lease. No logical-cell expansion.
	const std::vector<uint8_t> &Records() const
	{
		return records_;
	}
	const std::vector<uint8_t> &Indices() const
	{
		return indices_;
	}
	const std::vector<float> &Hdr() const
	{
		return hdr_;
	}

  private:
	friend class LightgridCapture;
	LightgridObservations() = default;
	LightmapMapIdentity map_;
	GridLayout layout_;
	GridReport report_;
	GridHdrState hdrState_ = GridHdrState::NotRequested;
	std::string hdrSource_;
	uint64_t recordOffset_ = 0, indexOffset_ = 0;
	std::vector<uint8_t> records_, indices_;
	std::vector<float> hdr_;
};
// Mutable, single-writer staging. Caller buffers borrowed ONLY for each call.
// Malformed tuples/references are preserved with diagnostics; unsafe layout,
// memory limits fail the entire unpublished snapshot. Invalid call order is
// rejected without replacing an already valid staged/published result.
class LightgridCapture
{
  public:
	explicit LightgridCapture(GridLimits limits = GridLimits()) : limits_(limits) {}
	void Begin(BuildPolicy, bool validatedCacheHit = false);
	bool Needed() const
	{
		return report_.state == CaptureState::Collecting;
	}
	Error SetSource(const LightmapMapIdentity &, const void *records, size_t recordBytes,
					uint64_t recordOffset, const void *indices, size_t indexBytes,
					uint64_t indexOffset);
	Error SetLayout(const GridLayout &);
	Error SetHdr(GridHdrState, const std::string &path, const void *floatBytes, size_t bytes);
	Error Freeze();
	Error Fail(Code, const std::string &);
	void Reset() noexcept;
	std::shared_ptr<const LightgridObservations> Acquire() const
	{
		return std::atomic_load(&published_);
	}
	const GridReport &Report() const
	{
		return report_;
	} // owner thread only
	const GridLimits &Limits() const
	{
		return limits_;
	}

  private:
	GridLimits limits_;
	GridReport report_;
	bool sourceSet_ = false, layoutSet_ = false, hdrSet_ = false;
	std::unique_ptr<LightgridObservations> staging_;
	std::shared_ptr<const LightgridObservations> published_;
};
// Style slots remain separate. No style animation weights, ambient/direct merge
// or HDR/LDR addition. Missing tuples become decoder Missing observations.
Error DecodeGridLdr(const Decode::Decoder &, const GridCell &, size_t styleSlot, Decode::Component,
					Decode::Observation &);
Error DecodeGridHdr(const Decode::Decoder &, const GridCell &, Decode::Component,
					Decode::Observation &);
} // namespace Core
} // namespace Rend2LightRecon
