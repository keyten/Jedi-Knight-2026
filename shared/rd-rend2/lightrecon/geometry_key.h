// Bounded canonical hash chaining; not a file checksum. No native struct padding/endian.
#pragma once
#include "disk.h"
#include <cstring>
namespace Rend2LightRecon
{
namespace Core
{
class GeometryKey
{
	unsigned char buffer_[4096] = {};
	size_t used_ = 32;
	uint64_t bytes_ = 0;
	void Flush()
	{
		auto h = Disk::Hash(buffer_, used_);
		std::memcpy(buffer_, h.data(), 32);
		used_ = 32;
	}

  public:
	void U(uint64_t v)
	{
		for (unsigned k = 0; k < 8; ++k)
		{
			buffer_[used_++] = uint8_t(v >> (8 * k));
			++bytes_;
			if (used_ == sizeof(buffer_))
				Flush();
		}
	}
	void F(double v)
	{
		uint64_t bits;
		if (v == 0)
			v = 0;
		std::memcpy(&bits, &v, 8);
		U(bits);
	}
	void V(Vec3 v)
	{
		F(v.x);
		F(v.y);
		F(v.z);
	}
	void B(const Bounds &b)
	{
		U(b.known);
		V(b.minWorld);
		V(b.maxWorld);
	}
	void Digest(const Disk::Digest &d)
	{
		for (auto v : d)
			U(v);
	}
	Disk::Digest Finish()
	{
		auto count = bytes_;
		U(count);
		Flush();
		Disk::Digest d;
		std::memcpy(d.data(), buffer_, 32);
		return d;
	}
};
} // namespace Core
} // namespace Rend2LightRecon
