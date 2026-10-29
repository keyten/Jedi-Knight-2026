// Offline copy of the r_animSoftLimits range scan (G2_AnimSoftBuildBone in
// shared/rd-rend2/tr_ghoul2.cpp): prints each bone's measured joint range and
// times the scan.
// Build: g++ -O2 -std=c++11 -I../../shared/rd-rend2 gla_range_dump.cpp -o gla_range_dump
// Run:   gla_range_dump _humanoid.gla [margin]

#include "G2_softlimits.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <chrono>
#include <vector>
#include <array>

struct Header
{
	int ident, version;
	char name[64];
	float scale;
	int numFrames, ofsFrames, numBones, ofsCompBonePool, ofsSkel, ofsEnd;
};

struct Skel
{
	char name[64];
	unsigned int flags;
	int parent;
	float base[3][4];
	float baseInv[3][4];
	int numChildren;
};

int main(int argc, char **argv)
{
	if (argc < 2)
	{
		printf("usage: gla_range_dump file.gla [margin]\n");
		return 1;
	}
	const float margin = argc > 2 ? (float)atof(argv[2]) : 0.1f;
	FILE *f = fopen(argv[1], "rb");
	if (!f) { printf("cannot open %s\n", argv[1]); return 1; }
	fseek(f, 0, SEEK_END);
	const long size = ftell(f);
	fseek(f, 0, SEEK_SET);
	std::vector<unsigned char> buf(size);
	fread(buf.data(), 1, size, f);
	fclose(f);

	const unsigned char *base = buf.data();
	const Header *h = (const Header *)base;
	printf("%s: %d bones, %d frames\n", h->name, h->numBones, h->numFrames);
	const int *offsets = (const int *)(base + sizeof(Header));

	const auto t0 = std::chrono::steady_clock::now();
	int limited = 0, uniqueTotal = 0;
	int poolEnd = h->ofsEnd;
	if (h->ofsSkel > h->ofsCompBonePool && h->ofsSkel < poolEnd) poolEnd = h->ofsSkel;
	if (h->ofsFrames > h->ofsCompBonePool && h->ofsFrames < poolEnd) poolEnd = h->ofsFrames;
	const int poolCount = (poolEnd - h->ofsCompBonePool) / 14;
	std::vector<int> seen(poolCount, -1);
	std::vector<float> quats, scratch;
	for (int b = 0; b < h->numBones; b++)
	{
		const Skel *s = (const Skel *)((const unsigned char *)offsets + offsets[b]);
		const bool skip = s->parent < 0 || !strcmp(s->name, "model_root") || !strcmp(s->name, "Motion") ||
			!strcmp(s->name, "pelvis") || strstr(s->name, "tag");
		if (skip) { printf("%-16s skipped\n", s->name); continue; }

		float B[3][3], Bo[3][3];
		for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) B[i][j] = s->base[i][j];
		g2soft::Orthonormalize(B, Bo);

		quats.clear();
		for (int fr = 0; fr < h->numFrames; fr++)
		{
			const unsigned char *ix = base + h->ofsFrames + (fr * h->numBones * 3) + b * 3;
			const int pool = (ix[2] << 16) + (ix[1] << 8) + ix[0];
			if (pool < 0 || pool >= poolCount || seen[pool] == b) continue;
			seen[pool] = b;
			float q[4];
			g2soft::UnpackGLAQuat(base + h->ofsCompBonePool + pool * 14, q);
			quats.insert(quats.end(), q, q + 4);
		}
		const int n = (int)quats.size() / 4;
		uniqueTotal += n;
		scratch.resize(7 * n);
		g2soft::BoneRange br;
		g2soft::ScanBoneRange((const float (*)[4])quats.data(), n, Bo, margin, scratch.data(), br);
		printf("%-16s %6d", s->name, n);
		for (int k = 0; k < 3; k++)
			printf("  [%7.1f %7.1f]%s", br.lo[k] * 57.29578f, br.hi[k] * 57.29578f, br.axisValid[k] ? " " : "x");
		printf("\n");
		limited += br.valid;
	}
	const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
	printf("%d of %d bones limited, %d unique poses, scan %.1f ms\n", limited, h->numBones, uniqueTotal, ms);
	return 0;
}
