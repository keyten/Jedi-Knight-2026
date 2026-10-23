// Renderer adapter for canonical CPU policy. No lighting integration yet.
#pragma once
#include "lightrecon/lifecycle.h"
#include "lightrecon/lightmaps.h"

// Retain this lease while using views and effective-state decisions. Loading is
// independent of consumption; acquiring a snapshot never runs work.
Rend2LightRecon::Snapshot R_ReconstructedLighting(void);
Rend2LightRecon::ConsumerState R_LightReconConsumer(const Rend2LightRecon::Snapshot&, Rend2LightRecon::Consumer);
void R_ClearReconstructedLighting(void);
void R_LightReconInfo_f(void);
// Resolves only the supported per-user fs_homepath; performs no I/O or loading.
std::string R_LightReconCacheRoot(void);
// Raw, uncalibrated observations, NOT a reconstructed asset/capability. Retain
// the lease for all views. No reads or reconstruction are triggered here.
std::shared_ptr<const Rend2LightRecon::Core::LightmapObservations> R_LightmapObservations(void);

// Stack guard for the entire MAIN world load. No use from R_LoadBSP/sub-BSPs.
// Counts are copied facts only; no borrowed hunk/renderer pointer is retained.
class LightReconWorldLoad
{
public:
	explicit LightReconWorldLoad(const char* name);
	~LightReconWorldLoad();
	void Finish(int surfaces, int gridSamples, int lightmapTextures);
private:
	Rend2LightRecon::WorldLoadGuard load_;
	bool finished_ = false;
};

// Scoped BSP adapter. Sub-BSPs are intentionally excluded from main-world
// observations. Unknown-size collision handoffs require a bounded VFS reread;
// normal FS_ReadFile buffers are borrowed only until ReleaseSource().
class LightReconBspCapture
{
public:
	LightReconBspCapture(const char* name, bool mainWorld, const void* originalBsp, long sourceBytes);
	LightReconBspCapture(const LightReconBspCapture&) = delete;
	LightReconBspCapture& operator=(const LightReconBspCapture&) = delete;
	void Embedded(int offset, int length, bool interleavedDeluxe);
	void External(int logicalIndex, bool deluxe, const char* source, int width, int height,
		const void* pixels, bool hdr);
	void Missing(int logicalIndex, bool deluxe, const char* source, bool hdr);
	bool Needed() const;
	void ReleaseSource();
private:
	const void* source_ = nullptr; // scoped original, never published
	size_t sourceBytes_ = 0;
	std::vector<uint8_t> reread_;
	bool active_ = false;
};
