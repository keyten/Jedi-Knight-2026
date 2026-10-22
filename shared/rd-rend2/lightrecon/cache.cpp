// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "cache.h"
#include <atomic>
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <limits>
#include <new>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace Rend2LightRecon
{
namespace Disk
{
namespace
{
Error Fail(Code code, const char *message)
{
	Error e;
	e.code = code;
	e.message = message;
	return e;
}
bool SafeRoot(const std::string &path)
{
	if (path.empty() || path.find('\0') != std::string::npos)
		return false;
	std::string p = path;
	for (char &c : p)
		if (c == '\\')
			c = '/';
	if (p[0] != '/' && !(p.size() > 2 && p[1] == ':' && p[2] == '/'))
		return false;
	return p != "/" && p.find("/../") == std::string::npos && p.find("/./") == std::string::npos &&
		   (p.size() < 3 || p.substr(p.size() - 3) != "/..") && p.substr(p.size() - 2) != "/.";
}
#ifdef _WIN32
std::wstring Wide(const std::string &s)
{
	if (s.size() > size_t(std::numeric_limits<int>::max()))
		return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
	std::wstring w(size_t(n), L'\0');
	if (n)
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), &w[0], n);
	return w;
}
bool Directory(const std::string &p)
{
	auto w = Wide(p);
	if (w.empty())
		return false;
	DWORD attr = GetFileAttributesW(w.c_str());
	if (attr != INVALID_FILE_ATTRIBUTES)
		return (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
	return CreateDirectoryW(w.c_str(), nullptr) != 0 || GetLastError() == ERROR_ALREADY_EXISTS;
}
void Remove(const std::string &p)
{
	DeleteFileW(Wide(p).c_str());
}
#else
bool Directory(const std::string &p)
{
	struct stat s;
	if (stat(p.c_str(), &s) == 0)
		return S_ISDIR(s.st_mode);
	return mkdir(p.c_str(), 0700) == 0;
}
void Remove(const std::string &p)
{
	unlink(p.c_str());
}
#endif
bool MakeDirectories(const std::string &path)
{
	std::string p = path;
	for (char &c : p)
		if (c == '\\')
			c = '/';
	for (size_t i = 1; i <= p.size(); ++i)
		if (i == p.size() || p[i] == '/')
		{
			if (p[i - 1] == '/' || p[i - 1] == ':')
				continue;
			// UNC server/share roots are supplied by the host, never created here.
			if (p.compare(0, 2, "//") == 0 && i <= p.find('/', 2))
				continue;
			if (!Directory(p.substr(0, i)))
				return false;
		}
	return true;
}
Error AtomicWrite(const std::string &path, const std::vector<uint8_t> &bytes)
{
	static std::atomic<uint64_t> serial{0};
#ifdef _WIN32
	uint64_t pid = GetCurrentProcessId();
#else
	uint64_t pid = uint64_t(getpid());
#endif
	std::string temporary;
#ifdef _WIN32
	HANDLE file = INVALID_HANDLE_VALUE;
	for (unsigned attempt = 0; attempt < 64 && file == INVALID_HANDLE_VALUE; ++attempt)
	{
		temporary = path + ".tmp-" + std::to_string(pid) + "-" + std::to_string(++serial);
		file = CreateFileW(Wide(temporary).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
						   FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE && GetLastError() != ERROR_FILE_EXISTS)
			return Fail(Code::IoError, "create cache temporary failed");
	}
	if (file == INVALID_HANDLE_VALUE)
		return Fail(Code::IoError, "temporary name collision");
	bool ok = true;
	size_t pos = 0;
	while (ok && pos < bytes.size())
	{
		DWORD written = 0;
		DWORD n = DWORD((std::min)(bytes.size() - pos, size_t(1024 * 1024)));
		ok = WriteFile(file, bytes.data() + pos, n, &written, nullptr) != 0 && written != 0;
		pos += written;
	}
	if (ok)
		ok = FlushFileBuffers(file) != 0;
	if (!CloseHandle(file))
		ok = false;
	if (ok)
	{
		// ReplaceFile preserves old open read leases on Windows. MoveFileEx with
		// REPLACE_EXISTING can reject these even with FILE_SHARE_DELETE.
		ok = ReplaceFileW(Wide(path).c_str(), Wide(temporary).c_str(), nullptr, 0, nullptr,
						  nullptr) != 0;
		if (!ok &&
			(GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND))
			ok = MoveFileExW(Wide(temporary).c_str(), Wide(path).c_str(), MOVEFILE_WRITE_THROUGH) !=
				 0;
	}
	DWORD failure = ok ? 0 : GetLastError();
#else
	int file = -1;
	for (unsigned attempt = 0; attempt < 64 && file < 0; ++attempt)
	{
		temporary = path + ".tmp-" + std::to_string(pid) + "-" + std::to_string(++serial);
		file = open(temporary.c_str(), O_CREAT | O_EXCL | O_WRONLY, 0600);
		if (file < 0 && errno != EEXIST)
			return Fail(Code::IoError, "create cache temporary failed");
	}
	if (file < 0)
		return Fail(Code::IoError, "temporary name collision");
	bool ok = true;
	size_t pos = 0;
	while (ok && pos < bytes.size())
	{
		ssize_t n =
			write(file, bytes.data() + pos, (std::min)(bytes.size() - pos, size_t(1024 * 1024)));
		if (n < 0 && errno == EINTR)
			continue;
		ok = n > 0;
		if (ok)
			pos += size_t(n);
	}
	if (ok)
		ok = fsync(file) == 0;
	if (close(file) != 0)
		ok = false;
	if (ok)
		ok = rename(temporary.c_str(), path.c_str()) == 0;
	if (ok)
	{
		std::string parent = path.substr(0, path.find_last_of('/'));
		int dir = open(parent.c_str(), O_RDONLY);
		if (dir >= 0)
		{
			ok = fsync(dir) == 0;
			close(dir);
		}
		else
			ok = false;
	}
#endif
	if (!ok)
	{
		Remove(temporary);
		Error e = Fail(Code::IoError, "atomic cache write/flush/replace failed");
#ifdef _WIN32
		e.message += " (Win32 " + std::to_string(failure) + ")";
#endif
		return e;
	}
	return Error();
}
} // namespace
struct FileReader::Impl
{
	std::FILE *file = nullptr;
	uint64_t size = 0;
	~Impl()
	{
		if (file)
			std::fclose(file);
	}
};
FileReader::FileReader(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
FileReader::~FileReader() = default;
uint64_t FileReader::Size() const
{
	return impl_->size;
}
Error FileReader::OpenFile(const std::string &path, std::unique_ptr<FileReader> &out)
{
	try
	{
		std::unique_ptr<Impl> p(new Impl);
#ifdef _WIN32
		// Share delete permits immutable old readers to survive atomic replacement.
		HANDLE h =
			CreateFileW(Wide(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
						nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE)
		{
			DWORD e = GetLastError();
			return Fail(e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? Code::Missing
																			   : Code::IoError,
						"open cache file failed");
		}
		int fd = _open_osfhandle(intptr_t(h), _O_RDONLY | _O_BINARY);
		if (fd < 0)
		{
			CloseHandle(h);
			return Fail(Code::IoError, "open cache descriptor failed");
		}
		p->file = _fdopen(fd, "rb");
		if (!p->file)
		{
			_close(fd);
			return Fail(Code::IoError, "open cache stream failed");
		}
		if (_fseeki64(p->file, 0, SEEK_END) != 0)
			return Fail(Code::IoError, "seek cache failed");
		auto size = _ftelli64(p->file);
#else
		p->file = std::fopen(path.c_str(), "rb");
		if (!p->file)
			return Fail(errno == ENOENT ? Code::Missing : Code::IoError, "open cache file failed");
		if (fseeko(p->file, 0, SEEK_END) != 0)
			return Fail(Code::IoError, "seek cache failed");
		auto size = ftello(p->file);
#endif
		if (size < 0)
			return Fail(Code::IoError, "cache size failed");
		p->size = uint64_t(size);
		std::unique_ptr<FileReader> reader(new FileReader(std::move(p)));
		out.swap(reader);
		return Error();
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::LimitExceeded, "file reader allocation failed");
	}
}
Error FileReader::Read(uint64_t offset, void *out, size_t size) const
{
	if (offset > impl_->size || size > impl_->size - offset || (size && !out) ||
		offset > uint64_t(std::numeric_limits<int64_t>::max()))
		return Fail(Code::Corrupt, "invalid cache read range");
#ifdef _WIN32
	if (_fseeki64(impl_->file, int64_t(offset), SEEK_SET) != 0)
		return Fail(Code::IoError, "cache seek failed");
#else
	if (offset > uint64_t(std::numeric_limits<off_t>::max()))
		return Fail(Code::LimitExceeded, "native file offset width exceeded");
	if (fseeko(impl_->file, off_t(offset), SEEK_SET) != 0)
		return Fail(Code::IoError, "cache seek failed");
#endif
	if (size && std::fread(out, 1, size, impl_->file) != size)
		return Fail(Code::IoError, "short cache read");
	return Error();
}
std::string CacheRoot(const std::string &home)
{
	return SafeRoot(home) ? home + "/rend2/lightrecon/v1" : std::string();
}
Error Cache::Load(const Fingerprint &f, ReconstructedLightingAsset &out, Index &index,
				  std::unique_ptr<FileReader> &reader, const Limits &limits) const
{
	try
	{
		Digest key;
		Error e = MakeKey(f, key, limits);
		if (!e)
			return e;
		if (!SafeRoot(root_))
			return Fail(Code::InvalidArgument, "absolute writable cache root required");
		std::unique_ptr<FileReader> file;
		e = FileReader::OpenFile(root_ + "/" + Hex(key) + ".jklr", file);
		if (!e)
			return e;
		e = Disk::Open(*file, f, out, index, limits);
		if (e)
			reader.swap(file);
		return e;
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::LimitExceeded, "cache allocation failed");
	}
}
Error Cache::Store(const Fingerprint &f, const ReconstructedLightingAsset &asset,
				   const std::vector<Chunk> &chunks, const Limits &limits) const
{
	try
	{
		Digest key;
		Error e = MakeKey(f, key, limits);
		if (!e)
			return e;
		if (!SafeRoot(root_))
			return Fail(Code::InvalidArgument, "absolute writable cache root required");
		std::vector<uint8_t> bytes;
		e = Encode(asset, f, chunks, bytes, limits);
		if (!e)
			return e;
		if (!MakeDirectories(root_))
			return Fail(Code::IoError, "create cache directory failed");
		return AtomicWrite(root_ + "/" + Hex(key) + ".jklr", bytes);
	}
	catch (const std::bad_alloc &)
	{
		return Fail(Code::LimitExceeded, "cache allocation failed");
	}
}
} // namespace Disk
} // namespace Rend2LightRecon
