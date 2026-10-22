// Native disk adapter, deliberately separate from the portable binary codec.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "disk.h"
#include <memory>
#include <utility>

namespace Rend2LightRecon
{
namespace Disk
{
// Engine callers supply fs_homepath through the supported cvar interface.
// Independent tools supply their explicit writable home/cache directory.
// No VFS/archive search, source-map directory or automatic environment fallback.
std::string CacheRoot(const std::string &homePath);
class FileReader : public Reader
{
  public:
	~FileReader() override;
	uint64_t Size() const override;
	Error Read(uint64_t, void *, size_t) const override;
	static Error OpenFile(const std::string &path, std::unique_ptr<FileReader> &out);

  private:
	struct Impl;
	explicit FileReader(std::unique_ptr<Impl>);
	std::unique_ptr<Impl> impl_;
};
// Calls are explicit owner/tool operations, never made by read-only consumers.
// Reads hold an open file across atomic replacements. FileReader calls must be
// serialized; callers retain it for later ReadChunk. Outputs unchanged on error.
class Cache
{
  public:
	explicit Cache(std::string root) : root_(std::move(root)) {}
	Error Load(const Fingerprint &, ReconstructedLightingAsset &, Index &,
			   std::unique_ptr<FileReader> &, const Limits & = Limits()) const;
	Error Store(const Fingerprint &, const ReconstructedLightingAsset &,
				const std::vector<Chunk> & = std::vector<Chunk>(), const Limits & = Limits()) const;
	const std::string &Root() const
	{
		return root_;
	}

  private:
	std::string root_;
};
} // namespace Disk
} // namespace Rend2LightRecon
