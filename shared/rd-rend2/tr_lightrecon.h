// Renderer adapter for canonical CPU policy. No lighting integration yet.
#pragma once
#include "lightrecon/lifecycle.h"
#include "lightrecon/lightmaps.h"
#include "lightrecon/lightgrid.h"
#include "lightrecon/surfaces.h"
#include "lightrecon/evidence.h"

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
std::shared_ptr<const Rend2LightRecon::Core::LightgridObservations> R_LightgridObservations(void);
std::shared_ptr<const Rend2LightRecon::Core::SurfaceGeometry> R_LuxelGeometry(void);
std::shared_ptr<const Rend2LightRecon::Core::EmitterEvidenceSet> R_EmitterEvidence(void);
void R_LightReconEvidence_f(void);
std::string R_LightReconShaderText(const char* name);
void R_LuxelDebug_f(void);
void R_LightReconObservations_f(void);
struct shader_s;

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
		const void* pixels, bool hdr, int pairedLightingIndex = -1);
	void Missing(int logicalIndex, bool deluxe, const char* source, bool hdr, int pairedLightingIndex = -1);
	bool Needed() const;
	void ReleaseSource();
	void GridEmbedded(int recordOffset, int recordBytes, int indexOffset, int indexBytes);
	void GridLayout(const float* origin, const float* spacing, const int* bounds);
	void GridHdr(const char* source, const void* data, int bytes);
	void GridHdrSidecar(const char* baseName); // bounded observation-only lookup when r_hdr is off
	void Surfaces(int surfaceOffset,int surfaceBytes,int vertexOffset,int vertexBytes,
		int indexOffset,int indexBytes,int modelOffset,int modelBytes,int shaderOffset,int shaderBytes);
	void SurfaceMaterial(unsigned ordinal, const shader_s*, unsigned bspSurfaceFlags, unsigned bspContentFlags);
	void VertexHdrSource(const char* source, const void*, size_t bytes, uint64_t sourceVertices);
	void VertexHdrSidecar(const char* baseName, uint64_t sourceVertices);
	bool GeometryNeeded() const;
private:
	Rend2LightRecon::Core::LightmapMapIdentity sourceMap_;
	const void* source_ = nullptr; // scoped original, never published
	size_t sourceBytes_ = 0;
	std::vector<uint8_t> reread_;
	bool active_ = false;
};
