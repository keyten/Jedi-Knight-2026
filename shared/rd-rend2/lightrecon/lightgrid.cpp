// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "lightgrid.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <new>

namespace Rend2LightRecon
{
namespace Core
{
namespace
{
constexpr double pi = 3.14159265358979323846;
float component(const Vec3 &v, size_t axis)
{
	return axis == 0 ? v.x : axis == 1 ? v.y : v.z;
}
void component(Vec3 &v, size_t axis, float x)
{
	if (!axis)
		v.x = x;
	else if (axis == 1)
		v.y = x;
	else
		v.z = x;
}
bool validComponent(Decode::Component c)
{
	return c == Decode::Component::GridAmbient || c == Decode::Component::GridDirected;
}
} // namespace
GridDirection DecodeGridDirection(uint8_t polar, uint8_t azimuth)
{
	GridDirection d;
	const double p = polar * (2 * pi / 256), a = azimuth * (2 * pi / 256);
	d.toSource.x = static_cast<float>(std::cos(a) * std::sin(p));
	d.toSource.y = static_cast<float>(std::sin(a) * std::sin(p));
	d.toSource.z = static_cast<float>(std::cos(p));
	// Same index swap, quarter-wave phase and /1023 table as tr_light.cpp/tr_init.cpp.
	const unsigned lat = unsigned(azimuth) * 4, lng = unsigned(polar) * 4;
	const auto sine = [](unsigned i) { return std::sin(double(i & 1023) * (2 * pi / 1023)); };
	double x = sine(lat + 256) * sine(lng), y = sine(lat) * sine(lng), z = sine(lng + 256);
	const double n = std::sqrt(x * x + y * y + z * z);
	d.entityToSource.x = static_cast<float>(x / n);
	d.entityToSource.y = static_cast<float>(y / n);
	d.entityToSource.z = static_cast<float>(z / n);
	d.stepRadians = static_cast<float>(2 * pi / 256);
	// Unknown bake rounding: allow one whole step in EACH angle, plus conservative table error.
	d.errorBoundRadians = static_cast<float>(4 * pi / 256 + .02);
	d.quality = GridDirectionQuality::QuantizedUncalibrated;
	return d;
}
Error LightgridObservations::Cell(uint64_t ordinal, GridCell &out) const
{
	out = GridCell();
	if (ordinal >= report_.cells)
		return Failure(Code::InvalidInput, "Grid ordinal outside layout");
	out.ordinal = ordinal;
	uint64_t rest = ordinal;
	for (size_t a = 0; a < 3; ++a)
	{
		out.coordinates[a] = static_cast<uint32_t>(rest % layout_.bounds[a]);
		rest /= layout_.bounds[a];
		component(
			out.centreWorld, a,
			static_cast<float>(double(component(layout_.originWorld, a)) +
							   double(out.coordinates[a]) * component(layout_.spacingWorld, a)));
	}
	out.issues = report_.issues & (GridRecordTail | GridIndexSize | GridHdrSize);
	if (ordinal < indices_.size() / 2)
	{
		out.indexPresent = true;
		out.sourceRecord = static_cast<uint16_t>(unsigned(indices_[size_t(ordinal) * 2]) |
												 unsigned(indices_[size_t(ordinal) * 2 + 1]) << 8);
		if (out.sourceRecord < records_.size() / kGridRecordBytes)
		{
			out.presence = GridPresence::Present;
			const uint8_t *p = records_.data() + size_t(out.sourceRecord) * kGridRecordBytes;
			std::memcpy(out.ambient, p, 12);
			std::memcpy(out.directed, p + 12, 12);
			std::memcpy(out.styles, p + 24, 4);
			std::memcpy(out.latLong, p + 28, 2);
			bool ended = false, gap = false, styled = false, any = false;
			for (size_t s = 0; s < kGridStyles; ++s)
			{
				if (out.styles[s] == 255)
					ended = true;
				else
				{
					any = true;
					styled = styled || out.styles[s] != 0;
					gap = gap || ended;
				}
			}
			out.styleStatus =
				gap ? GridStyle::NonContiguous
					: !any ? GridStyle::None : styled ? GridStyle::Styled : GridStyle::Static;
			if (gap)
				out.issues |= GridStyleGap;
			out.direction = DecodeGridDirection(out.latLong[0], out.latLong[1]);
		}
		else
		{
			out.presence = GridPresence::InvalidIndex;
			out.issues |= GridIndexOutOfRange;
		}
	}
	else
		out.presence = records_.empty() && indices_.empty() ? GridPresence::Absent
															: GridPresence::MissingIndex;
	if (ordinal < hdr_.size() / 6)
	{
		out.hdrPresent = true;
		std::memcpy(out.hdr, hdr_.data() + size_t(ordinal) * 6, sizeof(out.hdr));
		for (float f : out.hdr)
		{
			if (!std::isfinite(f))
				out.issues |= GridHdrNonFinite;
			else if (f < 0)
				out.issues |= GridHdrNegative;
		}
	}
	out.suspectedInvalid = out.issues != 0;
	return Error();
}
void LightgridCapture::Reset() noexcept
{
	std::atomic_store(&published_, std::shared_ptr<const LightgridObservations>());
	staging_.reset();
	sourceSet_ = layoutSet_ = hdrSet_ = false;
	report_ = GridReport();
}
void LightgridCapture::Begin(BuildPolicy policy, bool hit)
{
	Reset();
	if (NeedLightmapObservations(policy, hit))
		report_.state = CaptureState::Collecting;
	else if (hit && policy == BuildPolicy::CacheOrBuild)
		report_.state = CaptureState::CacheHit;
}
Error LightgridCapture::Fail(Code code, const std::string &message)
{
	staging_.reset();
	std::atomic_store(&published_, std::shared_ptr<const LightgridObservations>());
	report_.state = CaptureState::Failed;
	report_.error = code;
	report_.message = message;
	report_.bytes = 0;
	return Failure(code, message, "lightgrid");
}
Error LightgridCapture::SetSource(const LightmapMapIdentity &map, const void *records, size_t rb,
								  uint64_t ro, const void *indices, size_t ib, uint64_t io)
{
	if (!Needed() || sourceSet_)
		return Failure(Code::InvalidState, "Grid source is not writable");
	if (map.path.empty() || map.path.size() > limits_.maxPathBytes || !map.bytes ||
		(rb && !records) || (ib && !indices) || ro > map.bytes || rb > map.bytes - ro ||
		io > map.bytes || ib > map.bytes - io)
		return Fail(Code::InvalidInput, "Invalid grid map/lump span");
	if (rb > std::vector<uint8_t>().max_size() || ib > std::vector<uint8_t>().max_size() ||
		rb / kGridRecordBytes > limits_.maxRecords || uint64_t(rb) > limits_.maxBytes ||
		uint64_t(ib) > limits_.maxBytes - uint64_t(rb))
		return Fail(Code::MemoryLimit, "Grid source exceeds memory/record limits");
	try
	{
		staging_.reset(new LightgridObservations());
		staging_->map_ = map;
		staging_->recordOffset_ = ro;
		staging_->indexOffset_ = io;
		if (rb)
			staging_->records_.assign(static_cast<const uint8_t *>(records),
									  static_cast<const uint8_t *>(records) + rb);
		if (ib)
			staging_->indices_.assign(static_cast<const uint8_t *>(indices),
									  static_cast<const uint8_t *>(indices) + ib);
		report_.records = rb / kGridRecordBytes;
		report_.indices = ib / 2;
		report_.bytes = uint64_t(rb) + uint64_t(ib);
		if (rb % kGridRecordBytes)
			report_.issues |= GridRecordTail;
		if (ib % 2)
			report_.issues |= GridIndexSize;
		sourceSet_ = true;
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::MemoryLimit, "Grid source allocation failed");
	}
}
Error LightgridCapture::SetLayout(const GridLayout &layout)
{
	if (!Needed() || !sourceSet_ || layoutSet_)
		return Failure(Code::InvalidState, "Grid layout is not writable");
	uint64_t cells = 1;
	bool empty = false;
	for (size_t a = 0; a < 3; ++a)
	{
		const double origin = component(layout.originWorld, a),
					 spacing = component(layout.spacingWorld, a);
		if (!std::isfinite(origin) || !std::isfinite(spacing) || spacing <= 0)
			return Fail(Code::InvalidInput, "Invalid grid origin/spacing");
		if (!layout.bounds[a])
			empty = true;
		else if (cells > limits_.maxCells / layout.bounds[a])
			return Fail(Code::MemoryLimit, "Grid dimensions exceed cell limit");
		else
			cells *= layout.bounds[a];
		const double end = origin + double(layout.bounds[a] ? layout.bounds[a] - 1 : 0) * spacing;
		if (!std::isfinite(end) || std::abs(end) > std::numeric_limits<float>::max())
			return Fail(Code::InvalidInput, "Grid position overflow");
	}
	if (empty && (layout.bounds[0] || layout.bounds[1] || layout.bounds[2]))
		return Fail(Code::InvalidInput, "Partially zero grid dimensions");
	report_.cells = empty ? 0 : cells;
	staging_->layout_ = layout;
	layoutSet_ = true;
	if (report_.indices != report_.cells)
		report_.issues |= GridIndexSize;
	return Error();
}
Error LightgridCapture::SetHdr(GridHdrState state, const std::string &path, const void *data,
							   size_t bytes)
{
	static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
				  "Original HDR observations require IEEE binary32");
	if (!Needed() || !sourceSet_ || !layoutSet_ || hdrSet_)
		return Failure(Code::InvalidState, "Grid HDR is not writable");
	if ((state != GridHdrState::NotRequested && state != GridHdrState::Missing &&
		 state != GridHdrState::Present) ||
		path.size() > limits_.maxPathBytes || (bytes && !data) ||
		(state != GridHdrState::Present && bytes) || bytes % sizeof(float))
		return Fail(Code::InvalidInput, "Invalid HDR grid span/state");
	if (bytes / sizeof(float) > std::vector<float>().max_size() ||
		uint64_t(bytes) > limits_.maxBytes - report_.bytes)
		return Fail(Code::MemoryLimit, "HDR grid exceeds memory limit");
	try
	{
		staging_->hdrState_ = state;
		staging_->hdrSource_ = path;
		staging_->hdr_.resize(bytes / sizeof(float));
		if (bytes)
			std::memcpy(staging_->hdr_.data(), data, bytes);
		report_.bytes += bytes;
		report_.hdrCells = bytes / (6 * sizeof(float));
		if (state == GridHdrState::Present &&
			(report_.hdrCells != report_.cells || bytes % (6 * sizeof(float))))
			report_.issues |= GridHdrSize;
		hdrSet_ = true;
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::MemoryLimit, "HDR grid allocation failed");
	}
}
Error LightgridCapture::Freeze()
{
	if (!Needed())
		return Failure(Code::InvalidState, "Grid capture is not writable");
	if (!sourceSet_ || !layoutSet_)
		return Fail(Code::InvalidState, "Grid source/layout incomplete");
	staging_->report_ = report_;
	GridCell c;
	for (uint64_t i = 0; i < report_.cells; ++i)
	{
		staging_->Cell(i, c);
		if (c.presence != GridPresence::Present)
			++report_.missingCells;
		if (c.presence == GridPresence::InvalidIndex)
			++report_.invalidIndices;
		if (c.styleStatus == GridStyle::None)
			++report_.noStyle;
		if (c.issues & GridStyleGap)
			++report_.styleGaps;
		if (c.issues & GridHdrNonFinite)
			++report_.nonFiniteHdr;
		if (c.issues & GridHdrNegative)
			++report_.negativeHdr;
		report_.issues |= c.issues;
	}
	report_.state = CaptureState::Captured;
	staging_->report_ = report_;
	try
	{
		std::shared_ptr<const LightgridObservations> ready(std::move(staging_));
		std::atomic_store(&published_, ready);
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::MemoryLimit, "Grid publication allocation failed");
	}
}
Error DecodeGridLdr(const Decode::Decoder &decoder, const GridCell &cell, size_t slot,
					Decode::Component kind, Decode::Observation &out)
{
	out = Decode::Observation();
	if (slot >= kGridStyles || !validComponent(kind))
		return Failure(Code::InvalidInput, "Invalid grid colour component/style slot");
	Decode::Raw raw;
	raw.encoding = Decode::Encoding::Rgb8;
	raw.component = kind;
	raw.missing = cell.presence != GridPresence::Present;
	std::memcpy(raw.bytes,
				kind == Decode::Component::GridAmbient ? cell.ambient[slot] : cell.directed[slot],
				3);
	return decoder.Run(raw, out);
}
Error DecodeGridHdr(const Decode::Decoder &decoder, const GridCell &cell, Decode::Component kind,
					Decode::Observation &out)
{
	out = Decode::Observation();
	if (!validComponent(kind))
		return Failure(Code::InvalidInput, "Invalid grid HDR component");
	Decode::Raw raw;
	raw.encoding = Decode::Encoding::LinearRgb32F;
	raw.component = kind;
	raw.hdrOrigin = Decode::HdrOrigin::LightgridRawFloat32;
	raw.missing = !cell.hdrPresent;
	const size_t o = kind == Decode::Component::GridAmbient ? 0 : 3;
	raw.hdr.x = cell.hdr[o];
	raw.hdr.y = cell.hdr[o + 1];
	raw.hdr.z = cell.hdr[o + 2];
	return decoder.Run(raw, out);
}
} // namespace Core
} // namespace Rend2LightRecon
