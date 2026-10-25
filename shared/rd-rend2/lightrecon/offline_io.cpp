// Native streaming/atomic adapter. Binary-format logic is in offline.cpp.
// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "offline.h"
#include <atomic>
#include <cstdio>
#include <cerrno>
#include <limits>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <direct.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace Rend2LightRecon
{
namespace Core
{
namespace Offline
{
namespace
{
#ifdef _WIN32
std::wstring Wide(const std::string &s)
{
	int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
	std::wstring w(size_t(n), L'\0');
	if (n)
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), &w[0], n);
	return w;
}
bool Directory(const std::string &p)
{
	auto w = Wide(p);
	DWORD a = GetFileAttributesW(w.c_str());
	return a != INVALID_FILE_ATTRIBUTES ? (a & FILE_ATTRIBUTE_DIRECTORY) != 0
										: CreateDirectoryW(w.c_str(), nullptr) != 0;
}
void Remove(const std::string &p)
{
	DeleteFileW(Wide(p).c_str());
}
#else
bool Directory(const std::string &p)
{
	struct stat s;
	if (!stat(p.c_str(), &s))
		return S_ISDIR(s.st_mode);
	return !mkdir(p.c_str(), 0700);
}
void Remove(const std::string &p)
{
	unlink(p.c_str());
}
#endif
bool Parents(std::string p)
{
	for (char &c : p)
		if (c == '\\')
			c = '/';
	for (size_t i = 1; i < p.size(); ++i)
		if (p[i] == '/' && p[i - 1] != ':' && p[i - 1] != '/')
			if (!Directory(p.substr(0, i)))
				return false;
	return true;
}
class FileWriter final : public Writer
{
  public:
	FILE *file = nullptr;
	std::string temporary;
	bool ownsTemporary = false;
	~FileWriter()
	{
		if (file)
			std::fclose(file);
		if (ownsTemporary && !temporary.empty())
			Remove(temporary);
	}
	Error Write(uint64_t offset, const void *data, size_t bytes) override
	{
		if (offset > uint64_t(std::numeric_limits<int64_t>::max()))
			return Failure(Code::InvalidInput, "File offset overflow");
#ifdef _WIN32
		int seek = _fseeki64(file, int64_t(offset), SEEK_SET);
#else
		int seek = fseeko(file, off_t(offset), SEEK_SET);
#endif
		if (seek || (bytes && std::fwrite(data, 1, bytes, file) != bytes))
			return Failure(Code::ProviderError, "Dataset write failed");
		return {};
	}
};
} // namespace
Error ExportFile(const std::string &path, const Sources &s, const Options &o,
				 const ReadContext &ctx, Summary &summary)
{
	try
	{
		if (path.empty() || path.find('\0') != std::string::npos || !Parents(path))
			return Failure(Code::InvalidInput, "Invalid/unwritable dataset output path");
		static std::atomic<uint64_t> serial{0};
		FileWriter writer;
#ifdef _WIN32
		uint64_t pid = GetCurrentProcessId();
#else
		uint64_t pid = uint64_t(getpid());
#endif
		int fd = -1;
		for (unsigned n = 0; n < 64 && fd < 0; ++n)
		{
			writer.temporary =
				path + ".tmp-" + std::to_string(pid) + "-" + std::to_string(++serial);
#ifdef _WIN32
			fd = _wopen(Wide(writer.temporary).c_str(), _O_CREAT | _O_EXCL | _O_BINARY | _O_RDWR,
						_S_IREAD | _S_IWRITE);
#else
			fd = open(writer.temporary.c_str(), O_CREAT | O_EXCL | O_RDWR, 0600);
#endif
			if (fd < 0 && errno != EEXIST)
				return Failure(Code::ProviderError, "Cannot create exclusive dataset temporary");
		}
		if (fd < 0)
			return Failure(Code::ProviderError, "Dataset temporary collision limit");
		writer.ownsTemporary = true;
#ifdef _WIN32
		writer.file = _fdopen(fd, "w+b");
#else
		writer.file = fdopen(fd, "w+b");
#endif
		if (!writer.file)
		{
#ifdef _WIN32
			_close(fd);
#else
			close(fd);
#endif
			return Failure(Code::ProviderError, "Cannot open dataset stream");
		}
		Summary staged;
		auto result = Export(s, o, ctx, writer, staged);
		if (!result)
			return result;
		bool ok = std::fflush(writer.file) == 0;
#ifdef _WIN32
		ok = ok && _commit(fd) == 0;
#else
		ok = ok && fsync(fd) == 0;
#endif
		ok = std::fclose(writer.file) == 0 && ok;
		writer.file = nullptr;
		if (!ok)
			return Failure(Code::ProviderError, "Dataset flush failed");
#ifdef _WIN32
		ok = ReplaceFileW(Wide(path).c_str(), Wide(writer.temporary).c_str(), nullptr, 0, nullptr,
						  nullptr) != 0;
		if (!ok &&
			(GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND))
			ok = MoveFileExW(Wide(writer.temporary).c_str(), Wide(path).c_str(),
							 MOVEFILE_WRITE_THROUGH) != 0;
#else
		ok = rename(writer.temporary.c_str(), path.c_str()) == 0;
#endif
		if (!ok)
			return Failure(Code::ProviderError, "Atomic dataset replacement failed");
		writer.temporary.clear();
		summary = staged;
		return {};
	}
	catch (const std::bad_alloc &)
	{
		return Failure(Code::MemoryLimit, "Native dataset output allocation failed");
	}
}
Error OpenFile(const std::string &path, const Options &o, std::shared_ptr<const Dataset> &out)
{
	out.reset();
	std::unique_ptr<Disk::FileReader> file;
	auto e = Disk::FileReader::OpenFile(path, file);
	if (!e)
		return Failure(Code::ProviderError, e.message);
	return Dataset::Open(std::shared_ptr<const Disk::Reader>(file.release()), o, out);
}
} // namespace Offline
} // namespace Core
} // namespace Rend2LightRecon
