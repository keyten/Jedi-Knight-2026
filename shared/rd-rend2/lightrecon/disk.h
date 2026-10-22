// Portable CPU asset format. No filesystem or renderer dependencies.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "../tr_lightrecon_types.h"
#include <array>
#include <cstddef>

namespace Rend2LightRecon
{
namespace Disk
{
constexpr uint16_t kFormatMajor = 1, kFormatMinor = 0;
constexpr uint32_t kConventionVersion = 1;
constexpr uint32_t kHeaderBytes = 192, kDirectoryEntryBytes = 80;
using Digest = std::array<uint8_t, 32>;
Digest Hash(const void *data, size_t size); // SHA-256; Hash(nullptr, 0) is valid
std::string Hex(const Digest &digest);

enum class Code
{
	Ok,
	Missing,
	NotCacheable,
	Mismatch,
	Unsupported,
	Corrupt,
	LimitExceeded,
	InvalidAsset,
	InvalidArgument,
	IoError
};
struct Error
{
	Code code = Code::Ok;
	std::string message;
	explicit operator bool() const
	{
		return code == Code::Ok;
	}
};
struct Limits
{
	uint64_t maxFileBytes = 256ull * 1024 * 1024;
	uint64_t maxMetadataBytes = 32ull * 1024 * 1024;
	uint64_t maxDecodedBytes = 128ull * 1024 * 1024;
	uint64_t maxChunkBytes = 8ull * 1024 * 1024;
	uint64_t maxRecords = 100000;
	uint64_t maxAllocations = 1000000;
	uint32_t maxSections = 4096;
	uint32_t maxStringBytes = 1024 * 1024;
};

// Each category must explicitly enumerate ALL resolved inputs, or explicitly
// attest complete empty coverage. Missing/unhashed coverage is never cacheable.
enum class InputKind : uint32_t
{
	Bsp,
	InternalLightmaps,
	ExternalLightmaps,
	Lightgrid,
	DeluxeMaps,
	Materials,
	AuthoredLighting,
	Settings,
	UnitCalibration,
	Count
};
struct InputContent
{
	InputKind kind = InputKind::Bsp;
	std::string name; // stable semantic/VFS identity, not timestamp or OS path
	uint64_t bytes = 0;
	Digest sha256{}; // hash of resolved content, including explicitly absent-file policy
};
struct Fingerprint
{
	std::string algorithm; // namespaced implementation identity; no solver implied
	uint32_t algorithmVersion = 0;
	Units units; // exact scales are also part of the key and checked against output
	std::array<bool, static_cast<size_t>(InputKind::Count)> complete{};
	std::vector<InputContent> inputs;
};
// Stable across input enumeration order. Exactly one Bsp input is required;
// settings and unit/calibration policy each require at least one hashed input.
// Callers attest completeness; the format cannot discover missing dependencies.
Error MakeKey(const Fingerprint &, Digest &out, const Limits & = Limits());

enum class Section : uint32_t
{
	Metadata = 1,
	Fingerprint = 2,
	Provenance = 3,
	Calibration = 4,
	Hypotheses = 5,
	Emitters = 6,
	Sun = 7,
	Environment = 8,
	Transport = 9,
	Regions = 10,
	Datasets = 11,
	Visibility = 12,
	Contributions = 13,
	Diagnostics = 14,
	Chunk = 100
};
struct SectionInfo
{
	uint32_t type = 0, flags = 0; // bit 0 required; other bits unsupported
	Id dataset = 0;
	uint64_t firstElement = 0, offset = 0, bytes = 0, count = 0;
	Digest checksum{};
};
struct Chunk
{
	Id dataset = 0;
	uint64_t firstElement = 0, count = 0;
	std::vector<uint8_t> bytes; // opaque CPU encoding identified by Dataset
};
class Reader
{
  public:
	virtual ~Reader() = default;
	virtual uint64_t Size() const = 0;
	// Exactly size bytes or explicit error; no hidden prefetch/decoding.
	virtual Error Read(uint64_t offset, void *out, size_t size) const = 0;
};
class MemoryReader : public Reader
{
  public:
	MemoryReader(const void *data, size_t size)
		: data_(static_cast<const uint8_t *>(data)), size_(size)
	{
	}
	uint64_t Size() const override
	{
		return size_;
	}
	Error Read(uint64_t, void *, size_t) const override;

  private:
	const uint8_t *data_; // borrowed: caller retains immutable bytes
	size_t size_;
};
// Streaming content fingerprint helper; fixed 64 KiB scratch, no full-file load.
Error HashContent(const Reader &, Digest &out, uint64_t maxInputBytes);
struct Index
{
	Digest key{}, mapHash{};
	std::vector<SectionInfo> sections;
	// Metadata is validated on Open. Chunk/unknown optional payload checksums
	// are verified only when requested, never advertised as already verified.
};
// Output parameters remain unchanged on failure. No publication/renderer effects.
// Encode canonicalizes ID-bearing top-level record order; reference/diagnostic
// lists retain producer order. Unknown optional sections are skipped on Open.
Error Encode(const ReconstructedLightingAsset &, const Fingerprint &, const std::vector<Chunk> &,
			 std::vector<uint8_t> &out, const Limits & = Limits());
Error Open(const Reader &, const Fingerprint &expected, ReconstructedLightingAsset &, Index &,
		   const Limits & = Limits());
// Uses the validated directory, but also checks bounds/hash against this reader.
// Requires the SAME immutable reader/file as Open; no external locator is followed.
Error ReadChunk(const Reader &, const Index &, Id dataset, uint64_t firstElement,
				std::vector<uint8_t> &out, const Limits & = Limits());
} // namespace Disk
} // namespace Rend2LightRecon
