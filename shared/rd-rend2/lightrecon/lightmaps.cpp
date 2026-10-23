// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "lightmaps.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <new>
#include <tuple>

namespace Rend2LightRecon
{
namespace Core
{
bool NeedLightmapObservations(BuildPolicy policy, bool hit)
{
	return policy == BuildPolicy::ForceRebuild || (policy == BuildPolicy::CacheOrBuild && !hit);
}
const char *CaptureStateName(CaptureState state)
{
	switch (state)
	{
	case CaptureState::NotRequested:
		return "NotRequested";
	case CaptureState::CacheHit:
		return "CacheHit";
	case CaptureState::Collecting:
		return "Collecting";
	case CaptureState::Captured:
		return "Captured";
	case CaptureState::Failed:
		return "Failed";
	}
	return "Invalid";
}
const char *LightmapEncodingName(LightmapEncoding encoding)
{
	switch (encoding)
	{
	case LightmapEncoding::BspRgb8:
		return "BSP-RGB8";
	case LightmapEncoding::ExternalRgba8:
		return "external-RGBA8";
	case LightmapEncoding::ExternalRgb32F:
		return "external-RGB32F";
	}
	return "Invalid";
}
namespace
{
template <typename T>
Error Read(const std::vector<LightmapImage> &images, size_t ordinal, uint64_t first, size_t count,
		   const ReadContext &context,
		   const std::shared_ptr<const std::vector<T>> LightmapImage::*member, Chunk<T> &out)
{
	if (context.cancellation && context.cancellation->IsCancelled())
		return Failure(Code::Cancelled, "lightmap read cancelled");
	if (ordinal >= images.size())
		return Failure(Code::InvalidInput, "lightmap ordinal out of range");
	const auto &pixels = images[ordinal].*member;
	if (!pixels)
		return Failure(Code::Unavailable, "missing lightmap or different representation");
	if (first > pixels->size() || count > pixels->size() - first)
		return Failure(Code::InvalidInput, "lightmap span out of range");
	if (count > context.maxBytes / sizeof(T))
		return Failure(Code::MemoryLimit, "lightmap read budget exceeded");
	out = VectorChunk(pixels, static_cast<size_t>(first), count);
	return Error();
}
} // namespace
Error LightmapObservations::ReadBytes(size_t image, uint64_t first, size_t count,
									  const ReadContext &context, Chunk<uint8_t> &out) const
{
	return Read(images_, image, first, count, context, &LightmapImage::bytes, out);
}
Error LightmapObservations::ReadFloats(size_t image, uint64_t first, size_t count,
									   const ReadContext &context, Chunk<float> &out) const
{
	return Read(images_, image, first, count, context, &LightmapImage::floats, out);
}
void LightmapCapture::Reset() noexcept
{
	std::atomic_store(&published_, std::shared_ptr<const LightmapObservations>());
	std::vector<LightmapImage>().swap(images_);
	map_ = LightmapMapIdentity();
	report_ = LightmapReport();
}
void LightmapCapture::Begin(BuildPolicy policy, bool hit)
{
	Reset();
	report_.state = NeedLightmapObservations(policy, hit)
						? CaptureState::Collecting
						: (hit && policy == BuildPolicy::CacheOrBuild ? CaptureState::CacheHit
																	  : CaptureState::NotRequested);
}
Error LightmapCapture::Fail(Code code, const std::string &message)
{
	std::atomic_store(&published_, std::shared_ptr<const LightmapObservations>());
	std::vector<LightmapImage>().swap(images_);
	map_ = LightmapMapIdentity();
	report_ = LightmapReport();
	report_.state = CaptureState::Failed;
	report_.error = code;
	report_.message = message;
	return Failure(code, message, "lightmaps");
}
Error LightmapCapture::SetMap(const std::string &path, const void *bsp, size_t bytes)
{
	if (!Needed())
		return Failure(Code::InvalidState, "lightmap capture is not collecting");
	if (!map_.path.empty())
		return Fail(Code::InvalidState, "map identity already set");
	if (path.empty() || path.size() > limits_.maxPathBytes || !bsp || !bytes)
		return Fail(Code::InvalidInput, "missing BSP identity or original bytes");
	if (bytes > limits_.maxSourceMapBytes || path.size() > limits_.maxTotalBytes)
		return Fail(Code::MemoryLimit, "BSP identity exceeds capture limit");
	try
	{
		map_.path = path;
		map_.bytes = bytes;
		map_.sha256 = Disk::Hash(bsp, bytes);
		report_.accountedBytes = path.size();
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::MemoryLimit, "BSP identity allocation failed");
	}
}
Error LightmapCapture::Add(LightmapImage image, const void *data, size_t elements)
{
	if (!Needed())
		return Failure(Code::InvalidState, "lightmap capture is not collecting");
	if (map_.path.empty())
		return Fail(Code::InvalidState, "lightmap needs a source-map identity");
	if (image.source.empty() || image.source.size() > limits_.maxPathBytes ||
		static_cast<unsigned>(image.role) > static_cast<unsigned>(LightmapRole::DeluxeDirection) ||
		static_cast<unsigned>(image.encoding) >
			static_cast<unsigned>(LightmapEncoding::ExternalRgb32F))
		return Fail(Code::InvalidInput, "invalid lightmap source/role/encoding");
	if (images_.size() >= limits_.maxImages)
		return Fail(Code::MemoryLimit, "too many lightmap images");
	const bool hdr = image.encoding == LightmapEncoding::ExternalRgb32F;
	const uint64_t componentBytes = hdr ? sizeof(float) : 1;
	uint64_t pixels = 0, bytes = 0;
	if (!image.missing)
	{
		if (!data || !image.width || !image.height || image.width > limits_.maxDimension ||
			image.height > limits_.maxDimension)
			return Fail(Code::InvalidInput, "invalid lightmap dimensions or buffer");
		pixels = uint64_t(image.width) * image.height;
		// uint32 dimensions squared fit u64, but channel/element multiplication may not.
		if (pixels > std::numeric_limits<uint64_t>::max() / image.channels / componentBytes)
			return Fail(Code::MemoryLimit, "lightmap byte count overflow");
		bytes = pixels * image.channels * componentBytes;
		if (bytes > limits_.maxImageBytes || bytes > std::numeric_limits<size_t>::max())
			return Fail(Code::MemoryLimit, "lightmap image exceeds limit");
		if (elements > (hdr ? std::vector<float>().max_size() : std::vector<uint8_t>().max_size()))
			return Fail(Code::MemoryLimit, "lightmap exceeds addressable vector capacity");
		if (bytes / componentBytes != elements)
			return Fail(Code::InvalidInput, "lightmap buffer size mismatch");
	}
	// Allow vector growth/record copies and shared ownership bookkeeping, not only pixels.
	const uint64_t overhead = 2 * sizeof(LightmapImage) + 128 + image.source.size();
	if (report_.accountedBytes > limits_.maxTotalBytes ||
		overhead > limits_.maxTotalBytes - report_.accountedBytes ||
		bytes > limits_.maxTotalBytes - report_.accountedBytes - overhead)
		return Fail(Code::MemoryLimit, "lightmap capture budget exceeded");
	for (const auto &old : images_)
		if (std::tie(old.logicalIndex, old.role, old.encoding, old.source) ==
			std::tie(image.logicalIndex, image.role, image.encoding, image.source))
			return Fail(Code::InvalidInput, "duplicate lightmap identity");
	try
	{
		if (!image.missing)
		{
			if (hdr)
			{
				const float *values = static_cast<const float *>(data);
				for (size_t i = 0; i < elements; ++i)
					if (!std::isfinite(values[i]))
						return Fail(Code::InvalidInput, "non-finite HDR lightmap sample");
				image.floats =
					std::make_shared<const std::vector<float>>(values, values + elements);
			}
			else
			{
				const uint8_t *values = static_cast<const uint8_t *>(data);
				image.bytes =
					std::make_shared<const std::vector<uint8_t>>(values, values + elements);
			}
			image.pixelHash = Disk::Hash(data, static_cast<size_t>(bytes));
		}
		images_.push_back(std::move(image));
		const auto &added = images_.back();
		++report_.images;
		if (added.missing)
			++report_.missing;
		else if (hdr)
			++report_.hdr;
		else if (added.encoding == LightmapEncoding::BspRgb8)
			++report_.rgb8;
		else
			++report_.rgba8;
		if (added.role == LightmapRole::DeluxeDirection)
			++report_.deluxe;
		report_.pixels += pixels;
		report_.pixelBytes += bytes;
		report_.accountedBytes += bytes + overhead;
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::MemoryLimit, "lightmap pixel allocation failed");
	}
}
Error LightmapCapture::Embedded(const void *bsp, size_t bspBytes, uint64_t offset, uint64_t length,
								uint32_t width, uint32_t height, bool deluxe)
{
	try
	{
		if (!Needed())
			return Failure(Code::InvalidState, "lightmap capture is not collecting");
		if (!bsp || bspBytes != map_.bytes || offset > bspBytes || length > bspBytes - offset ||
			!width || !height || width > limits_.maxDimension || height > limits_.maxDimension)
			return Fail(Code::InvalidInput, "invalid embedded lightmap lump bounds/dimensions");
		const uint64_t pixels = uint64_t(width) * height;
		if (pixels > std::numeric_limits<uint64_t>::max() / 3)
			return Fail(Code::MemoryLimit, "embedded image size overflow");
		const uint64_t stride = pixels * 3;
		if (length % stride || length / stride > limits_.maxImages ||
			(deluxe && (length / stride) % 2))
			return Fail(Code::InvalidInput, "incomplete embedded lightmap/deluxe image");
		for (uint64_t i = 0; i < length / stride; ++i)
		{
			LightmapImage image;
			image.logicalIndex = static_cast<uint32_t>(i);
			image.role = deluxe && (i & 1) ? LightmapRole::DeluxeDirection : LightmapRole::Lighting;
			image.width = width;
			image.height = height;
			image.channels = 3;
			image.source = map_.path + "#LUMP_LIGHTMAPS";
			image.sourceOffset = offset + i * stride;
			Error error =
				Add(std::move(image), static_cast<const uint8_t *>(bsp) + offset + i * stride,
					static_cast<size_t>(stride));
			if (!error)
				return error;
		}
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::MemoryLimit, "embedded lightmap metadata allocation failed");
	}
}
Error LightmapCapture::Rgba8(uint32_t index, LightmapRole role, const std::string &source,
							 uint32_t width, uint32_t height, const uint8_t *data, size_t bytes)
{
	try
	{
		LightmapImage image;
		image.logicalIndex = index;
		image.role = role;
		image.encoding = LightmapEncoding::ExternalRgba8;
		image.source = source;
		image.width = width;
		image.height = height;
		image.channels = 4;
		return Add(std::move(image), data, bytes);
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::MemoryLimit, "external lightmap metadata allocation failed");
	}
}
Error LightmapCapture::Rgb32F(uint32_t index, const std::string &source, uint32_t width,
							  uint32_t height, const float *data, size_t values)
{
	try
	{
		LightmapImage image;
		image.logicalIndex = index;
		image.source = source;
		image.encoding = LightmapEncoding::ExternalRgb32F;
		image.width = width;
		image.height = height;
		image.channels = 3;
		return Add(std::move(image), data, values);
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::MemoryLimit, "HDR lightmap metadata allocation failed");
	}
}
Error LightmapCapture::Missing(uint32_t index, LightmapRole role, LightmapEncoding encoding,
							   const std::string &source)
{
	try
	{
		LightmapImage image;
		image.logicalIndex = index;
		image.role = role;
		image.encoding = encoding;
		image.source = source;
		image.missing = true;
		return Add(std::move(image), nullptr, 0);
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::MemoryLimit, "missing lightmap metadata allocation failed");
	}
}
Error LightmapCapture::Freeze()
{
	if (!Needed())
		return Failure(Code::InvalidState, "lightmap capture cannot be frozen");
	if (map_.path.empty())
		return Fail(Code::InvalidInput, "BSP capture never provided a source-map identity");
	try
	{
		std::sort(images_.begin(), images_.end(),
				  [](const LightmapImage &a, const LightmapImage &b) {
					  return std::tie(a.logicalIndex, a.role, a.encoding, a.source) <
							 std::tie(b.logicalIndex, b.role, b.encoding, b.source);
				  });
		std::shared_ptr<LightmapObservations> result(new LightmapObservations());
		result->map_ = std::move(map_);
		result->images_ = std::move(images_);
		std::atomic_store(&published_, std::shared_ptr<const LightmapObservations>(result));
		report_.state = CaptureState::Captured;
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::MemoryLimit, "lightmap publication allocation failed");
	}
}
} // namespace Core
} // namespace Rend2LightRecon
