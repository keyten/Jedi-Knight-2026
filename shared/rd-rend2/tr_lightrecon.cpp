// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "tr_local.h"
#include "tr_lightrecon.h"
#include "lightrecon/cache.h"
#include "lightrecon/offline.h"
#include "lightrecon/dependencies.h"
#include "lightrecon/diagnostics.h"
#include <algorithm>
#include <new>
#include <cmath>
#include <cstring>
#include <set>
#include <map>
#include <cerrno>
#include <climits>
#include <sstream>
#include <chrono>
#include <cstdlib>

qhandle_t RE_RegisterShaderFromImage(const char*,const int*,const byte*,image_t*,qboolean);

std::string R_LightReconCacheRoot(void)
{
	char home[MAX_OSPATH]{};
	ri.Cvar_VariableStringBuffer("fs_homepath", home, sizeof(home));
	return Rend2LightRecon::Disk::CacheRoot(home);
}

// Nontrivial ownership stays outside hunk-allocated/memset world_t. One writer
// follows the main world's lifetime; leases keep old publications safely alive.
static Rend2LightRecon::WorldLighting s_reconstructedLighting;
static Rend2LightRecon::Core::LightmapCapture s_lightmapCapture;
static Rend2LightRecon::Core::LightgridCapture s_lightgridCapture;
static Rend2LightRecon::Core::SurfaceCapture s_surfaceCapture;
static std::shared_ptr<const Rend2LightRecon::Core::BspRegionDomain> s_offlineDomain;
static std::vector<Rend2LightRecon::Core::DynamicBoundary> s_offlineDynamic;
static Rend2LightRecon::Core::Error s_offlineSpaceError;
static std::shared_ptr<const Rend2LightRecon::Core::EmitterEvidenceSet> s_emitterEvidence;
static Rend2LightRecon::Core::Error s_emitterEvidenceError;
static void R_CaptureEmitterEvidence();
static void R_LightReconEarlyCache(uint64_t ticket);
static Rend2LightRecon::Disk::Dependencies::Work s_dependencyWork;
std::shared_ptr<const Rend2LightRecon::Core::EmitterEvidenceSet> R_EmitterEvidence(void)
{
	return std::atomic_load(&s_emitterEvidence);
}
static std::vector<Rend2LightRecon::Core::SurfaceLightingObservation> s_luxelDebug;
static qhandle_t s_luxelDebugShader = 0;
struct ReconstructionPreviewMarker
{
    unsigned long long id = 0;
    unsigned kind = 0, priorOnly = 0;
    vec3_t position{}, right{}, up{}, forward{};
    float width = 0, height = 0, length = 0, radius = 0, confidence = 0;
    unsigned confidenceKnown = 0;
};
static std::vector<ReconstructionPreviewMarker> s_reconstructionPreview;
namespace LRDebug = Rend2LightRecon::Core::Diagnostics;
static std::shared_ptr<const LRDebug::Scene> s_diagnosticScene;
static LRDebug::Selection s_diagnosticSelection;
static uint64_t s_diagnosticTicket=0,s_diagnosticDrawn=0,s_diagnosticClipped=0;
static double s_diagnosticMicroseconds=0;
static std::shared_ptr<const Rend2LightRecon::Core::StaticVisibility> s_diagnosticRays;

std::shared_ptr<const Rend2LightRecon::Core::SurfaceGeometry> R_LuxelGeometry(void)
{
	return s_surfaceCapture.Acquire();
}

std::shared_ptr<const Rend2LightRecon::Core::LightgridObservations> R_LightgridObservations(void)
{
	return s_lightgridCapture.Acquire();
}

std::shared_ptr<const Rend2LightRecon::Core::LightmapObservations> R_LightmapObservations(void)
{
	return s_lightmapCapture.Acquire();
}

Rend2LightRecon::Snapshot R_ReconstructedLighting(void)
{
	return s_reconstructedLighting.Acquire();
}

LightReconWorldLoad::LightReconWorldLoad(const char* name)
	: load_(s_reconstructedLighting, name ? name : "",
		static_cast<Rend2LightRecon::BuildPolicy>(r_lightReconBuild ? r_lightReconBuild->integer : 0))
{
	R_ClearResolvedSun();
	s_dependencyWork = {};
	s_diagnosticScene.reset();s_diagnosticSelection={};s_diagnosticTicket=0;
	s_diagnosticRays.reset();
	R_LightReconEarlyCache(load_.Ticket());
	// A validated hit needs no original-observation derivation. Cache-only misses
	// also never capture or reconstruct. Ordinary BSP/GPU loading is unchanged.
	auto capturePolicy = s_reconstructedLighting.Report().pendingCacheHit
		? Rend2LightRecon::BuildPolicy::Disabled : s_reconstructedLighting.Report().policy;
	s_lightmapCapture.Begin(capturePolicy);
	s_lightgridCapture.Begin(capturePolicy);
	s_surfaceCapture.Begin(capturePolicy);
	s_offlineDomain.reset();
	s_offlineDynamic.clear();
	s_offlineSpaceError = {};
	s_luxelDebug.clear();
	s_reconstructionPreview.clear();
	std::atomic_store(&s_emitterEvidence,
					  std::shared_ptr<const Rend2LightRecon::Core::EmitterEvidenceSet>());
	s_emitterEvidenceError = {};
}

LightReconWorldLoad::~LightReconWorldLoad()
{
	if (!finished_ && s_reconstructedLighting.Report().ticket == load_.Ticket())
	{
		s_lightmapCapture.Reset();
		s_lightgridCapture.Reset();
		s_surfaceCapture.Reset();
		s_offlineDomain.reset();
		s_offlineDynamic.clear();
		std::atomic_store(&s_emitterEvidence,
						  std::shared_ptr<const Rend2LightRecon::Core::EmitterEvidenceSet>());
		s_luxelDebug.clear();
	s_reconstructionPreview.clear();
		s_diagnosticScene.reset();s_diagnosticSelection={};s_diagnosticTicket=0;s_diagnosticRays.reset();
	}
}

namespace {
class DependencyBytes final : public Rend2LightRecon::Disk::Reader
{
public:
	std::vector<uint8_t> bytes;
	uint64_t Size() const override { return bytes.size(); }
	Rend2LightRecon::Disk::Error Read(uint64_t offset, void *target, size_t count) const override {
		Rend2LightRecon::Disk::Error e;
		if (offset > bytes.size() || count > bytes.size()-offset || (count && !target)) {
			e.code=Rend2LightRecon::Disk::Code::Corrupt; e.message="Dependency read extent";
		} else if(count) std::memcpy(target, bytes.data()+size_t(offset), count);
		return e;
	}
};
class RendererDependencies final : public Rend2LightRecon::Disk::Dependencies::Sources
{
public:
	Rend2LightRecon::Disk::Error Open(const std::string &path,
		std::unique_ptr<Rend2LightRecon::Disk::Reader> &out) const override {
		using namespace Rend2LightRecon::Disk;
		Error e; fileHandle_t h=0;
		long count=ri.FS_FOpenFileRead(path.c_str(), &h, qtrue);
		if(!h) {e.code=Code::Missing;e.message="Absent VFS dependency";return e;}
		struct Close {fileHandle_t h;~Close(){ri.FS_FCloseFile(h);}} close{h};
		if(count<0 || uint64_t(count)>(256ull<<20)) {e.code=Code::LimitExceeded;e.message="VFS dependency size limit";return e;}
		std::unique_ptr<DependencyBytes> data(new DependencyBytes);
		data->bytes.resize(size_t(count));
		if(count && ri.FS_Read(data->bytes.data(),int(count),h)!=count) {e.code=Code::IoError;e.message="Short VFS dependency read";return e;}
		out=std::move(data);return e;
	}
	Rend2LightRecon::Disk::Error Material(const std::string &name, std::string &text) const override {
		bool found=false;
		text=R_LightReconShaderText(name.c_str(),&found);Rend2LightRecon::Disk::Error e;
		if(text.empty()) {
			e.code=found?Rend2LightRecon::Disk::Code::NotCacheable:Rend2LightRecon::Disk::Code::Missing;
			e.message="Selected material definition is truncated/unsupported";
		}
		return e;
	}
};
class RendererWorldSource : public Rend2LightRecon::WorldSource
{
public:
	Rend2LightRecon::Disk::Error Describe(const std::string& name, Rend2LightRecon::Disk::Fingerprint& fingerprint) override
	{
		using namespace Rend2LightRecon::Disk;
		static_assert(LUMP_SURFACES==13 && LUMP_LIGHTMAPS==14 && sizeof(dsurface_t)==148 && sizeof(dshader_t)==72,
			"Update portable dependency inventory for a changed BSP schema");
		RendererDependencies source;
		std::unique_ptr<Reader> bsp;
		auto error=source.Open(name,bsp);if(!error)return error;
		Dependencies::Recipe recipe;
#ifdef REND2_SP
		recipe.materialContext="rend2-sp/source-materials/1";
#else
		recipe.materialContext="rend2-mp/source-materials/1";
#endif
		recipe.decoder="RBSP1/original-rgb8-grid-hdr/deluxe/vertex/source-images-jpg-png-tga/srgb/1";
		recipe.geometry="original-visual-triangles-patch-model-ownership/occupancy-v2/mounting-v1";
		recipe.profiles="UnknownStock/conditional-source-locked-NetRadiant-alternatives-v1";
		recipe.solver="renderer-offline-import/1;producer7;joint7;no-automatic-solver";
		recipe.settings="immutable-original/full-map/offline-explicit-model;receiver-support-v1";
		recipe.calibration="relative-stock-no-absolute-scale/physical-contract-v1";
		recipe.units.worldUnitsPerMeter=r_pbrWorldUnitsPerMeter->value;
		recipe.units.physicalToScene=R_PBRPhysicalToScene();
		return Dependencies::Collect(name,*bsp,source,recipe,fingerprint,s_dependencyWork);
	}
	Rend2LightRecon::Core::Error Prepare(const std::string&, Rend2LightRecon::BuildPlan& plan) override
	{
		plan.input.lightmaps = R_LightmapObservations();
		plan.input.lightgrid = R_LightgridObservations();
		plan.input.surfaceGeometry = R_LuxelGeometry();
		R_CaptureEmitterEvidence();
		plan.input.emitterEvidence = R_EmitterEvidence();
		return Rend2LightRecon::Core::Failure(Rend2LightRecon::Core::Code::Unavailable,
			"Original BSP observations may be captured; complete solver input/material adapter and reconstruction algorithm are not installed");
	}
};
}
static void R_LightReconEarlyCache(uint64_t ticket)
{
	if(s_reconstructedLighting.Report().policy==Rend2LightRecon::BuildPolicy::Disabled)return;
	RendererWorldSource source;
	Rend2LightRecon::Disk::Persistent::Selection selection;
	selection.discoverReflectedIrradiance = r_lightReconEntities && r_lightReconEntities->integer;
	Rend2LightRecon::NativeWorldCache cache(R_LightReconCacheRoot(), selection);
	s_reconstructedLighting.Preflight(ticket,source,&cache);
}
void LightReconWorldLoad::Finish(int surfaces, int gridSamples, int lightmapTextures)
{
	if (s_lightmapCapture.Needed())
		s_lightmapCapture.Freeze();
	if (s_lightgridCapture.Needed())
		s_lightgridCapture.Freeze();
	if (s_surfaceCapture.Needed())
		s_surfaceCapture.Freeze();
	RendererWorldSource source;
	Rend2LightRecon::WorldFacts facts;
	if (s_reconstructedLighting.Report().policy == Rend2LightRecon::BuildPolicy::Disabled)
	{
		load_.Finish(facts, source); // no path lookup, dependency hashing, disk I/O or sessions
		finished_ = true;
		return;
	}
	facts.geometryReady = facts.areaLightsReady = true;
	facts.surfaces = static_cast<uint64_t>(std::max(0, surfaces));
	facts.gridSamples = static_cast<uint64_t>(std::max(0, gridSamples));
	facts.lightmapTextures = static_cast<uint64_t>(std::max(0, lightmapTextures));
	Rend2LightRecon::Disk::Persistent::Selection selection;
	selection.discoverReflectedIrradiance = r_lightReconEntities && r_lightReconEntities->integer;
	Rend2LightRecon::NativeWorldCache cache(R_LightReconCacheRoot(), selection);
	load_.Finish(facts, source, &cache);
	// A completed build/cache hit needs no resident source images. Currently the
	// renderer has no complete cache key/solver, so BuildRequired keeps a bounded
	// immutable input snapshot for a future session and CPU inspection.
	if (s_reconstructedLighting.Report().state == Rend2LightRecon::WorldState::Available)
	{
		s_lightmapCapture.Reset();
		s_lightgridCapture.Reset();
		s_surfaceCapture.Reset();
		s_offlineDomain.reset();
		s_offlineDynamic.clear();
		std::atomic_store(&s_emitterEvidence,
						  std::shared_ptr<const Rend2LightRecon::Core::EmitterEvidenceSet>());
	}
	finished_ = true;
}

void R_ClearReconstructedLighting(void)
{
	s_diagnosticScene.reset();s_diagnosticSelection={};s_diagnosticTicket=0;
	s_diagnosticRays.reset();
	s_lightmapCapture.Reset();
	s_lightgridCapture.Reset();
	s_surfaceCapture.Reset();
	s_offlineDomain.reset();
	s_offlineDynamic.clear();
	std::atomic_store(&s_emitterEvidence,
					  std::shared_ptr<const Rend2LightRecon::Core::EmitterEvidenceSet>());
	std::vector<Rend2LightRecon::Core::SurfaceLightingObservation>().swap(s_luxelDebug);
	std::vector<ReconstructionPreviewMarker>().swap(s_reconstructionPreview);
	s_luxelDebugShader = 0;
	R_ClearResolvedSun();
	s_reconstructedLighting.Unload();
}

namespace {
struct LightmapSourceFile
{
	fileHandle_t handle = 0;
	~LightmapSourceFile() { if (handle) ri.FS_FCloseFile(handle); }
};
}
LightReconBspCapture::LightReconBspCapture(const char* name, bool mainWorld, const void* bsp, long bytes)
{
	using namespace Rend2LightRecon::Core;
	active_ = mainWorld && name && s_reconstructedLighting.Report().world == name && s_lightmapCapture.Needed();
	if (!active_) return;
	try
	{
		// SP's collision-cache import supplies no allocation length. Do not guess
		// one from lump offsets or Z_MemSize (which sums all allocations per tag).
		// Re-read that case through a single bounded handle; retain it only through
		// R_LoadLightmaps. Ordinary BSP reads reuse the existing exact-size buffer.
		if (bytes < 0)
		{
			LightmapSourceFile file;
			bytes = ri.FS_FOpenFileRead(name, &file.handle, qtrue);
			if (!file.handle || bytes < static_cast<long>(sizeof(dheader_t)))
			{
				s_lightmapCapture.Fail(Code::ProviderError, "Original cached BSP could not be read");
				return;
			}
			if (static_cast<uint64_t>(bytes) > s_lightmapCapture.Limits().maxSourceMapBytes)
			{
				s_lightmapCapture.Fail(Code::MemoryLimit, "Original cached BSP exceeds source limit");
				return;
			}
			reread_.resize(static_cast<size_t>(bytes));
			if (ri.FS_Read(reread_.data(), static_cast<int>(bytes), file.handle) != bytes)
			{
				s_lightmapCapture.Fail(Code::ProviderError, "Short original BSP read");
				ReleaseSource();
				return;
			}
			if (!bsp || memcmp(bsp, reread_.data(), sizeof(dheader_t)) != 0)
			{
				s_lightmapCapture.Fail(Code::ProviderError, "BSP changed since collision-cache load");
				ReleaseSource();
				return;
			}
			bsp = reread_.data();
		}
		if (bytes < static_cast<long>(sizeof(dheader_t)))
		{
			s_lightmapCapture.Fail(Code::InvalidInput, "Truncated source BSP");
			return;
		}
		if (s_lightmapCapture.SetMap(name, bsp, static_cast<size_t>(bytes)))
		{
			sourceMap_ = s_lightmapCapture.SourceIdentity();
			source_ = bsp;
			sourceBytes_ = static_cast<size_t>(bytes);
			if (s_surfaceCapture.Needed())
			{
				ReadContext context;
				context.maxBytes = 256ull * 1024 * 1024;
				s_offlineSpaceError = Offline::CaptureBspSpace(
					source_, sourceBytes_, sourceMap_, context, s_offlineDomain, s_offlineDynamic);
			}
		}
	}
	catch (const std::bad_alloc&) { s_lightmapCapture.Fail(Code::MemoryLimit, "BSP capture allocation failed"); }
}
bool LightReconBspCapture::Needed() const
{
	return active_ && s_lightmapCapture.Needed();
}
void LightReconBspCapture::ReleaseSource()
{
	source_ = nullptr;
	sourceBytes_ = 0;
	std::vector<uint8_t>().swap(reread_);
}
void LightReconBspCapture::GridEmbedded(int ro, int rb, int io, int ib)
{
	using namespace Rend2LightRecon::Core;
	static_assert(sizeof(mgrid_t) == kGridRecordBytes && MAXLIGHTMAPS == kGridStyles,
		"Update the canonical JA record adapter for a changed BSP schema");
	if (!active_ || !s_lightgridCapture.Needed()) return;
	if (!source_ || ro < 0 || rb < 0 || io < 0 || ib < 0 ||
		static_cast<size_t>(ro) > sourceBytes_ || static_cast<size_t>(rb) > sourceBytes_ - static_cast<size_t>(ro) ||
		static_cast<size_t>(io) > sourceBytes_ || static_cast<size_t>(ib) > sourceBytes_ - static_cast<size_t>(io))
	{
		s_lightgridCapture.Fail(Code::InvalidInput, "Invalid original lightgrid lump span");
		return;
	}
	const auto* bytes = static_cast<const uint8_t*>(source_);
	s_lightgridCapture.SetSource(sourceMap_, bytes + ro, rb, ro, bytes + io, ib, io);
}
void LightReconBspCapture::Surfaces(int so, int sb, int vo, int vb, int io, int ib, int mo, int mb,
									int sho, int shb)
{
	using namespace Rend2LightRecon::Core;
	if (!active_ || !s_surfaceCapture.Needed())
		return;
	auto span = [&](int offset, int bytes) {
		return source_ && offset >= 0 && bytes >= 0 &&
			   static_cast<size_t>(offset) <= sourceBytes_ &&
			   static_cast<size_t>(bytes) <= sourceBytes_ - static_cast<size_t>(offset);
	};
	if (!span(so, sb) || !span(vo, vb) || !span(io, ib) || !span(mo, mb) || !span(sho, shb) ||
		so % 4 || vo % 4 || io % 4 || mo % 4 || sho % 4 || sb % sizeof(dsurface_t) ||
		vb % sizeof(drawVert_t) || ib % 4 || mb % sizeof(dmodel_t) || shb % sizeof(dshader_t))
	{
		s_surfaceCapture.Fail(Code::InvalidInput, "Malformed original geometry lumps");
		return;
	}
	const auto *bytes = static_cast<const byte *>(source_);
	const auto *surfaces = reinterpret_cast<const dsurface_t *>(bytes + so);
	const auto *verts = reinterpret_cast<const drawVert_t *>(bytes + vo);
	const auto *indices = reinterpret_cast<const int *>(bytes + io);
	const auto *models = reinterpret_cast<const dmodel_t *>(bytes + mo);
	const auto *shaders = reinterpret_cast<const dshader_t *>(bytes + sho);
	const size_t count = sb / sizeof(dsurface_t), vertexCount = vb / sizeof(drawVert_t),
				 indexCount = ib / 4, modelCount = mb / sizeof(dmodel_t),
				 shaderCount = shb / sizeof(dshader_t);
	const auto &limit = s_surfaceCapture.Limits();
	if (count > limit.maxSurfaces || vertexCount > limit.maxVertices ||
		indexCount > limit.maxIndices)
	{
		s_surfaceCapture.Fail(Code::MemoryLimit, "Original geometry counts exceed capture limits");
		return;
	}
	try
	{
		if (!s_surfaceCapture.SetMap(sourceMap_))
			return;
		std::vector<uint32_t> owners(count, UINT32_MAX);
		for (size_t m = 0; m < modelCount; ++m)
		{
			int first = LittleLong(models[m].firstSurface), n = LittleLong(models[m].numSurfaces);
			if (first < 0 || n < 0 || static_cast<size_t>(first) > count ||
				static_cast<size_t>(n) > count - static_cast<size_t>(first))
			{
				s_surfaceCapture.Fail(Code::InvalidInput, "Invalid brush model surface span");
				return;
			}
			for (int j = 0; j < n; ++j)
			{
				auto &owner = owners[static_cast<size_t>(first) + j];
				owner = owner == UINT32_MAX ? static_cast<uint32_t>(m) : UINT32_MAX - 1;
			}
		}
		for (size_t i = 0; i < count; ++i)
		{
			const auto &ds = surfaces[i];
			BakedSurface s;
			s.ordinal = static_cast<uint32_t>(i);
			s.model = owners[i];
			s.motion = s.model == 0 ? SurfaceMotion::StaticWorld
									: s.model < modelCount ? SurfaceMotion::BrushModel
														   : SurfaceMotion::Unknown;
			int kind = LittleLong(ds.surfaceType);
			s.kind = kind == MST_PLANAR
						 ? BakedSurfaceKind::Face
						 : kind == MST_PATCH
							   ? BakedSurfaceKind::Patch
							   : kind == MST_TRIANGLE_SOUP
									 ? BakedSurfaceKind::Triangles
									 : kind == MST_FLARE ? BakedSurfaceKind::Flare
														 : BakedSurfaceKind::Unsupported;
			int shader = LittleLong(ds.shaderNum);
			if (shader >= 0 && static_cast<size_t>(shader) < shaderCount)
			{
				s.materialIndex = shader;
				s.material.assign(shaders[shader].shader,
								  strnlen(shaders[shader].shader, sizeof(shaders[shader].shader)));
			}
			else if (s.kind != BakedSurfaceKind::Flare)
				s.kind = BakedSurfaceKind::Unsupported;
			for (size_t slot = 0; slot < 4; ++slot)
			{
				s.lightmap[slot] = LittleLong(ds.lightmapNum[slot]);
				s.styles[slot] = ds.lightmapStyles[slot];
				s.vertexStyles[slot] = ds.vertexStyles[slot];
				s.chartX[slot] = LittleLong(ds.lightmapX[slot]);
				s.chartY[slot] = LittleLong(ds.lightmapY[slot]);
			}
			s.lightmapOrigin = Rend2LightRecon::Vec3(LittleFloat(ds.lightmapOrigin[0]),
													 LittleFloat(ds.lightmapOrigin[1]),
													 LittleFloat(ds.lightmapOrigin[2]));
			for (size_t j = 0; j < 3; ++j)
				s.lightmapBasis[j] = Rend2LightRecon::Vec3(LittleFloat(ds.lightmapVecs[j][0]),
														   LittleFloat(ds.lightmapVecs[j][1]),
														   LittleFloat(ds.lightmapVecs[j][2]));
			s.patchWidth = static_cast<uint32_t>(LittleLong(ds.patchWidth));
			s.patchHeight = static_cast<uint32_t>(LittleLong(ds.patchHeight));
			s.chartWidth = LittleLong(ds.lightmapWidth);
			s.chartHeight = LittleLong(ds.lightmapHeight);
			int fv = LittleLong(ds.firstVert), nv = LittleLong(ds.numVerts),
				fi = LittleLong(ds.firstIndex), ni = LittleLong(ds.numIndexes);
			if (s.kind != BakedSurfaceKind::Flare &&
				(fv < 0 || nv < 0 || fi < 0 || ni < 0 || static_cast<size_t>(fv) > vertexCount ||
				 static_cast<size_t>(nv) > vertexCount - static_cast<size_t>(fv) ||
				 static_cast<size_t>(fi) > indexCount ||
				 static_cast<size_t>(ni) > indexCount - static_cast<size_t>(fi)))
				s.kind = BakedSurfaceKind::Unsupported;
			if (s.kind != BakedSurfaceKind::Unsupported && s.kind != BakedSurfaceKind::Flare)
			{
				const uint64_t recordBytes = sizeof(BakedSurface) + s.material.size() +
											 uint64_t(nv) * sizeof(BakedVertex) +
											 uint64_t(ni) * sizeof(uint32_t);
				if (recordBytes > limit.maxBytes - s_surfaceCapture.Report().bytes)
				{
					s_surfaceCapture.Fail(Code::MemoryLimit,
										  "Surface geometry payload exceeds capture budget");
					return;
				}
				s.vertices.resize(nv);
				s.indices.resize(ni);
				for (int v = 0; v < nv; ++v)
				{
					auto &out = s.vertices[v];
					const auto &in = verts[fv + v];
					out.sourceVertex = static_cast<uint32_t>(fv + v);
					out.textureUv = LightmapUv(LittleFloat(in.st[0]), LittleFloat(in.st[1]));
					out.colourPresent = true;
					std::memcpy(out.colour, in.color, sizeof(out.colour));
					out.position = Rend2LightRecon::Vec3(
						LittleFloat(in.xyz[0]), LittleFloat(in.xyz[1]), LittleFloat(in.xyz[2]));
					out.shadingNormal =
						Rend2LightRecon::Vec3(LittleFloat(in.normal[0]), LittleFloat(in.normal[1]),
											  LittleFloat(in.normal[2]));
					for (size_t slot = 0; slot < 4; ++slot)
						out.lightmap[slot] = LightmapUv(LittleFloat(in.lightmap[slot][0]),
														LittleFloat(in.lightmap[slot][1]));
				}
				for (int j = 0; j < ni; ++j)
					s.indices[j] = static_cast<uint32_t>(LittleLong(indices[fi + j]));
			}
			if (!s_surfaceCapture.Add(std::move(s)))
				return;
		}
	}
	catch (const std::bad_alloc &)
	{
		s_surfaceCapture.Fail(Code::MemoryLimit, "Geometry capture allocation failed");
	}
}
void LightReconBspCapture::SurfaceMaterial(unsigned ordinal, const shader_s *shader,
										   unsigned bspSurfaceFlags, unsigned bspContentFlags)
{
	using namespace Rend2LightRecon::Core;
	if (!GeometryNeeded())
		return;
	MaterialFacts facts;
	facts.available = shader && shader != tr.defaultShader && !shader->defaultShader;
	facts.surfaceFlags = bspSurfaceFlags;
	facts.contentFlags = bspContentFlags;
	facts.uncertainty |= RuntimeShaderInterpretation;
	if (!facts.available)
		facts.signals |= MaterialMissing;
	if (bspSurfaceFlags & SURF_SKY)
		facts.signals |= MaterialSky;
	if (bspSurfaceFlags & SURF_NODRAW)
		facts.signals |= MaterialNoDraw;
	if (bspContentFlags & CONTENTS_TRANSLUCENT)
		facts.signals |= MaterialTranslucent;
	if (shader)
	{
		facts.cull = shader->cullType == CT_TWO_SIDED ? SurfaceCull::None :
			shader->cullType == CT_FRONT_SIDED ? SurfaceCull::Front : SurfaceCull::Back;
		if (shader->isSky)
			facts.signals |= MaterialSky;
		if (shader->surfaceFlags & SURF_NODRAW)
			facts.signals |= MaterialNoDraw;
		if (shader->contentFlags & CONTENTS_TRANSLUCENT)
			facts.signals |= MaterialTranslucent;
		if (shader->numDeforms)
			facts.signals |= MaterialDeformed;
		facts.legacySurfaceLight = shader->surfaceLight;
		facts.legacySourceColour =
			Rend2LightRecon::Vec3(shader->surfaceLightColor[0], shader->surfaceLightColor[1],
								  shader->surfaceLightColor[2]);
		for (int i = 0; i < std::min(shader->numUnfoggedPasses, MAX_SHADER_STAGES); ++i)
		{
			const shaderStage_t *stage = shader->stages[i];
			if (!stage || !stage->active)
				continue;
			const auto &bundle = stage->bundle[TB_DIFFUSEMAP];
			const bool lightall = stage->glslShaderGroup == tr.lightallShader;
			const int lightType = lightall ? stage->glslShaderIndex & LIGHTDEF_LIGHTTYPE_MASK : 0;
			if (bundle.isLightmap || stage->bundle[TB_LIGHTMAP].isLightmap ||
				lightType == LIGHTDEF_USE_LIGHTMAP)
				facts.signals |= MaterialLightmapped;
			if (lightType == LIGHTDEF_USE_LIGHT_VERTEX || stage->rgbGen == CGEN_VERTEX ||
				stage->rgbGen == CGEN_EXACT_VERTEX || stage->rgbGen == CGEN_VERTEX_LIT ||
				stage->rgbGen == CGEN_EXACT_VERTEX_LIT)
				facts.signals |= MaterialVertexLit;
			// Extra lightstyle passes are lighting channels, not translucent material layers.
			if (bundle.isLightmap || stage->rgbGen == CGEN_LIGHTMAPSTYLE)
				continue;
			if (stage->alphaTestType != ALPHA_TEST_NONE)
				facts.signals |= MaterialCutout;
			if (bundle.numImageAnimations > 1 || bundle.isVideoMap ||
				stage->rgbGen == CGEN_WAVEFORM || stage->alphaGen == AGEN_WAVEFORM)
				facts.signals |= MaterialAnimated;
			if (stage->rgbGen == CGEN_ENTITY || stage->rgbGen == CGEN_ONE_MINUS_ENTITY ||
				stage->rgbGen == CGEN_LIGHTING_DIFFUSE ||
				stage->rgbGen == CGEN_LIGHTING_DIFFUSE_ENTITY || stage->alphaGen == AGEN_ENTITY ||
				stage->alphaGen == AGEN_ONE_MINUS_ENTITY)
				facts.signals |= MaterialEntityDependent;
			if (bundle.tcGen == TCGEN_ENVIRONMENT_MAPPED ||
				bundle.tcGen == TCGEN_ENVIRONMENT_MAPPED_SP ||
				bundle.tcGen == TCGEN_ENVIRONMENT_MAPPED_SP_FP)
				facts.signals |= MaterialReflection;
			if (bundle.numTexMods)
				facts.uncertainty |= TextureTransformPresent;
			for (int j = 0; j < bundle.numTexMods; ++j)
				if (bundle.texMods[j].type == TMOD_SCROLL ||
					bundle.texMods[j].type == TMOD_ROTATE ||
					bundle.texMods[j].type == TMOD_STRETCH ||
					bundle.texMods[j].type == TMOD_TURBULENT ||
					bundle.texMods[j].type == TMOD_ENTITY_TRANSLATE)
					facts.signals |= MaterialAnimated;
			const image_t *image = bundle.image[0];
			const bool emission = stage->glow || stage->emissive || stage->emissivePhysical;
			if (emission)
			{
				facts.signals |= MaterialGlow;
				++facts.emissionLayerCount;
				facts.uncertainty |= EmissionUncalibrated;
				if (facts.emissionLayerCount == 1)
				{
					const image_t *emissionImage =
						stage->emissive && stage->bundle[TB_EMISSIVEMAP].image[0]
							? stage->bundle[TB_EMISSIVEMAP].image[0]
							: image;
					facts.emissionTexture = emissionImage ? emissionImage->imgName : "";
					const auto &emissionBundle =
						emissionImage == image ? bundle : stage->bundle[TB_EMISSIVEMAP];
					facts.emissionCoordinatesKnown = true;
					facts.emissionStaticCoordinates =
						emissionBundle.tcGen == TCGEN_TEXTURE && emissionBundle.numTexMods == 0;
					facts.emissionClamp =
						emissionImage && (emissionImage->flags & IMGFLAG_CLAMPTOEDGE);
					facts.emissionProvenance =
						stage->emissivePhysical ? EmissionProvenance::AuthoredLuminance
												: stage->emissive ? EmissionProvenance::LegacyShader
																  : EmissionProvenance::GlowTexture;
					facts.authoredLuminance =
						stage->emissivePhysical ? stage->emissiveLuminance : 0;
					facts.emissionColourKnown =
						stage->emissive || stage->emissivePhysical || stage->rgbGen == CGEN_CONST;
					facts.emissionColour = Rend2LightRecon::Vec3(
						stage->emissiveColor[0], stage->emissiveColor[1], stage->emissiveColor[2]);
					if (!stage->emissive && !stage->emissivePhysical && stage->rgbGen == CGEN_CONST)
						facts.emissionColour =
							Rend2LightRecon::Vec3(stage->constantColor[0], stage->constantColor[1],
												  stage->constantColor[2]);
				}
				continue;
			}
			// Opaque lightmap multiplication does not make an ordinary surface translucent.
			const auto blend = stage->stateBits & (GLS_SRCBLEND_BITS | GLS_DSTBLEND_BITS);
			const bool multiply = blend == (GLS_SRCBLEND_DST_COLOR | GLS_DSTBLEND_ZERO) ||
								  blend == (GLS_SRCBLEND_ZERO | GLS_DSTBLEND_SRC_COLOR);
			if (blend && !multiply && shader->sort > SS_OPAQUE)
				facts.signals |= MaterialTranslucent;
			if (image && !bundle.isLightmap && image != tr.whiteImage)
			{
				if (facts.baseTexture.empty())
				{
					facts.baseTexture = image->imgName;
					facts.baseLayerCount = 1;
					facts.baseTextureSrgb = (image->flags & IMGFLAG_SRGB) != 0;
					facts.baseModulationKnown = stage->rgbGen == CGEN_CONST;
					facts.baseModulation = Rend2LightRecon::Vec3(
						stage->constantColor[0], stage->constantColor[1], stage->constantColor[2]);
				}
				else if (facts.baseTexture != image->imgName)
				{
					++facts.baseLayerCount;
					facts.uncertainty |= MultipleBaseLayers;
				}
			}
		}
	}
	if (facts.signals & MaterialLightmapped)
		facts.meaning = BakedMeaning::DiffuseModulationBakeUnknown;
	if (!(facts.signals & (MaterialLightmapped | MaterialVertexLit | MaterialEntityDependent)))
		facts.signals |= MaterialUnlit;
	s_surfaceCapture.Material(ordinal, facts);
}
void LightReconBspCapture::VertexHdrSource(const char *path, const void *data, size_t bytes,
										   uint64_t sourceVertices)
{
	using namespace Rend2LightRecon::Core;
	if (!GeometryNeeded())
		return;
	s_surfaceCapture.VertexHdr(data ? VertexHdrState::Present : VertexHdrState::Missing,
							   path ? path : "", data, bytes, sourceVertices);
}
void LightReconBspCapture::VertexHdrSidecar(const char *baseName, uint64_t sourceVertices)
{
	using namespace Rend2LightRecon::Core;
	if (!GeometryNeeded())
		return;
	char path[MAX_QPATH];
	Com_sprintf(path, sizeof(path), "maps/%s/vertlight.raw", baseName);
	LightmapSourceFile file;
	const int bytes = ri.FS_FOpenFileRead(path, &file.handle, qtrue);
	if (!file.handle || bytes < 0)
	{
		VertexHdrSource(path, nullptr, 0, sourceVertices);
		return;
	}
	if (static_cast<uint64_t>(bytes) != sourceVertices * 3 * sizeof(float))
	{
		s_surfaceCapture.VertexHdr(VertexHdrState::Present, path, nullptr,
								   static_cast<size_t>(bytes), sourceVertices);
		return;
	}
	if (static_cast<uint64_t>(bytes) >
		s_surfaceCapture.Limits().maxBytes - s_surfaceCapture.Report().bytes)
	{
		s_surfaceCapture.Fail(Code::MemoryLimit, "Vertex HDR sidecar exceeds capture budget");
		return;
	}
	try
	{
		std::vector<uint8_t> scratch(static_cast<size_t>(bytes));
		if (ri.FS_Read(scratch.data(), bytes, file.handle) != bytes)
			s_surfaceCapture.VertexHdr(VertexHdrState::ReadFailed, path, nullptr, 0,
									   sourceVertices);
		else
			s_surfaceCapture.VertexHdr(VertexHdrState::Present, path, scratch.data(),
									   scratch.size(), sourceVertices);
	}
	catch (const std::bad_alloc &)
	{
		s_surfaceCapture.Fail(Code::MemoryLimit, "Vertex HDR sidecar allocation failed");
	}
}
bool LightReconBspCapture::GeometryNeeded() const
{
	return active_ && s_surfaceCapture.Needed();
}
void LightReconBspCapture::GridLayout(const float *origin, const float *spacing, const int *bounds)
{
	using namespace Rend2LightRecon::Core;
	if (!active_ || !s_lightgridCapture.Needed()) return;
	if (bounds[0] < 0 || bounds[1] < 0 || bounds[2] < 0)
	{
		s_lightgridCapture.Fail(Code::InvalidInput, "Negative lightgrid bounds");
		return;
	}
	Rend2LightRecon::Core::GridLayout layout;
	layout.originWorld.x=origin[0]; layout.originWorld.y=origin[1]; layout.originWorld.z=origin[2];
	layout.spacingWorld.x=spacing[0]; layout.spacingWorld.y=spacing[1]; layout.spacingWorld.z=spacing[2];
	for (size_t i=0;i<3;++i) layout.bounds[i]=static_cast<uint32_t>(bounds[i]);
	s_lightgridCapture.SetLayout(layout);
}
void LightReconBspCapture::GridHdr(const char* path, const void* data, int bytes)
{
	using namespace Rend2LightRecon::Core;
	if (!active_ || !s_lightgridCapture.Needed()) return;
	if (bytes < 0 && data) { s_lightgridCapture.Fail(Code::InvalidInput, "Negative HDR grid size"); return; }
	s_lightgridCapture.SetHdr(data ? GridHdrState::Present : GridHdrState::Missing,
		path ? path : "", data, data ? static_cast<size_t>(bytes) : 0);
}
void LightReconBspCapture::GridHdrSidecar(const char* baseName)
{
	using namespace Rend2LightRecon::Core;
	if (!active_ || !s_lightgridCapture.Needed()) return;
	char path[MAX_QPATH];
	Com_sprintf(path, sizeof(path), "maps/%s/lightgrid.raw", baseName);
	LightmapSourceFile file;
	const int bytes = ri.FS_FOpenFileRead(path, &file.handle, qtrue);
	if (!file.handle || bytes < 0) { GridHdr(path, nullptr, 0); return; }
	if (static_cast<uint64_t>(bytes) > s_lightgridCapture.Limits().maxBytes - s_lightgridCapture.Report().bytes)
	{
		s_lightgridCapture.Fail(Code::MemoryLimit, "HDR grid sidecar exceeds capture budget");
		return;
	}
	try
	{
		std::vector<uint8_t> data(static_cast<size_t>(bytes));
		if (ri.FS_Read(data.data(), bytes, file.handle) != bytes)
		{
			s_lightgridCapture.Fail(Code::ProviderError, "Short HDR grid sidecar read");
			return;
		}
		// Empty existing files are Present (malformed), not missing.
		s_lightgridCapture.SetHdr(GridHdrState::Present, path, data.data(), data.size());
	}
	catch (const std::bad_alloc&) { s_lightgridCapture.Fail(Code::MemoryLimit, "HDR grid sidecar allocation failed"); }
}
void LightReconBspCapture::Embedded(int offset, int length, bool deluxe)
{
	if (!Needed()) return;
	if (offset < 0 || length < 0)
	{
		s_lightmapCapture.Fail(Rend2LightRecon::Core::Code::InvalidInput, "Negative lightmap lump bounds");
		return;
	}
	s_lightmapCapture.Embedded(source_, sourceBytes_, static_cast<uint64_t>(offset),
		static_cast<uint64_t>(length), 128, 128, deluxe);
}
void LightReconBspCapture::External(int index, bool deluxe, const char* path, int width, int height,
	const void* pixels, bool hdr, int pairedLightingIndex)
{
	using namespace Rend2LightRecon::Core;
	if (!Needed()) return;
	if (index < 0 || width <= 0 || height <= 0)
	{
		s_lightmapCapture.Fail(Code::InvalidInput, "Invalid external lightmap dimensions/index");
		return;
	}
	const uint64_t values = uint64_t(width) * uint64_t(height) * (hdr ? 3 : 4);
	if (values > SIZE_MAX)
	{
		s_lightmapCapture.Fail(Code::MemoryLimit, "External lightmap size overflow");
		return;
	}
	if (hdr)
		s_lightmapCapture.Rgb32F(static_cast<uint32_t>(index), path ? path : "", width, height,
			static_cast<const float*>(pixels), static_cast<size_t>(values));
	else
		s_lightmapCapture.Rgba8(static_cast<uint32_t>(index), deluxe ? LightmapRole::DeluxeDirection : LightmapRole::Lighting,
			path ? path : "", width, height, static_cast<const uint8_t*>(pixels), static_cast<size_t>(values),
			pairedLightingIndex >= 0 ? static_cast<uint32_t>(pairedLightingIndex) : UINT32_MAX);
}
void LightReconBspCapture::Missing(int index, bool deluxe, const char* path, bool hdr, int pairedLightingIndex)
{
	using namespace Rend2LightRecon::Core;
	if (!Needed()) return;
	if (index < 0) { s_lightmapCapture.Fail(Code::InvalidInput, "Negative missing lightmap index"); return; }
	s_lightmapCapture.Missing(static_cast<uint32_t>(index), deluxe ? LightmapRole::DeluxeDirection : LightmapRole::Lighting,
		hdr ? LightmapEncoding::ExternalRgb32F : LightmapEncoding::ExternalRgba8, path ? path : "",
		pairedLightingIndex >= 0 ? static_cast<uint32_t>(pairedLightingIndex) : UINT32_MAX);
}

namespace {
Rend2LightRecon::SunLighting::State s_sunCandidate, s_lastSceneSun;
uint64_t s_sunGeneration = 0;
bool s_sunResolved = false;
// Legacy mode 2 can sample the shadow block even in a scene that does not
// generate CSM. Preserve its pre-consumer values across a live canonical interval.
// Otherwise disabling a newly enabled sun leaves canonical matrices behind.
struct LegacySunShadowState {
    float sunShadowMvp[3][16], sunShadowSplits[3], sunShadowBlendWidths[2];
    float sunShadowTexelSize[3], sunShadowDepthSpan[3], sunShadowDepthTop;
} s_legacySunShadows;
bool s_legacySunShadowsSaved = false;
void R_CopyLegacySunShadowState(bool save)
{
#define SUN_SHADOW_MEMBER(member) \
    if (save) Com_Memcpy(&s_legacySunShadows.member, &tr.refdef.member, sizeof(tr.refdef.member)); \
    else Com_Memcpy(&tr.refdef.member, &s_legacySunShadows.member, sizeof(tr.refdef.member));
    SUN_SHADOW_MEMBER(sunShadowMvp)
    SUN_SHADOW_MEMBER(sunShadowSplits)
    SUN_SHADOW_MEMBER(sunShadowBlendWidths)
    SUN_SHADOW_MEMBER(sunShadowTexelSize)
    SUN_SHADOW_MEMBER(sunShadowDepthSpan)
    SUN_SHADOW_MEMBER(sunShadowDepthTop)
#undef SUN_SHADOW_MEMBER
}
const Rend2LightRecon::SunLighting::State& R_SunCandidate(const Rend2LightRecon::Snapshot& snapshot)
{
    if (!s_sunResolved || s_sunGeneration != snapshot.Generation()) {
        s_sunCandidate = Rend2LightRecon::SunLighting::Resolve(snapshot);
        s_sunGeneration = snapshot.Generation(); s_sunResolved = true;
    }
    return s_sunCandidate;
}
bool R_CanonicalSunFeature()
{
    // These existing latched controls provide the compiled physical/sun variants.
    // Probe contents have no renderer-bound P06 sun exclusion proof yet.
    return r_pbrPhysical->integer && r_sunlightMode->integer && r_depthPrepass->integer &&
        tr.sunShadowArrayImage && tr.sunShadowFbo[0] && tr.sunShadowFbo[1] && tr.sunShadowFbo[2] &&
        !r_cubeMapping->integer && !r_diffuseIBL->integer &&
        (r_volumetricFog->integer != 2 || !tr.world || tr.world->volumetricLightMaps[0]);
}
}
void R_ClearResolvedSun(void)
{
    s_sunCandidate = {}; s_lastSceneSun = {}; s_sunGeneration = 0; s_sunResolved = false;
    s_legacySunShadows = {}; s_legacySunShadowsSaved = false;
}
void R_ResolveSceneSun(const refdef_t *fd)
{
    using namespace Rend2LightRecon;
    using namespace SunLighting;
    PolicyInput input;
    input.master = r_lightReconUse && r_lightReconUse->integer;
    input.requested = r_lightReconSun && r_lightReconSun->integer;
    input.physical = r_pbrPhysical->integer != 0;
    input.adapterReady = R_CanonicalSunFeature();
    input.world = tr.world && !(fd->rdflags & RDF_NOWORLDMODEL);
    input.sceneOverride = (fd->rdflags & (RDF_EXTRA | RDF_SUNLIGHT)) == (RDF_EXTRA | RDF_SUNLIGHT);
    input.forceSun = r_forceSun->integer;
    // Explicit physical brightness authoring is also authoritative.
    if (!input.forceSun && r_pbrPhysical->integer && r_pbrSunIlluminance->value > 0) input.forceSun = 1;
    input.shaderSun = tr.sunParsed != 0;
    State candidate;
    if (input.master && input.requested && input.world && !input.sceneOverride && !input.forceSun) {
        const Snapshot snapshot = R_ReconstructedLighting();
        candidate = R_SunCandidate(snapshot);
        if (!R_LightReconConsumer(snapshot, Consumer::Sun).effective) candidate.canonical = false;
    }
    State state = Select(candidate, input);
    if (state.canonical) {
        Vec3 color;
        if (SceneIlluminance(state, R_PBRPhysicalToScene(), color)) {
            VectorSet(tr.refdef.sunDir, state.directionToSun.x, state.directionToSun.y, state.directionToSun.z);
            VectorSet(tr.refdef.sunCol, color.x, color.y, color.z);
            // sunAmbCol remains the historical baked-shadow floor, not physical sky.
        } else {
            State rejected = state; rejected.canonical = false; rejected.reason = Reason::ScaleMismatch;
            state = Select(rejected, input);
        }
    }
    if (!state.canonical) {
        // Retain the historical values byte-for-byte. Never label them lux.
        state.directionToSun = Vec3(tr.refdef.sunDir[0], tr.refdef.sunDir[1], tr.refdef.sunDir[2]);
        state.photonDirection = Vec3(-tr.refdef.sunDir[0], -tr.refdef.sunDir[1], -tr.refdef.sunDir[2]);
        state.illuminanceLux = {};
    }
    if (input.world) {
        if (state.canonical && !s_lastSceneSun.canonical) {
            R_CopyLegacySunShadowState(true); s_legacySunShadowsSaved = true;
        } else if (!state.canonical && s_lastSceneSun.canonical && s_legacySunShadowsSaved) {
            R_CopyLegacySunShadowState(false); s_legacySunShadowsSaved = false;
        }
        s_lastSceneSun = state;
    }
    tr.refdef.resolvedSun = state;
}
void R_ResolvedSunInfo(void)
{
    using namespace Rend2LightRecon::SunLighting;
    const auto& s = s_lastSceneSun;
    ri.Printf(PRINT_ALL, "resolved-sun canonical=%d policy=%u kind=%s reason=%s sun-query=%s source=%llu provenance=%llu generation=%llu override=%d\n",
        s.canonical, unsigned(s.policy), KindName(s.kind), ReasonName(s.reason), Rend2LightRecon::StatusName(s.status),
        (unsigned long long)s.source, (unsigned long long)s.provenance, (unsigned long long)s.generation, s.explicitOverride);
    ri.Printf(PRINT_ALL, "  rejected-candidate=%s\n", KindName(s.rejectedKind));
    ri.Printf(PRINT_ALL, "  to-sun %.6f %.6f %.6f lux-rgb %.6g %.6g %.6g confidence=%s%.4f radius=%s%.6g\n",
        s.directionToSun.x, s.directionToSun.y, s.directionToSun.z, s.illuminanceLux.x, s.illuminanceLux.y, s.illuminanceLux.z,
        s.confidenceKnown ? "" : "unknown/", s.confidence, s.angularRadiusKnown ? "" : "unknown/", s.angularRadius);
    ri.Printf(PRINT_ALL, "  feature physical=%d sun-mode=%d depth=%d shadow-cache=%d probes=%d fog-baseline=%d\n",
        r_pbrPhysical->integer, r_sunlightMode->integer, r_depthPrepass->integer, r_sunShadowCache->integer,
        (r_cubeMapping->integer || r_diffuseIBL->integer) ? 1 : 0,
        (r_volumetricFog->integer != 2 || (tr.world && tr.world->volumetricLightMaps[0])) ? 1 : 0);
    ri.Printf(PRINT_ALL, "  P06 additive world=%d entity=%d fog=%d; absent proof preserves baked baseline; probe composition unsupported\n",
        s.worldDiffuse, s.entityDiffuse, s.volumetricDirect);
}

Rend2LightRecon::ConsumerState R_LightReconConsumer(const Rend2LightRecon::Snapshot& snapshot, Rend2LightRecon::Consumer consumer)
{
	Rend2LightRecon::UseSettings settings;
	settings.master = r_lightReconUse && r_lightReconUse->integer != 0;
#define LIGHTRECON_CONTROL(name, maximum, description)
#define LIGHTRECON_CONSUMER(name, kind, flags, description) settings.requested[static_cast<size_t>(Rend2LightRecon::Consumer::kind)] = name && name->integer != 0;
#include "tr_lightrecon_cvars.inc"
#undef LIGHTRECON_CONSUMER
#undef LIGHTRECON_CONTROL
	// An existing fog/PBR/Forward+/LTC toggle alone is not an implementation of
	// canonical consumption. Future adapters must also check their real feature
	// prerequisites here, without setting any cvar or changing the shared asset.
	Rend2LightRecon::FeatureSupport feature;
    if (consumer == Rend2LightRecon::Consumer::Sun && settings.master &&
        settings.requested[static_cast<size_t>(consumer)])
        feature.available = R_CanonicalSunFeature() && R_SunCandidate(snapshot).canonical;
	if (consumer == Rend2LightRecon::Consumer::Entities && settings.master &&
		settings.requested[static_cast<size_t>(consumer)])
		feature.available = snapshot.GetCapability(Rend2LightRecon::Capability::ReflectedIrradianceSH).status ==
			Rend2LightRecon::Status::Ok;
    return Rend2LightRecon::EvaluateConsumer(snapshot, settings, consumer, feature);
}

void R_LightReconInfo_f(void)
{
	R_ResolvedSunInfo();
	RB_EntityIrradianceInfo();
	using namespace Rend2LightRecon;
	const Snapshot snapshot = R_ReconstructedLighting();
	const auto asset = snapshot.Asset();
	ri.Printf(PRINT_ALL, "lightRecon: build requested=%d (next map load/reload); use=%d\n",
		r_lightReconBuild ? r_lightReconBuild->integer : 0,
		r_lightReconUse ? r_lightReconUse->integer : 0);
	ri.Printf(PRINT_ALL, "  asset=%s valid=%d emitters=%d transport=%d (metadata counts, not usable lights)\n",
		AvailabilityName(snapshot.State()), asset ? 1 : 0, asset ? static_cast<int>(asset.value->emitters.size()) : 0,
		asset ? static_cast<int>(asset.value->transport.size()) : 0);
	const auto& report = s_reconstructedLighting.Report();
	const auto& geometry=s_surfaceCapture.Report();
	ri.Printf(PRINT_ALL,"  luxel-geometry=%s faces=%llu patches=%llu soups=%llu vertices=%llu indices=%llu bytes=%llu excluded=%llu malformed=%llu vertex-lit=%llu: %s\n",
		Core::CaptureStateName(geometry.state),static_cast<unsigned long long>(geometry.faces),static_cast<unsigned long long>(geometry.patches),static_cast<unsigned long long>(geometry.triangles),
		static_cast<unsigned long long>(geometry.vertices),static_cast<unsigned long long>(geometry.indices),static_cast<unsigned long long>(geometry.bytes),static_cast<unsigned long long>(geometry.excluded),static_cast<unsigned long long>(geometry.malformed),static_cast<unsigned long long>(geometry.vertexLit),geometry.message.c_str());
	const auto& lm = s_lightmapCapture.Report();
	ri.Printf(PRINT_ALL, "  observations=%s images=%llu missing=%llu RGB8=%llu RGBA8=%llu HDR32F=%llu deluxe=%llu pixels=%llu bytes=%llu accounted=%llu\n",
		Core::CaptureStateName(lm.state), static_cast<unsigned long long>(lm.images), static_cast<unsigned long long>(lm.missing),
		static_cast<unsigned long long>(lm.rgb8), static_cast<unsigned long long>(lm.rgba8), static_cast<unsigned long long>(lm.hdr),
		static_cast<unsigned long long>(lm.deluxe), static_cast<unsigned long long>(lm.pixels),
		static_cast<unsigned long long>(lm.pixelBytes), static_cast<unsigned long long>(lm.accountedBytes));
	const auto observations = R_LightmapObservations();
	if (observations)
	{
		ri.Printf(PRINT_ALL, "  observation-map=%s sha256=%s source-bytes=%llu (uncalibrated; no lighting capability)\n",
			observations->Map().path.c_str(), Disk::Hex(observations->Map().sha256).c_str(),
			static_cast<unsigned long long>(observations->Map().bytes));
		if (ri.Cmd_Argc() > 1 && !Q_stricmp(ri.Cmd_Argv(1), "images"))
			for (const auto& image : observations->Images())
				ri.Printf(PRINT_ALL, "  lm-observation index=%u role=%s encoding=%s size=%ux%ux%u missing=%d offset=%llu hash=%s source=%s\n",
					image.logicalIndex, image.role == Core::LightmapRole::Lighting ? "lightmap" : "deluxe",
					Core::LightmapEncodingName(image.encoding), image.width, image.height, image.channels, image.missing ? 1 : 0,
					static_cast<unsigned long long>(image.sourceOffset), Disk::Hex(image.pixelHash).c_str(), image.source.c_str());
	}
	if (lm.state == Core::CaptureState::Failed) ri.Printf(PRINT_ALL, "  observation-error: %s\n", lm.message.c_str());
	const auto& grid = s_lightgridCapture.Report();
	ri.Printf(PRINT_ALL, "  grid-observations=%s cells=%llu records=%llu indices=%llu HDR=%llu bytes=%llu missing=%llu invalid-indices=%llu no-style=%llu style-gaps=%llu nonfinite-HDR=%llu negative-HDR=%llu issues=%u (geometry unknown)\n",
		Core::CaptureStateName(grid.state), static_cast<unsigned long long>(grid.cells),
		static_cast<unsigned long long>(grid.records), static_cast<unsigned long long>(grid.indices),
		static_cast<unsigned long long>(grid.hdrCells), static_cast<unsigned long long>(grid.bytes),
		static_cast<unsigned long long>(grid.missingCells), static_cast<unsigned long long>(grid.invalidIndices),
		static_cast<unsigned long long>(grid.noStyle), static_cast<unsigned long long>(grid.styleGaps),
		static_cast<unsigned long long>(grid.nonFiniteHdr), static_cast<unsigned long long>(grid.negativeHdr), grid.issues);
	const auto rawGrid = R_LightgridObservations();
	if (rawGrid)
	{
		const auto& layout = rawGrid->Layout();
		ri.Printf(PRINT_ALL, "  grid-layout bounds=%u,%u,%u origin=%.9g,%.9g,%.9g spacing=%.9g,%.9g,%.9g HDR-state=%d source=%s\n",
			layout.bounds[0], layout.bounds[1], layout.bounds[2],
			layout.originWorld.x, layout.originWorld.y, layout.originWorld.z,
			layout.spacingWorld.x, layout.spacingWorld.y, layout.spacingWorld.z,
			static_cast<int>(rawGrid->HdrState()), rawGrid->HdrSource().c_str());
		ri.Printf(PRINT_ALL, "  grid-map=%s sha256=%s records-sha256=%s indices-sha256=%s HDR-sha256=%s\n",
			rawGrid->Map().path.c_str(), Disk::Hex(rawGrid->Map().sha256).c_str(),
			Disk::Hex(Disk::Hash(rawGrid->Records().data(), rawGrid->Records().size())).c_str(),
			Disk::Hex(Disk::Hash(rawGrid->Indices().data(), rawGrid->Indices().size())).c_str(),
			Disk::Hex(Disk::Hash(rawGrid->Hdr().data(), rawGrid->Hdr().size()*sizeof(float))).c_str());
	}
	if (grid.state == Core::CaptureState::Failed) ri.Printf(PRINT_ALL, "  grid-observation-error: %s\n", grid.message.c_str());
	ri.Printf(PRINT_ALL, "  lifecycle=%s world=%s ticket=%llu generation=%llu identity=%s\n",
		WorldStateName(report.state), report.world.empty() ? "<none>" : report.world.c_str(),
		static_cast<unsigned long long>(report.ticket), static_cast<unsigned long long>(snapshot.Generation()),
		report.assetIdentity.empty() ? "<none>" : report.assetIdentity.c_str());
	ri.Printf(PRINT_ALL, "  cache=%s key-complete=%d key=%s write=%s; metadata-estimate=%llu temporary=%s\n",
		CacheCodeName(report.cacheStatus), report.keyComplete ? 1 : 0, report.cacheKey.empty() ? "<none>" : report.cacheKey.c_str(),
		CacheCodeName(report.writeStatus), static_cast<unsigned long long>(report.assetBytes),
		report.transientBytesKnown ? "0 bytes" : "host/provider scratch (not measured)");
	if (!report.world.empty())
		ri.Printf(PRINT_ALL, "  build applied=%d result=%s cache-called=%d build-called=%d: %s\n",
			static_cast<int>(report.policy), StatusName(report.result),
			report.cacheCalled ? 1 : 0, report.buildCalled ? 1 : 0, report.message.c_str());
	else ri.Printf(PRINT_ALL, "  build policy has not been applied to a loaded world\n");
	ri.Printf(PRINT_ALL, "  world-ready=%d area-ready=%d surfaces=%llu grid=%llu lightmap-textures=%llu session=%d steps=%llu\n",
		report.facts.geometryReady ? 1 : 0, report.facts.areaLightsReady ? 1 : 0,
		static_cast<unsigned long long>(report.facts.surfaces), static_cast<unsigned long long>(report.facts.gridSamples),
		static_cast<unsigned long long>(report.facts.lightmapTextures), report.sessionPrepared ? 1 : 0,
		static_cast<unsigned long long>(report.steps));
	ri.Printf(PRINT_ALL, "  cache-preflight early=%d pending=%d dependencies=%llu missing=%llu hashed=%llu dependency-seconds=%.6f cache-read=%llu cache-reads=%llu selected-chunks=%llu selected-bytes=%llu open-seconds=%.6f prepare-seconds=%.6f\n",
		report.earlyPreflight?1:0,report.pendingCacheHit?1:0,
		static_cast<unsigned long long>(s_dependencyWork.files),static_cast<unsigned long long>(s_dependencyWork.missing),
		static_cast<unsigned long long>(s_dependencyWork.hashedBytes),s_dependencyWork.seconds,
		static_cast<unsigned long long>(report.cacheWork.readBytes),static_cast<unsigned long long>(report.cacheWork.reads),
		static_cast<unsigned long long>(report.cacheWork.chunks),static_cast<unsigned long long>(report.cacheWork.payloadBytes),
		report.cacheWork.openSeconds,report.cacheWork.prepareSeconds);
	ri.Printf(PRINT_ALL,"  first-publication-seconds=%.6f available=%d\n",report.secondsToPublication,
		report.state==WorldState::Available?1:0);
	if(ri.Cmd_Argc()>1 && !Q_stricmp(ri.Cmd_Argv(1),"dependencies") && report.keyComplete) {
		const auto &f=s_reconstructedLighting.InputFingerprint();
		ri.Printf(PRINT_ALL,"dependency-manifest algorithm=%s version=%u world-units=%.9g physical-to-scene=%.9g key=%s\n",
			f.algorithm.c_str(),f.algorithmVersion,double(f.units.worldUnitsPerMeter),double(f.units.physicalToScene),report.cacheKey.c_str());
		for(const auto &input:f.inputs)ri.Printf(PRINT_ALL,"dependency kind=%u bytes=%llu sha256=%s name=%s\n",
			unsigned(input.kind),static_cast<unsigned long long>(input.bytes),Disk::Hex(input.sha256).c_str(),input.name.c_str());
	}
	for (size_t i = 0; i < kConsumerCount; ++i)
	{
		const Consumer c = static_cast<Consumer>(i);
		const ConsumerState state = R_LightReconConsumer(snapshot, c);
		ri.Printf(PRINT_ALL, "  %s requested=%d effective=%d capability=%s feature=%d: %s\n", ConsumerCvar(c),
			state.requested ? 1 : 0, state.effective ? 1 : 0, StatusName(state.capabilityStatus),
			state.featureAvailable ? 1 : 0, GateReasonName(state.reason));
	}
}

void R_LuxelDebug_f(void)
{
	using namespace Rend2LightRecon;
	using namespace Rend2LightRecon::Core;
	if (ri.Cmd_Argc() < 2 || !Q_stricmp(ri.Cmd_Argv(1), "off"))
	{
		s_luxelDebug.clear();
		ri.Printf(PRINT_ALL, "luxel-debug off; usage: r_lightReconLuxels list | [logical] <image "
							 "ID> <stride 1..64> [surface ordinal]\n");
		return;
	}
	const auto geometry = R_LuxelGeometry();
	const auto images = R_LightmapObservations();
	if (!geometry || !images)
	{
		ri.Printf(PRINT_ALL, "luxel-debug unavailable: reload map with r_lightReconBuild 2 or 3\n");
		return;
	}
	if (!Q_stricmp(ri.Cmd_Argv(1), "list"))
	{
		for (const auto &s : geometry->Surfaces())
			if (s.kind == BakedSurfaceKind::Patch && s.motion == SurfaceMotion::StaticWorld &&
				s.lightmap[0] >= 0 && !s.vertices.empty())
			{
				const auto &v = s.vertices[s.vertices.size() / 2];
				ri.Printf(
					PRINT_ALL,
					"luxel-patch surface=%u lightmap=%d position=%.3f,%.3f,%.3f material=%s\n",
					s.ordinal, s.lightmap[0], v.position.x, v.position.y, v.position.z,
					s.material.c_str());
			}
		for (size_t i = 0; i < images->Images().size(); ++i)
		{
			const auto &im = images->Images()[i];
			ri.Printf(PRINT_ALL, "luxel-image ordinal=%u logical=%u size=%ux%u source=%s\n",
					  static_cast<unsigned>(i), im.logicalIndex, im.width, im.height,
					  im.source.c_str());
		}
		return;
	}
	const bool logical = !Q_stricmp(ri.Cmd_Argv(1), "logical");
	int first = logical ? 2 : 1;
	int image = atoi(ri.Cmd_Argv(first)),
		stride = ri.Cmd_Argc() > first + 1 ? atoi(ri.Cmd_Argv(first + 1)) : 4;
	s_luxelDebug.clear(); // failed new requests cannot leave a previous image's overlay visible
	int surface = ri.Cmd_Argc() > first + 2 ? atoi(ri.Cmd_Argv(first + 2)) : -1;
	if (logical)
	{
		int requested = image;
		image = -1;
		for (size_t i = 0; i < images->Images().size(); ++i)
		{
			const auto &im = images->Images()[i];
			if (requested >= 0 && im.logicalIndex == static_cast<unsigned>(requested) &&
				im.role == LightmapRole::Lighting && !im.missing)
			{
				image = static_cast<int>(i);
				break;
			}
		}
	}
	if (image < 0 || static_cast<size_t>(image) >= images->Images().size() || stride < 1 ||
		stride > 64)
	{
		ri.Printf(PRINT_ALL, "luxel-debug invalid image/stride\n");
		return;
	}
	const auto &im = images->Images()[image];
	MappingOptions options;
	ReadContext context;
	context.maxBytes = 32ull * 1024 * 1024;
	LuxelTile tile;
	const auto error =
		MapLuxels(geometry, images, image, 0, 0, im.width, im.height, options, context, tile);
	if (!error)
	{
		ri.Printf(PRINT_ALL, "luxel-debug failed: %s\n", error.message.c_str());
		return;
	}
	s_luxelDebug.clear();
	std::string mask =
		"P6\n" + std::to_string(tile.width) + " " + std::to_string(tile.height) + "\n255\n";
	for (const auto &s : tile.samples)
	{
		byte rgb[3] = {20, 20, 20};
		if (s.coverage == LuxelCoverage::Mapped)
		{
			rgb[0] = 40;
			rgb[1] = 220;
			rgb[2] = 80;
		}
		if (s.issues & LuxelBorder)
		{
			rgb[0] = 255;
			rgb[1] = 180;
			rgb[2] = 20;
		}
		if (s.coverage == LuxelCoverage::FootprintOnly)
		{
			rgb[0] = 80;
			rgb[1] = 100;
			rgb[2] = 255;
		}
		if (s.coverage == LuxelCoverage::Ambiguous)
		{
			rgb[0] = 255;
			rgb[1] = 20;
			rgb[2] = 180;
		}
		if (s.coverage == LuxelCoverage::Unsupported)
		{
			rgb[0] = 255;
			rgb[1] = 30;
			rgb[2] = 30;
		}
		mask.append(reinterpret_cast<const char *>(rgb), 3);
		if (s.positionKnown && s.x % stride == 0 && s.y % stride == 0 &&
			(surface < 0 || s.surface == static_cast<unsigned>(surface)))
			s_luxelDebug.push_back(s);
	}
	auto distance = [](const SurfaceLightingObservation &s) {
		double x = s.positionWorld.x - tr.refdef.vieworg[0],
			   y = s.positionWorld.y - tr.refdef.vieworg[1],
			   z = s.positionWorld.z - tr.refdef.vieworg[2];
		return x * x + y * y + z * z;
	};
	std::stable_sort(s_luxelDebug.begin(), s_luxelDebug.end(),
					 [&](const SurfaceLightingObservation &a, const SurfaceLightingObservation &b) {
						 return distance(a) < distance(b);
					 });
	if (s_luxelDebug.size() > 512)
		s_luxelDebug.resize(512);
	const std::string stem = "lightrecon/luxel-image-" + std::to_string(image);
	ri.FS_WriteFile((stem + ".ppm").c_str(), mask.data(), static_cast<int>(mask.size()));
	std::string obj;
	if (LuxelDebugObj(tile, options.maxTexels, obj))
		ri.FS_WriteFile((stem + ".obj").c_str(), obj.data(), static_cast<int>(obj.size()));
	const auto &r = tile.report;
	ri.Printf(PRINT_ALL,
			  "luxel-debug image=%d logical=%u mapped=%llu padding=%llu footprint=%llu "
			  "ambiguous=%llu borders=%llu duplicate-triangles=%llu degenerate=%llu "
			  "patch-segments=%llu unsupported-patch-UV=%llu excluded=%llu basis-max=%.9g "
			  "markers=%u unsupported-texels=%llu; wrote %s.obj and %s.ppm\n",
			  image, im.logicalIndex, static_cast<unsigned long long>(r.mapped),
			  static_cast<unsigned long long>(r.padding),
			  static_cast<unsigned long long>(r.footprintOnly),
			  static_cast<unsigned long long>(r.ambiguous),
			  static_cast<unsigned long long>(r.borders),
			  static_cast<unsigned long long>(r.duplicates),
			  static_cast<unsigned long long>(r.degenerate),
			  static_cast<unsigned long long>(r.patchSegments),
			  static_cast<unsigned long long>(r.unsupportedPatchUv),
			  static_cast<unsigned long long>(r.excluded), r.maximumBasisError,
			  static_cast<unsigned>(s_luxelDebug.size()),
			  static_cast<unsigned long long>(r.unsupportedTexels), stem.c_str(), stem.c_str());
}

void R_LightReconObservations_f(void)
{
	using namespace Rend2LightRecon::Core;
	const auto geometry = R_LuxelGeometry();
	const auto images = R_LightmapObservations();
	if (!geometry || !images)
	{
		ri.Printf(PRINT_ALL,
				  "baked-summary unavailable: reload map with r_lightReconBuild 2 or 3\n");
		return;
	}
	if (ri.Cmd_Argc() > 1 && !Q_stricmp(ri.Cmd_Argv(1), "surface"))
	{
		const int ordinal = ri.Cmd_Argc() > 2 ? atoi(ri.Cmd_Argv(2)) : -1;
		if (ordinal < 0 || static_cast<size_t>(ordinal) >= geometry->Surfaces().size())
		{
			ri.Printf(PRINT_ALL, "baked-summary invalid surface ordinal\n");
			return;
		}
		const auto &s = geometry->Surfaces()[ordinal];
		const auto &m = s.materialFacts;
		ri.Printf(PRINT_ALL,
				  "baked-material surface=%u shader=%s signals=%u uncertainty=%u base=%s "
				  "base-pixels=%d emission=%s authored-nits=%.9g legacy-surfacelight=%.9g\n",
				  s.ordinal, s.material.c_str(), m.signals, m.uncertainty, m.baseTexture.c_str(),
				  m.baseColourKnown ? 1 : 0, m.emissionTexture.c_str(), m.authoredLuminance,
				  m.legacySurfaceLight);
		for (uint32_t slot = 0; slot < 4; ++slot)
			for (bool vertex : {false, true})
			{
				const auto c = ClassifySurface(s, slot, vertex);
				ri.Printf(PRINT_ALL,
						  "  channel=%s slot=%u style=%u path=%s fit=%d supplementary=%d "
						  "emitter-hint=%d reasons:",
						  vertex ? "vertex" : "lightmap", slot, unsigned(c.style.id),
						  ObservationPathName(c.path), c.diffuseFitCandidate ? 1 : 0,
						  c.supplementaryEvidence ? 1 : 0, c.emitterHint ? 1 : 0);
				for (size_t bit = 0; bit < kObservationReasonCount; ++bit)
					if (c.reasons & (1ull << bit))
						ri.Printf(PRINT_ALL, " %s", ObservationReasonName(1ull << bit));
				ri.Printf(PRINT_ALL, "\n");
			}
		return;
	}
	if (ri.Cmd_Argc() > 1 && !Q_stricmp(ri.Cmd_Argv(1), "direction"))
	{
		const int logical = ri.Cmd_Argc() > 2 ? atoi(ri.Cmd_Argv(2)) : -1;
		const int x = ri.Cmd_Argc() > 3 ? atoi(ri.Cmd_Argv(3)) : -1,
				  y = ri.Cmd_Argc() > 4 ? atoi(ri.Cmd_Argv(4)) : -1;
		for (size_t image = 0; image < images->Images().size(); ++image)
		{
			const auto &im = images->Images()[image];
			if (logical < 0 || x < 0 || y < 0 ||
				im.logicalIndex != static_cast<unsigned>(logical) || im.missing ||
				im.role != LightmapRole::Lighting)
				continue;
			DeluxeBinding binding;
			DeluxeSample sample;
			ReadContext ctx;
			ctx.maxBytes = 16;
			auto error = BindDeluxe(images, image, binding);
			if (error)
				error = ReadDeluxe(binding, static_cast<uint32_t>(x), static_cast<uint32_t>(y), ctx,
								   sample);
			if (!error)
			{
				ri.Printf(PRINT_ALL, "baked-direction failed: %s\n", error.message.c_str());
				return;
			}
			const auto &d = sample.direction;
			ri.Printf(PRINT_ALL,
					  "baked-direction logical=%d x=%d y=%d state=%s raw=%u,%u,%u "
					  "to-source=%.9g,%.9g,%.9g angle-bound=%.9g (supplementary WORLD evidence)\n",
					  logical, x, y, DeluxeStateName(d.state), unsigned(d.raw[0]),
					  unsigned(d.raw[1]), unsigned(d.raw[2]), d.toSource.x, d.toSource.y,
					  d.toSource.z, d.angularBoundRadians);
			return;
		}
		ri.Printf(PRINT_ALL, "baked-direction invalid/missing colour image\n");
		return;
	}
	ClassificationOptions options;
	options.scanLuxels = ri.Cmd_Argc() > 1 && !Q_stricmp(ri.Cmd_Argv(1), "scan");
	ReadContext context;
	context.maxBytes = 32ull * 1024 * 1024;
	ObservationSummary s;
	const auto error = SummarizeObservations(geometry, images, options, context, s);
	if (!error)
	{
		ri.Printf(PRINT_ALL, "baked-summary failed: %s\n", error.message.c_str());
		return;
	}
	auto n = [](uint64_t value) { return static_cast<unsigned long long>(value); };
	ri.Printf(PRINT_ALL,
			  "baked-summary surfaces=%llu charts=%llu static-charts=%llu styled-charts=%llu "
			  "vertex-samples=%llu images=%llu alternatives=%llu missing-images=%llu "
			  "paired-deluxe=%llu unavailable-deluxe=%llu\n",
			  n(s.surfaces), n(s.lightmappedCharts), n(s.staticCharts), n(s.styledCharts),
			  n(s.vertexSamples), n(s.selectedLightingImages), n(s.alternativeImages),
			  n(s.missingImages), n(s.pairedDeluxeImages), n(s.unavailableDeluxeImages));
	ri.Printf(PRINT_ALL,
			  "baked-categories emissive=%llu sky=%llu cutout=%llu translucent=%llu unlit=%llu "
			  "moving=%llu animated=%llu excluded-surfaces=%llu\n",
			  n(s.emissiveSurfaces), n(s.skySurfaces), n(s.cutoutSurfaces),
			  n(s.translucentSurfaces), n(s.unlitSurfaces), n(s.movingSurfaces),
			  n(s.animatedSurfaces), n(s.excludedSurfaces));
	if (s.luxelsScanned)
		ri.Printf(PRINT_ALL,
				  "baked-luxels texels=%llu valid=%llu fit-candidates=%llu styled=%llu "
				  "excluded=%llu deluxe-valid=%llu deluxe-unavailable=%llu\n",
				  n(s.texels), n(s.validLuxels), n(s.fitCandidates), n(s.styledLuxels),
				  n(s.excludedLuxels), n(s.deluxeAvailable), n(s.deluxeUnavailable));
	else
		ri.Printf(PRINT_ALL, "baked-luxels not scanned; use r_lightReconObservations scan for "
							 "bounded CPU coverage/direction statistics\n");
	for (unsigned style = 0; style < 255; ++style)
		if (s.styles[style] || s.vertexStyles[style])
			ri.Printf(PRINT_ALL, "baked-style id=%u charts=%llu vertex-channels=%llu status=%s\n",
					  style, n(s.styles[style]), n(s.vertexStyles[style]),
					  style ? "styled-animation-unknown" : "static");
	for (size_t bit = 0; bit < kObservationReasonCount; ++bit)
		if (s.exclusionReasons[bit])
			ri.Printf(PRINT_ALL, "baked-exclusion reason=%s texels=%llu\n",
					  ObservationReasonName(1ull << bit), n(s.exclusionReasons[bit]));
	ri.Printf(
		PRINT_ALL,
		"baked-summary map=%s source=%s geometry-bytes=%llu vertex-HDR-state=%d "
		"vertex-HDR-source=%s (relative evidence; no measured albedo or absolute lighting scale)\n",
		geometry->Map().path.c_str(), Rend2LightRecon::Disk::Hex(geometry->Map().sha256).c_str(),
		n(geometry->Report().bytes), static_cast<int>(geometry->HdrState()),
		geometry->HdrSource().c_str());
}

// Explicit data-only marker import. Never reads an asset automatically, fits a
// source, or writes runtime lighting state. Requires the captured original BSP hash.
void R_LightReconPreview_f(void)
{
    s_reconstructionPreview.clear();
    std::string name=ri.Cmd_Argc()==2 ? ri.Cmd_Argv(1) : "";
    if(name=="off" || name.empty()) { ri.Printf(PRINT_ALL,"reconstruction-preview off; usage: r_lightReconPreview <safe-name>|off\n");return; }
    if(name.size()>64 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=std::string::npos) {
        ri.Printf(PRINT_ALL,"reconstruction-preview invalid name\n");return;
    }
    auto geometry=R_LuxelGeometry();
    if(!geometry) { ri.Printf(PRINT_ALL,"reconstruction-preview requires explicit r_lightReconBuild 2/3 source capture; no fitting occurs\n");return; }
    LightmapSourceFile file;
    std::string path="lightrecon-preview/"+name+".txt";
    int bytes=ri.FS_FOpenFileRead(path.c_str(),&file.handle,qtrue);
    if(!file.handle || bytes<=0 || bytes>128*1024) {ri.Printf(PRINT_ALL,"reconstruction-preview missing/oversized file\n");return;}
    std::string text(size_t(bytes),'\0');ri.FS_Read(&text[0],bytes,file.handle);
    std::istringstream in(text);std::string magic,hash;unsigned count=0;
    in>>magic>>hash>>count;
    bool valid=bool(in) && magic=="JKRECONPREVIEW1" && hash==Rend2LightRecon::Disk::Hex(geometry->Map().sha256) && count<=256;
    std::vector<ReconstructionPreviewMarker> markers;
    for(unsigned i=0;valid && i<count;++i) {
        ReconstructionPreviewMarker m;
        in>>m.id>>m.kind>>m.priorOnly;
        for(auto v:{m.position,m.right,m.up,m.forward})for(unsigned j=0;j<3;++j)in>>v[j];
        in>>m.width>>m.height>>m.length>>m.radius>>m.confidenceKnown>>m.confidence;
        valid=bool(in) && m.id && m.kind<=4 && m.priorOnly<=1 && m.confidenceKnown<=1 &&
            std::isfinite(m.confidence) && m.confidence>=0 && m.confidence<=1;
        for(auto v:{m.position,m.right,m.up,m.forward})for(unsigned j=0;j<3;++j)valid=valid && std::isfinite(v[j]) && std::abs(v[j])<1e7;
        for(float v:{m.width,m.height,m.length,m.radius})valid=valid && std::isfinite(v) && v>=0 && v<1e5;
        for(auto v:{m.right,m.up,m.forward})valid=valid && std::abs(VectorLengthSquared(v)-1)<.01f;
        valid=valid && std::abs(DotProduct(m.right,m.up))<.01f && std::abs(DotProduct(m.right,m.forward))<.01f && std::abs(DotProduct(m.up,m.forward))<.01f;
        if(valid) markers.push_back(m);
    }
    std::string extra;valid=valid && !(in>>extra);
    if(!valid) {ri.Printf(PRINT_ALL,"reconstruction-preview rejected: map hash/finite frame/schema/data mismatch\n");return;}
    s_reconstructionPreview.swap(markers);
    ri.Printf(PRINT_ALL,"reconstruction-preview %u DATA ONLY markers; orange=conditional fitted hypothesis, magenta=UNFITTED prior; no lights\n",unsigned(s_reconstructionPreview.size()));
    for(const auto &m:s_reconstructionPreview)
        ri.Printf(PRINT_ALL,"preview id=%llu kind=%u position=%.2f %.2f %.2f priorOnly=%u existence=%s %.3f\n",m.id,m.kind,m.position[0],m.position[1],m.position[2],m.priorOnly,m.confidenceKnown?"specified":"Unknown",m.confidence);
}
namespace {
bool DiagnosticName(const std::string &s)
{
	return !s.empty() && s.size() <= 64 &&
		   s.find_first_not_of(
			   "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") ==
			   std::string::npos;
}
bool DiagnosticId(const char *text, Rend2LightRecon::Id &id)
{
	if (!text || !*text || std::strspn(text, "0123456789") != std::strlen(text))
		return false;
	char *end = nullptr;
	errno = 0;
	auto value = std::strtoull(text, &end, 10);
	if (errno || !end || *end)
		return false;
	id = value;
	return true;
}
bool DiagnosticPrepare()
{
	using namespace Rend2LightRecon::Core;
	auto geometry = R_LuxelGeometry();
	if (!geometry || !s_offlineDomain)
	{
		ri.Printf(PRINT_ALL, "lighting-debug geometry Unavailable: reload with explicit "
							 "r_lightReconBuild 2/3, consumers OFF; no automatic build\n");
		return false;
	}
	if (s_diagnosticRays && s_diagnosticRays->BuiltFrom(geometry))
		return true;
	VisibilityBuildOptions options;
	options.maxBytes = 128ull << 20;
	// Explicit geometry-inspection accuracy, matching the original offline
	// capture policy. Curved chord envelopes remain Unknown in exact ray queries.
	// This changes no solver or canonical visibility tolerance.
	options.patchToleranceWorld = 8;
	ReadContext context;
	context.maxBytes = 128ull << 20;
	auto start = std::chrono::steady_clock::now();
	auto e = StaticVisibility::Build(geometry, options, context, s_diagnosticRays, s_offlineDomain);
	if (!e)
	{
		ri.Printf(PRINT_ALL, "lighting-debug geometry preparation failed: %s\n", e.message.c_str());
		return false;
	}
	const auto &r = s_diagnosticRays->Report();
	ri.Printf(PRINT_ALL,
			  "lighting-debug explicit BVH build triangles=%llu bytes=%llu seconds=%.6f "
			  "patch-error-bound-world=8; static optical scope, unknown movers remain separate\n",
			  static_cast<unsigned long long>(r.triangles),
			  static_cast<unsigned long long>(r.ownedBytes),
			  std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count());
	return true;
}
} // namespace
void R_LightReconDebug_f(void)
{
	using namespace Rend2LightRecon;
	const std::string command = ri.Cmd_Argc() > 1 ? ri.Cmd_Argv(1) : "info";
	if (command == "off")
	{
		s_diagnosticScene.reset();
		s_diagnosticSelection = {};
		s_diagnosticTicket = 0;
		s_diagnosticRays.reset();
		ri.Printf(PRINT_ALL, "lighting-debug off; resident diagnostic buffers released\n");
		return;
	}
	if (command == "prepare")
	{
		DiagnosticPrepare();
		return;
	}
	if (command == "geometry")
	{
		using namespace Core;
		if (!DiagnosticPrepare())
			return;
		Vec3 eye(tr.refdef.vieworg[0], tr.refdef.vieworg[1], tr.refdef.vieworg[2]);
		Vec3 forward(tr.refdef.viewaxis[0][0], tr.refdef.viewaxis[0][1], tr.refdef.viewaxis[0][2]);
		ReadContext context;
		context.maxBytes = 16ull << 20;
		auto scene = std::make_shared<LRDebug::Scene>();
		scene->map = s_diagnosticRays->GeometryIdentity().sha256;
		scene->mapPath = s_reconstructedLighting.Report().world;
		scene->algorithm = "V03/original-geometry-reference/1";
		scene->scope = "Original camera-ray hit and geometric normal; NOT recovered sources or "
					   "proof of bake-time mover absence";
		LRDebug::Builder builder(*scene);
		size_t attempts = 0, accepted = 0;
		std::set<Id> selected;
		for (float horizontal : {0.f, -.25f, .25f})
			for (float vertical : {0.f, -.15f, .15f})
			{
				if (accepted >= 4)
					continue;
				++attempts;
				Vec3 dir(forward.x + horizontal * tr.refdef.viewaxis[1][0] +
							 vertical * tr.refdef.viewaxis[2][0],
						 forward.y + horizontal * tr.refdef.viewaxis[1][1] +
							 vertical * tr.refdef.viewaxis[2][1],
						 forward.z + horizontal * tr.refdef.viewaxis[1][2] +
							 vertical * tr.refdef.viewaxis[2][2]);
				RayQuery query;
				query.startWorld = eye;
				query.endWorld =
					Vec3(eye.x + 2048 * dir.x, eye.y + 2048 * dir.y, eye.z + 2048 * dir.z);
				query.maxWork = 100000;
				RayResult ray;
				auto e = s_diagnosticRays->Query(query, context, ray);
				if (!e || !ray.firstHit.positionKnown ||
					!selected.insert(ray.firstHit.triangle).second)
					continue;
				const auto &h = ray.firstHit;
				Vec3 p = h.positionWorld, n = h.geometricNormal;
				LRDebug::Record r;
				r.id = h.triangle;
				r.sample = h.surface;
				r.layer = LRDebug::Layer::Receivers;
				r.state = LRDebug::State::Conditional;
				r.geometryKnown = true;
				r.a = p;
				r.domain = "original-world-geometry";
				r.quantity = "geometry-only-no-light";
				r.reason = "Original ray intersection; NOT a recovered lamp or source visibility "
						   "certificate";
				r.provenance = "BSP source surface=" + std::to_string(h.surface) +
							   " primitive=" + std::to_string(h.triangle);
				r.issues = ray.issues;
				builder.Add(r);
				r.primitive = LRDebug::Primitive::Line;
				r.b = Vec3(p.x + 24 * n.x, p.y + 24 * n.y, p.z + 24 * n.z);
				r.reason = "Geometric surface normal; receiver-to-light is separate";
				builder.Add(r);
				Bounds bounds;
				bounds.known = true;
				bounds.minWorld = Vec3(p.x - 1, p.y - 1, p.z - 1);
				bounds.maxWorld = Vec3(p.x + 1, p.y + 1, p.z + 1);
				LocalGeometry local;
				e = s_diagnosticRays->ExtractBox(bounds, context, 4096, 100000, local);
				if (e)
					for (auto &t : local.triangles)
						if (t.primitive == h.triangle)
						{
							r.primitive = LRDebug::Primitive::Triangle;
							r.a = t.geometry.verticesWorld[0];
							r.b = t.geometry.verticesWorld[1];
							r.c = t.geometry.verticesWorld[2];
							r.reason = "Original source triangle winding";
							builder.Add(r);
							break;
						}
				builder.Ray(h.triangle, 0, eye, query.endWorld, ray,
							"Static optical reference; historical mover occupancy not certified");
				ri.Printf(PRINT_ALL,
						  "lighting-debug geometry-marker surface=%llu primitive=%llu xyz=%.4f "
						  "%.4f %.4f normal=%.6f %.6f %.6f camera=%.4f %.4f %.4f forward=%.6f %.6f "
						  "%.6f ray-state=%u issues=%u\n",
						  static_cast<unsigned long long>(h.surface),
						  static_cast<unsigned long long>(h.triangle), p.x, p.y, p.z, n.x, n.y, n.z,
						  eye.x, eye.y, eye.z, forward.x, forward.y, forward.z, unsigned(ray.state),
						  ray.issues);
				++accepted;
			}
		for (const auto &d : s_offlineDynamic)
			builder.Dynamic(d);
		s_diagnosticScene = scene;
		s_diagnosticTicket = s_reconstructedLighting.Report().ticket;
		s_diagnosticSelection = {};
		s_diagnosticSelection.layers = 1u << unsigned(LRDebug::Layer::Receivers);
		ri.Printf(PRINT_ALL,
				  "lighting-debug geometry selected=%u attempted=%u map=%s; display only, no "
				  "recovered light\n",
				  unsigned(accepted), unsigned(attempts), Disk::Hex(scene->map).c_str());
		return;
	}
	if (command == "ray" && ri.Cmd_Argc() == 4)
	{
		using namespace Core;
		Id source = 0, sample = 0;
		if (!DiagnosticId(ri.Cmd_Argv(2), source) || !DiagnosticId(ri.Cmd_Argv(3), sample) ||
			!source || !sample)
			return;
		if (!s_diagnosticScene || !DiagnosticPrepare())
		{
			ri.Printf(PRINT_ALL, "lighting-debug ray Unavailable\n");
			return;
		}
		const LRDebug::Record *a = nullptr, *b = nullptr;
		for (auto &r : s_diagnosticScene->records)
			if (r.geometryKnown)
			{
				if (r.source == source && r.layer == LRDebug::Layer::Sources && !a)
					a = &r;
				if (r.sample == sample && r.layer == LRDebug::Layer::Receivers && !b)
					b = &r;
			}
		if (!a || !b)
		{
			ri.Printf(PRINT_ALL, "lighting-debug exact IDs not found; Unknown\n");
			return;
		}
		RayQuery query;
		query.startWorld = a->a;
		query.endWorld = b->a;
		query.maxWork = 100000;
		RayResult ray;
		ReadContext context;
		auto e = s_diagnosticRays->Query(query, context, ray);
		if (!e)
		{
			ri.Printf(PRINT_ALL, "lighting-debug ray failed %s\n", e.message.c_str());
			return;
		}
		auto scene = std::make_shared<LRDebug::Scene>(*s_diagnosticScene);
		LRDebug::Builder builder(*scene);
		builder.Ray(sample, source, a->a, b->a, ray,
					"Independent static segment only; not finite-source integrated visibility or "
					"bake mover certification");
		s_diagnosticScene = scene;
		ri.Printf(
			PRINT_ALL, "lighting-debug ray state=%u issues=%u hit-surface=%llu hit-material=%u\n",
			unsigned(ray.state), ray.issues,
			static_cast<unsigned long long>(ray.terminalHit.surface), ray.terminalHit.material);
		return;
	}
	if (command == "layer" && ri.Cmd_Argc() >= 3)
	{
		std::string name = ri.Cmd_Argv(2);
		bool enabled = ri.Cmd_Argc() < 4 || std::string(ri.Cmd_Argv(3)) != "0";
		bool known = name == "all";
		if (name == "all")
			s_diagnosticSelection.layers =
				enabled ? (1u << unsigned(LRDebug::Layer::Count)) - 1 : 0;
		else
			for (unsigned j = 0; j < unsigned(LRDebug::Layer::Count); ++j)
				if (name == LRDebug::Name(LRDebug::Layer(j)))
				{
					known = true;
					if (enabled)
						s_diagnosticSelection.layers |= 1u << j;
					else
						s_diagnosticSelection.layers &= ~(1u << j);
				}
		if (!known)
		{
			ri.Printf(PRINT_ALL,
					  "lighting-debug unknown layer; use info for supported layer names\n");
			return;
		}
		ri.Printf(PRINT_ALL, "lighting-debug layer-mask=%u (data overlay only)\n",
				  s_diagnosticSelection.layers);
		return;
	}
	if (command == "select" && ri.Cmd_Argc() == 4)
	{
		Id id = 0;
		if (!DiagnosticId(ri.Cmd_Argv(3), id))
		{
			ri.Printf(PRINT_ALL, "lighting-debug invalid uint64 ID\n");
			return;
		}
		std::string kind = ri.Cmd_Argv(2);
		if (kind == "source")
			s_diagnosticSelection.source = id;
		else if (kind == "region")
			s_diagnosticSelection.region = id;
		else if (kind == "configuration")
			s_diagnosticSelection.configuration = id;
		else
		{
			ri.Printf(PRINT_ALL, "select source|region|configuration <stable-id;0=all>\n");
			return;
		}
		return;
	}
	if (command == "load" && ri.Cmd_Argc() == 3)
	{
		s_diagnosticScene.reset();
		s_diagnosticSelection = {};
		s_diagnosticTicket = 0;
		std::string name = ri.Cmd_Argv(2);
		if (!DiagnosticName(name))
		{
			ri.Printf(PRINT_ALL, "lighting-debug invalid safe file name\n");
			return;
		}
		const auto &world = s_reconstructedLighting.Report();
		if (world.world.empty() || !tr.world)
		{
			ri.Printf(PRINT_ALL, "lighting-debug Unavailable: no loaded main world\n");
			return;
		}
		Disk::Digest expected{};
		for (auto &i : s_reconstructedLighting.InputFingerprint().inputs)
			if (i.kind == Disk::InputKind::Bsp)
				expected = i.sha256;
		if (expected == Disk::Digest{})
		{
			auto g = R_LuxelGeometry();
			if (g)
				expected = g->Map().sha256;
		}
		if (expected == Disk::Digest{})
		{
			RendererDependencies source;
			std::unique_ptr<Disk::Reader> original;
			auto e = source.Open(world.world, original);
			if (e)
				e = Disk::HashContent(*original, expected, 256ull << 20);
			if (!e)
			{
				ri.Printf(PRINT_ALL, "lighting-debug source identity unavailable: %s\n",
						  e.message.c_str());
				return;
			}
		}
		std::string path = "lightrecon-debug/" + name + ".jkdbg";
		LightmapSourceFile file;
		long count = ri.FS_FOpenFileRead(path.c_str(), &file.handle, qtrue);
		if (!file.handle || count <= 0 || count > 16 * 1024 * 1024)
		{
			ri.Printf(PRINT_ALL, "lighting-debug missing/oversized diagnostic file\n");
			return;
		}
		std::string text(size_t(count), '\0');
		if (ri.FS_Read(&text[0], int(count), file.handle) != count)
		{
			ri.Printf(PRINT_ALL, "lighting-debug short read\n");
			return;
		}
		auto scene = std::make_shared<LRDebug::Scene>();
		auto e = LRDebug::Decode(text, expected, *scene);
		if (!e)
		{
			ri.Printf(PRINT_ALL, "lighting-debug rejected: %s\n", e.message.c_str());
			return;
		}
		s_diagnosticScene = scene;
		s_diagnosticTicket = world.ticket;
		ri.Printf(PRINT_ALL,
				  "lighting-debug loaded %u records map=%s omitted=%llu; layers default OFF; not a "
				  "reconstructed-light consumer\n",
				  unsigned(scene->records.size()), Disk::Hex(scene->map).c_str(),
				  static_cast<unsigned long long>(scene->omitted));
		return;
	}
	if (command == "asset")
	{
		auto snapshot = R_ReconstructedLighting();
		auto a = snapshot.Asset();
		if (!a)
		{
			ri.Printf(PRINT_ALL, "lighting-debug asset Unavailable\n");
			return;
		}
		auto scene = std::make_shared<LRDebug::Scene>();
		scene->generation = snapshot.Generation();
		scene->units = a.value->units;
		scene->scope = "canonical source records; alternatives not summed; display only";
		scene->mapPath = s_reconstructedLighting.Report().world;
		for (auto &i : s_reconstructedLighting.InputFingerprint().inputs)
			if (i.kind == Disk::InputKind::Bsp)
				scene->map = i.sha256;
		LRDebug::Builder b(*scene);
		Vec3 widget(tr.refdef.vieworg[0] + 96 * tr.refdef.viewaxis[0][0],
					tr.refdef.vieworg[1] + 96 * tr.refdef.viewaxis[0][1],
					tr.refdef.vieworg[2] + 96 * tr.refdef.viewaxis[0][2]);
		for (auto &e : a.value->emitters)
			b.Emitter(e,
					  e.source.state == HypothesisState::Rejected
						  ? LRDebug::State::Rejected
						  : e.source.state == HypothesisState::Resolved
								? LRDebug::State::Accepted
								: LRDebug::State::Conditional,
					  a.value->assessment.selectedProfile,
					  "canonical record; Resolved is not exact-use suitability", widget);
		s_diagnosticScene = scene;
		s_diagnosticTicket = s_reconstructedLighting.Report().ticket;
		s_diagnosticSelection = {};
		return;
	}
	if (command == "sample" && (ri.Cmd_Argc() == 10 || ri.Cmd_Argc() == 11))
	{
		const std::string kind = ri.Cmd_Argv(2);
		Id id = 0, configuration = 0;
		if (!DiagnosticId(ri.Cmd_Argv(3), id) ||
			(ri.Cmd_Argc() == 11 && !DiagnosticId(ri.Cmd_Argv(10), configuration)))
			return;
		float v[6];
		for (unsigned j = 0; j < 6; ++j)
		{
			char *end = nullptr;
			errno = 0;
			const char *arg = ri.Cmd_Argv(j + 4);
			double x = std::strtod(arg, &end);
			if (errno || end == arg || *end || !std::isfinite(x) || std::abs(x) > 1e7)
			{
				ri.Printf(PRINT_ALL, "lighting-debug invalid query number\n");
				return;
			}
			v[j] = float(x);
		}
		auto snapshot = R_ReconstructedLighting();
		SourceHandle handle;
		if (id)
		{
			auto found = snapshot.IdentifySource(id);
			if (!found)
			{
				ri.Printf(PRINT_ALL, "lighting-debug field ID Unavailable (status=%u)\n",
						  unsigned(found.status));
				return;
			}
			handle = found.value;
		}
		Vec3 p(v[0], v[1], v[2]), direction(v[3], v[4], v[5]);
		auto start = std::chrono::steady_clock::now();
		if (kind == "conditional")
		{
			ConditionalFieldQuery q;
			q.field = handle;
			q.configuration = configuration;
			q.positionWorld = p;
			q.directionOrNormalWorld = direction;
			q.optIn = true;
			q.allowConditionalStaticGeometry = false;
			auto result = snapshot.SampleConditionalField(q);
			ri.Printf(PRINT_ALL,
					  "lighting-debug conditional irradiance status=%u; explicitly selected "
					  "configuration=%llu; no physical calibration implied\n",
					  unsigned(result.status), static_cast<unsigned long long>(configuration));
			if (result)
				ri.Printf(PRINT_ALL, "conditional value=%.7g %.7g %.7g unit=%u quantity=%u\n",
						  result.value.value.x, result.value.value.y, result.value.value.z,
						  unsigned(result.value.descriptor->unit),
						  unsigned(result.value.descriptor->quantity));
		}
		else if (kind == "indirect" || kind == "radiance")
		{
			Result<LightingSample> result;
			if (kind == "indirect")
			{
				IrradianceQuery q;
				q.field = handle;
				q.positionWorld = p;
				q.normalWorld = direction;
				result = snapshot.SampleIndirect(q);
			}
			else
			{
				RadianceQuery q;
				q.field = handle;
				q.positionWorld = p;
				q.directionToSource = direction;
				result = snapshot.SampleIncomingRadiance(q);
			}
			ri.Printf(PRINT_ALL, "lighting-debug physical %s status=%u\n", kind.c_str(),
					  unsigned(result.status));
			if (result)
				ri.Printf(PRINT_ALL, "physical value=%.7g %.7g %.7g unit=%u calibration=%llu\n",
						  result.value.rgb.x, result.value.rgb.y, result.value.rgb.z,
						  unsigned(result.value.unit),
						  static_cast<unsigned long long>(result.value.calibration));
		}
		else
		{
			ri.Printf(PRINT_ALL,
					  "sample indirect|radiance|conditional <field-id;0=automatic-physical> <px py "
					  "pz dx dy dz> [configuration]\n");
			return;
		}
		ri.Printf(
			PRINT_ALL,
			"lighting-debug immutable query us=%.3f; no fallback, no disk/build/ray preparation\n",
			std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - start)
				.count());
		return;
	}
	if (command == "report" && ri.Cmd_Argc() == 3)
	{
		std::string name = ri.Cmd_Argv(2);
		if (!DiagnosticName(name))
		{
			ri.Printf(PRINT_ALL, "lighting-debug invalid report name\n");
			return;
		}
		std::ostringstream json, human;
		auto snapshot = R_ReconstructedLighting();
		auto e = LRDebug::Report(snapshot, &s_reconstructedLighting.Report(), json, &human,
								 s_diagnosticScene.get());
		if (!e)
		{
			ri.Printf(PRINT_ALL, "lighting-debug report unavailable: %s\n", e.message.c_str());
			return;
		}
		auto text = json.str();
		std::string path = "lightrecon-debug/reports/" + name + ".json";
		ri.FS_WriteFile(path.c_str(), text.data(), int(text.size()));
		ri.Printf(PRINT_ALL, "%sreport written: %s\n", human.str().c_str(), path.c_str());
		return;
	}
	if (command == "source" && ri.Cmd_Argc() == 3)
	{
		Id id = 0;
		if (!DiagnosticId(ri.Cmd_Argv(2), id))
			return;
		unsigned count = 0;
		if (s_diagnosticScene)
			for (auto &r : s_diagnosticScene->records)
				if (r.source == id && count++ < 64)
					ri.Printf(PRINT_ALL,
							  "debug source=%llu sample=%llu layer=%s state=%s profile=%s "
							  "quantity=%s domain=%s reason=%s\n",
							  static_cast<unsigned long long>(r.source),
							  static_cast<unsigned long long>(r.sample), LRDebug::Name(r.layer),
							  LRDebug::Name(r.state), r.profile.c_str(), r.quantity.c_str(),
							  r.domain.c_str(), r.reason.c_str());
		if (!count)
			ri.Printf(PRINT_ALL,
					  "lighting-debug source ID not present; Unknown, not zero contribution\n");
		return;
	}
	ri.Printf(PRINT_ALL,
			  "lighting-debug records=%u mask=%u drawn=%llu clipped=%llu last-overlay-us=%.3f\n",
			  s_diagnosticScene ? unsigned(s_diagnosticScene->records.size()) : 0,
			  s_diagnosticSelection.layers, static_cast<unsigned long long>(s_diagnosticDrawn),
			  static_cast<unsigned long long>(s_diagnosticClipped), s_diagnosticMicroseconds);
	ri.Printf(PRINT_ALL,
			  "r_lightReconDebug load <safe-name> | asset | prepare | geometry | ray <source-id> "
			  "<sample-id> | sample indirect|radiance|conditional <field-id> <px py pz dx dy dz> "
			  "[configuration] | layer "
			  "<sources|placement|dynamic|receivers|direct|sun|sky|indirect|unresolved|residual|"
			  "probes|field|visibility|transport|all> [0/1] | select source|region|configuration "
			  "<id;0=all> | source <id> | report <safe-name> | off\n");
}
void R_LuxelDebugDraw(const refdef_t *fd)
{
	// No debug state means no shader registration, poly submission or asset query.
	const bool diagnosticActive=s_diagnosticScene&&s_diagnosticSelection.layers&&
		s_diagnosticTicket==s_reconstructedLighting.Report().ticket;
	if ((s_luxelDebug.empty() && s_reconstructionPreview.empty()&&!diagnosticActive) || (fd->rdflags & RDF_NOWORLDMODEL))
		return;
	if (!s_luxelDebugShader)
		s_luxelDebugShader = RE_RegisterShaderFromImage("*luxelDebug", lightmapsVertex, stylesDefault,
														tr.whiteImage, qfalse);
	auto line = [&](const vec3_t a, const vec3_t b, const byte *colour) {
		vec3_t dir, eye, side;
		VectorSubtract(b, a, dir);
		VectorSubtract(a, fd->vieworg, eye);
		CrossProduct(dir, eye, side);
		if (VectorNormalize(side) < 1e-6f)
			return;
		VectorScale(side, .35f, side);
		polyVert_t p[4];
		VectorSubtract(a, side, p[0].xyz);
		VectorAdd(a, side, p[1].xyz);
		VectorAdd(b, side, p[2].xyz);
		VectorSubtract(b, side, p[3].xyz);
		for (auto &v : p)
		{
			v.st[0] = v.st[1] = .5f;
			Com_Memcpy(v.modulate, colour, 4);
		}
		RE_AddPolyToScene(s_luxelDebugShader, 4, p, 1);
	};
	if(diagnosticActive){
		auto started=std::chrono::steady_clock::now();s_diagnosticDrawn=s_diagnosticClipped=0;
		for(const auto&r:s_diagnosticScene->records){
			if(!r.geometryKnown||!s_diagnosticSelection.Matches(r))continue;
			if(s_diagnosticDrawn>=2048){++s_diagnosticClipped;continue;}
			vec3_t a={r.a.x,r.a.y,r.a.z},b={r.b.x,r.b.y,r.b.z},c={r.c.x,r.c.y,r.c.z};
			vec3_t delta;VectorSubtract(a,fd->vieworg,delta);
			if(VectorLengthSquared(delta)>4096*4096||DotProduct(delta,fd->viewaxis[0])<=0){++s_diagnosticClipped;continue;}
			byte colour[4]={0,0,0,255};LRDebug::Colour(r.state,colour);
			if(r.primitive==LRDebug::Primitive::Point){for(unsigned j=0;j<3;++j){VectorCopy(a,b);VectorCopy(a,c);b[j]-=2;c[j]+=2;line(b,c,colour);}}
			else {line(a,b,colour);if(r.primitive==LRDebug::Primitive::Triangle){line(b,c,colour);line(c,a,colour);}}
			++s_diagnosticDrawn;
		}
		s_diagnosticMicroseconds=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-started).count();
	}
    for(const auto &m:s_reconstructionPreview) {
        vec3_t centre,delta;VectorCopy(m.position,centre);
        // A directional source has no finite position. This anchor is a DRAWING
        // widget in front of the camera, never a reconstructed world-space XYZ.
        if(m.kind==4)VectorMA(fd->vieworg,96,fd->viewaxis[0],centre);
        VectorSubtract(centre,fd->vieworg,delta);
        if(VectorLengthSquared(delta)>2048*2048)continue;
        byte colour[4]={255,150,30,255};if(m.priorOnly){colour[0]=255;colour[1]=40;colour[2]=230;}
        auto axis=[&](const vec3_t v,float extent){vec3_t a,b;VectorMA(centre,-extent,v,a);VectorMA(centre,extent,v,b);line(a,b,colour);};
        axis(m.right,std::max(4.f,m.kind==2?m.width:m.kind==3?m.length:m.radius));
        axis(m.up,std::max(4.f,m.kind==2?m.height:m.radius));axis(m.forward,m.kind==1||m.kind==4?24.f:6.f);
        if(m.kind==1||m.kind==4){
            vec3_t tip,a,b;VectorMA(centre,24,m.forward,tip);VectorMA(tip,-6,m.forward,a);
            VectorMA(a,3,m.right,b);line(tip,b,colour);VectorMA(a,-3,m.right,b);line(tip,b,colour);
        }
        if(m.kind==2) {
            vec3_t corners[4];
            for(unsigned i=0;i<4;++i) {VectorMA(centre,(i==0||i==3?-1:1)*m.width,m.right,corners[i]);VectorMA(corners[i],(i<2?-1:1)*m.height,m.up,corners[i]);}
            for(unsigned i=0;i<4;++i)line(corners[i],corners[(i+1)%4],colour);
        }
    }

	for (const auto &s : s_luxelDebug)
	{
		vec3_t p = {s.positionWorld.x, s.positionWorld.y, s.positionWorld.z},
			   n = {s.geometricNormal.x, s.geometricNormal.y, s.geometricNormal.z}, end, a, b, eye;
		VectorSubtract(p, fd->vieworg, eye);
		if (VectorLengthSquared(eye) > 2048 * 2048 || VectorLengthSquared(eye) < 32 * 32)
			continue;
		// 0.12 world units is DRAWING separation only; CPU observation stays exact.
		VectorMA(p, .12f, n, p);
		VectorMA(p, 6, n, end);
		byte red[4] = {255, 60, 60, 255};
		line(p, end, red);
		vec3_t tangent = {s.tangentWorld.x, s.tangentWorld.y, s.tangentWorld.z};
		VectorMA(p, -1.0f, tangent, a);
		VectorMA(p, 1.0f, tangent, b);
		byte colour[4] = {60, 255, 80, 255};
		const auto geometry = R_LuxelGeometry();
		if (geometry && s.surface < geometry->Surfaces().size() &&
			geometry->Surfaces()[s.surface].kind == Rend2LightRecon::Core::BakedSurfaceKind::Patch)
		{
			colour[0] = 40;
			colour[1] = 210;
			colour[2] = 255;
		}
		if (s.issues & Rend2LightRecon::Core::LuxelBorder)
		{
			colour[0] = 255;
			colour[1] = 190;
			colour[2] = 20;
		}
		line(a, b, colour);
	}
}

namespace
{
class SourceEmissionMasks final : public Rend2LightRecon::Core::EmissionMaskProvider
{
	mutable std::map<std::string, Rend2LightRecon::Core::Emission::Mask> masks_;
	mutable uint64_t bytes_ = 0;

  public:
	Rend2LightRecon::Core::Error Read(const Rend2LightRecon::Core::EmissionMaskRequest &request,
									  const Rend2LightRecon::Core::ReadContext &context,
									  Rend2LightRecon::Core::Emission::Mask &mask) const override
	{
		using namespace Rend2LightRecon::Core;
		auto cached = masks_.find(request.texture);
		if (cached != masks_.end())
		{
			mask = cached->second;
			return {};
		}
		if (request.texture.empty())
			return Failure(Code::Unavailable, "No source emission texture");
		if (context.cancellation && context.cancellation->IsCancelled())
			return Failure(Code::Cancelled, "Mask read cancelled");
		if (request.texture[0] == '*')
			return Failure(Code::Unavailable, "Procedural/white image has no source mask");
		// Source image decoding, never GL readback or a GPU-ready colour transform.
		byte *pixels = nullptr;
		int width = 0, height = 0;
		R_LoadImage(request.texture.c_str(), &pixels, &width, &height);
		struct Release
		{
			byte *pixels;
			~Release()
			{
				if (pixels)
					Z_Free(pixels);
			}
		} release{pixels};
		if (!pixels || width <= 0 || height <= 0)
			return Failure(Code::Unavailable, "Source emission image missing");
		mask.width = std::min(width, 128);
		mask.height = std::min(height, 128);
		if (uint64_t(mask.width) * mask.height * 3 * sizeof(float) > context.maxBytes)
			return Failure(Code::MemoryLimit, "Source mask budget");
		mask.rgb.resize(size_t(mask.width) * mask.height * 3);
		// Box average to a bounded CPU mask; source texture colour only, not luminance calibration.
		double total[3] = {};
		for (unsigned y = 0; y < mask.height; ++y)
			for (unsigned x = 0; x < mask.width; ++x)
			{
				double sum[3] = {};
				uint64_t count = 0;
				int x0 = int(uint64_t(x) * width / mask.width),
					x1 = int(uint64_t(x + 1) * width / mask.width);
				int y0 = int(uint64_t(y) * height / mask.height),
					y1 = int(uint64_t(y + 1) * height / mask.height);
				for (int j = y0; j < y1; ++j)
					for (int i = x0; i < x1; ++i)
					{
						++count;
						for (int c = 0; c < 3; ++c)
						{
							float v = pixels[(size_t(j) * width + i) * 4 + c] / 255.0f;
							sum[c] += v <= .04045f ? v / 12.92f : powf((v + .055f) / 1.055f, 2.4f);
						}
					}
				for (int c = 0; c < 3; ++c)
				{
					float value = float(sum[c] / count);
					mask.rgb[(size_t(y) * mask.width + x) * 3 + c] = value;
					total[c] += value;
				}
			}
		for (int c = 0; c < 3; ++c)
			mask.average[c] = float(total[c] / (mask.width * mask.height));
		uint64_t bytes = mask.rgb.size() * sizeof(float);
		if (bytes_ + bytes <= 16ull * 1024 * 1024)
		{
			masks_.emplace(request.texture, mask);
			bytes_ += bytes;
		}
		return {};
	}
};
} // namespace
static void R_CaptureEmitterEvidence()
{
	using namespace Rend2LightRecon::Core;
	std::atomic_store(&s_emitterEvidence,
					  std::shared_ptr<const Rend2LightRecon::Core::EmitterEvidenceSet>());
	s_emitterEvidenceError = {};
	auto geometry = R_LuxelGeometry();
	if (!geometry || !tr.world)
	{
		s_emitterEvidenceError = Failure(Code::Unavailable, "Source geometry not captured");
		return;
	}
	EmitterEvidenceInput input;
	input.map = geometry->Map();
	input.geometry = geometry;
	input.masks = std::make_shared<SourceEmissionMasks>();
	if (tr.world->entityString)
		input.entityText = tr.world->entityString;
	std::set<std::string> materials;
	for (const auto &s : geometry->Surfaces())
		if (materials.insert(s.material).second)
		{
			auto text = R_LightReconShaderText(s.material.c_str());
			if (!text.empty())
			{
				ShaderEvidenceText shader;
				shader.material = s.material;
				shader.text = std::move(text);
				input.shaders.push_back(std::move(shader));
			}
		}
	input.areaPath = "maps/" + std::string(tr.world->baseName) + ".arealights.json";
	{
		LightmapSourceFile file;
		int bytes = ri.FS_FOpenFileRead(input.areaPath.c_str(), &file.handle, qtrue);
		if (file.handle && bytes > 0)
		{
			if (bytes > 1024 * 1024)
			{
				s_emitterEvidenceError = Failure(Code::MemoryLimit, "Area evidence JSON limit");
				return;
			}
			input.areaJson.resize(bytes);
			ri.FS_Read(&input.areaJson[0], bytes, file.handle);
		}
	}
	ReadContext context;
	context.maxBytes = 64ull * 1024 * 1024;
	std::shared_ptr<const EmitterEvidenceSet> publication;
	s_emitterEvidenceError = ExtractEmitterEvidence(input, EvidenceOptions(), context, publication);
	std::atomic_store(&s_emitterEvidence, publication);
}
void R_LightReconEvidence_f(void)
{
	using namespace Rend2LightRecon::Core;
	const auto evidence = R_EmitterEvidence();
	if (!evidence)
	{
		ri.Printf(PRINT_ALL,
				  "Emitter evidence unavailable: %s (build policy 2/3 at map load required; no "
				  "lights fitted)\n",
				  s_emitterEvidenceError.message.c_str());
		return;
	}
	const auto &r = evidence->Report();
	uint64_t classes[7] = {};
	for (const auto &c : evidence->Candidates())
		for (unsigned bit = 0; bit < 7; ++bit)
			if (c.shapes & (1u << bit))
				++classes[bit];
	ri.Printf(PRINT_ALL,
			  "Emitter evidence %s: %u candidates, %u evidence, %llu bytes; hypotheses only\n",
			  evidence->Map().path.c_str(), unsigned(evidence->Candidates().size()),
			  unsigned(evidence->Evidence().size()), (unsigned long long)r.bytes);
	ri.Printf(PRINT_ALL,
			  "  point=%llu rect=%llu tube=%llu spot=%llu surface=%llu sun=%llu sky=%llu (classes "
			  "overlap)\n",
			  (unsigned long long)classes[0], (unsigned long long)classes[1],
			  (unsigned long long)classes[2], (unsigned long long)classes[3],
			  (unsigned long long)classes[4], (unsigned long long)classes[5],
			  (unsigned long long)classes[6]);
	ri.Printf(PRINT_ALL,
			  "  triangles=%llu groups=%llu maskBlobs=%llu authored=%llu entities=%llu "
			  "sunDeclarations=%llu skyHints=%llu animated=%llu moving=%llu unsupported=%llu\n",
			  (unsigned long long)r.triangles, (unsigned long long)r.groups,
			  (unsigned long long)r.maskBlobs, (unsigned long long)r.authored,
			  (unsigned long long)r.entities, (unsigned long long)r.suns,
			  (unsigned long long)r.skies, (unsigned long long)r.animated,
			  (unsigned long long)r.moving, (unsigned long long)r.unsupported);
	if (ri.Cmd_Argc() > 1)
		for (const auto &e : evidence->Evidence())
			ri.Printf(PRINT_ALL, "  id=%llu origin=%s material=%s shapes=%u issues=%u static=%d\n",
					  (unsigned long long)e.id, EvidenceOriginName(e.origin), e.material.c_str(),
					  e.shapes, e.issues, e.staticEligible ? 1 : 0);
}

// Explicit author/tool operation. Never called by map loading or consumer queries.
static void R_LightReconExport()
{
	using namespace Rend2LightRecon;
	using namespace Rend2LightRecon::Core;
	std::string stem = ri.Cmd_Argc() > 1 ? ri.Cmd_Argv(1) : "";
	if ((ri.Cmd_Argc() != 2 && ri.Cmd_Argc() != 3 && ri.Cmd_Argc() != 7) || stem.empty() ||
		stem.size() > 64 ||
		stem.find_first_not_of(
			"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-") !=
			std::string::npos)
	{
		ri.Printf(
			PRINT_ALL,
			"usage: r_lightReconExport <safe-name> [mapped-images:1,0=all] [cx cy cz radius]\n");
		return;
	}
	Offline::Sources s;
	s.images = R_LightmapObservations();
	s.grid = R_LightgridObservations();
	s.geometry = R_LuxelGeometry();
	s.evidence = R_EmitterEvidence();
	s.domain = s_offlineDomain;
	s.dynamic = s_offlineDynamic;
	if (!s.geometry || !s.images)
	{
		ri.Printf(PRINT_ALL, "offline export unavailable: reload main map with r_lightReconBuild "
							 "2/3; consumers may remain off\n");
		return;
	}
	if (!s.domain)
	{
		ri.Printf(PRINT_ALL, "offline export unavailable: original BSP collision snapshot: %s\n",
				  s_offlineSpaceError.message.c_str());
		return;
	}
	s.metadata.map = s.geometry->Map();
	s.metadata.producer = "Rend2 original CPU map-load snapshots";
	s.metadata.producerVersion = "org.openjk.offline-export.v1";

#ifdef _MSC_VER
	s.metadata.producerVersion += "; MSVC " + std::to_string(_MSC_FULL_VER);
#else
	s.metadata.producerVersion += std::string("; GNU/Clang ") + __VERSION__;
#endif
	s.metadata.compiler = "Unknown historical BSP compiler";
	s.metadata.recipe = "Renderer load is not a historical bake recipe";
	s.metadata.units.worldUnitsPerMeter = r_pbrWorldUnitsPerMeter->value;
	s.metadata.units.physicalToScene = R_PBRPhysicalToScene();
	auto &profile = s.metadata.decoding;
	profile.identity = "renderer-export-source-relative-v1";
	profile.sourceProvenance =
		"Original BSP/external samples; historical bake transfer and absolute intensity unknown";
	// Unknown remains unavailable; users can form explicit alternative hypotheses offline.
	auto &r = profile.renderer;
	r.known = true;
	r.linearLighting = r_linearLighting->integer != 0;
	r.physicalPbr = r_pbrPhysical->integer != 0;
	r.hdr = r_hdr->integer != 0;
	r.toneMap = r_toneMap->integer != 0;
	r.floatLightmaps = glRefConfig.floatLightmap != qfalse;
	r.mapOverbrightBits = r_mapOverBrightBits->integer;
	r.requestedOverbrightBits = r_overBrightBits->integer;
	r.effectiveOverbrightBits = tr.overbrightBits;
	r.displayGamma = r_gamma->value;
	r.cameraExposure = r_cameraExposure->value;
	r.exposureCompensation = r_exposureCompensation->value;
	r.autoExposure = r_autoExposure->integer != 0;
	r.exposureKnown = true;
	r.lightmapDebug = r_lightmap->integer;
	for (const auto &im : s.images->Images())
		r.hdrLightmapSeen |= im.encoding == LightmapEncoding::ExternalRgb32F;
	Offline::Options options;
	// Stock ctf1's immutable emission evidence exceeds the generic 8 MiB
	// per-section default. Keep it intact under an explicit bounded tool budget.
	options.maxChunkBytes = 32ull * 1024 * 1024;
	options.maxMappedTexels = 8ull * 1024 * 1024;
	if (ri.Cmd_Argc() > 2)
	{
		const char *arg = ri.Cmd_Argv(2);
		char *end = nullptr;
		errno = 0;
		unsigned long n = std::strtoul(arg, &end, 10);
		if (!arg[0] || arg[0] == '-' || !end || *end || errno || n > UINT_MAX)
		{
			ri.Printf(PRINT_ALL, "offline export invalid image prefix\n");
			return;
		}
		options.mappedImages = unsigned(n);
	}
	ReadContext context;
	context.maxBytes = options.maxResidentBytes;
	// A bounded, explicitly stored ROI; source geometry/images always cover the full map.
	const auto &world = s.domain->Source().worldBounds;
	Vec3 centre((world.minWorld.x + world.maxWorld.x) * .5f,
				(world.minWorld.y + world.maxWorld.y) * .5f,
				(world.minWorld.z + world.maxWorld.z) * .5f);
	float radius = 256;
	if (ri.Cmd_Argc() >= 7)
	{
		centre = Vec3(float(atof(ri.Cmd_Argv(3))), float(atof(ri.Cmd_Argv(4))),
					  float(atof(ri.Cmd_Argv(5))));
		radius = float(atof(ri.Cmd_Argv(6)));
	}
	if (!std::isfinite(centre.x) || !std::isfinite(centre.y) || !std::isfinite(centre.z) ||
		!std::isfinite(radius) || radius <= 0)
	{
		ri.Printf(PRINT_ALL, "offline export invalid region ROI\n");
		return;
	}
	VisibilityBuildOptions visibility;
	visibility.patchToleranceWorld = s.metadata.visibilityPatchToleranceWorld;
	visibility.maxBytes = 128ull * 1024 * 1024;
	std::shared_ptr<const StaticVisibility> rays;
	auto error = StaticVisibility::Build(s.geometry, visibility, context, rays, s.domain);
	if (!error)
	{
		ri.Printf(PRINT_ALL, "offline export visibility preparation failed: %s\n",
				  error.message.c_str());
		return;
	}
	RegionBuildInput input;
	input.identity = s.metadata.map;
	input.domain = s.domain;
	input.geometry = rays;
	input.dynamic = s.dynamic;
	RegionOptions region;
	region.minimumCellWorld = 16;
	region.coarseCellWorld = 128;
	region.constrictionWorld = 64;
	region.skySamplesPerRegion = 4;
	region.bounds.known = true;
	region.bounds.minWorld = Vec3(std::max(world.minWorld.x, centre.x - radius),
								  std::max(world.minWorld.y, centre.y - radius),
								  std::max(world.minWorld.z, centre.z - radius));
	region.bounds.maxWorld = Vec3(std::min(world.maxWorld.x, centre.x + radius),
								  std::min(world.maxWorld.y, centre.y + radius),
								  std::min(world.maxWorld.z, centre.z + radius));
	error = LightingRegionGraph::Build(std::move(input), region, context, s.regions);
	if (!error)
	{
		ri.Printf(PRINT_ALL, "offline export region graph failed: %s\n", error.message.c_str());
		return;
	}
	std::string root = R_LightReconCacheRoot();
	if (root.empty())
	{
		ri.Printf(PRINT_ALL, "offline export requires supported fs_homepath\n");
		return;
	}
	std::string path = root + "/observations/" + stem + ".jkobs";
	Offline::Summary summary;
	error = Offline::ExportFile(path, s, options, context, summary);
	if (!error)
	{
		ri.Printf(PRINT_ALL, "offline export failed: %s\n", error.message.c_str());
		return;
	}
	ri.Printf(PRINT_ALL,
			  "offline export wrote %s bytes=%llu samples=%llu complete=%d raw-images=%llu "
			  "regions=%llu (observations, no reconstructed lights)\n",
			  path.c_str(), (unsigned long long)summary.fileBytes,
			  (unsigned long long)summary.mappedSamples, summary.derivedComplete ? 1 : 0,
			  (unsigned long long)summary.images, (unsigned long long)summary.regions);
}
void R_LightReconExport_f(void)
{
	try
	{
		R_LightReconExport();
	}
	catch (const std::bad_alloc &)
	{
		ri.Printf(PRINT_ALL,
				  "offline export allocation failed; no reconstructed asset published\n");
	}
}
