// Copyright (C) 2026 OpenJK contributors. SPDX-License-Identifier: GPL-2.0-or-later
#include "tr_local.h"
#include "tr_lightrecon.h"
#include "lightrecon/cache.h"
#include <algorithm>
#include <new>

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
	s_lightmapCapture.Begin(s_reconstructedLighting.Report().policy);
	s_lightgridCapture.Begin(s_reconstructedLighting.Report().policy);
}

LightReconWorldLoad::~LightReconWorldLoad()
{
	if (!finished_ && s_reconstructedLighting.Report().ticket == load_.Ticket())
	{
		s_lightmapCapture.Reset();
		s_lightgridCapture.Reset();
	}
}

namespace {
class RendererWorldSource : public Rend2LightRecon::WorldSource
{
public:
	Rend2LightRecon::Disk::Error Describe(const std::string&, Rend2LightRecon::Disk::Fingerprint& fingerprint) override
	{
		const auto observations = R_LightmapObservations();
		const auto grid = R_LightgridObservations();
		if (observations || grid)
		{
			const auto& identity = observations ? observations->Map() : grid->Map();
			Rend2LightRecon::Disk::InputContent map;
			map.kind = Rend2LightRecon::Disk::InputKind::Bsp;
			map.name = identity.path;
			map.bytes = identity.bytes;
			map.sha256 = identity.sha256;
			fingerprint.inputs.push_back(map);
			fingerprint.complete[static_cast<size_t>(map.kind)] = true;
		}
		// File/BSP/material dependency collection is deliberately NOT guessed from
		// world name, GPU textures or calibrated lightgrid buffers. No false cache hit.
		Rend2LightRecon::Disk::Error error;
		error.code = Rend2LightRecon::Disk::Code::NotCacheable;
		error.message = "Complete BSP/sidecar/material/settings fingerprints are unavailable";
		return error;
	}
	Rend2LightRecon::Core::Error Prepare(const std::string&, Rend2LightRecon::BuildPlan& plan) override
	{
		plan.input.lightmaps = R_LightmapObservations();
		plan.input.lightgrid = R_LightgridObservations();
		return Rend2LightRecon::Core::Failure(Rend2LightRecon::Core::Code::Unavailable,
			"Original lightmaps may be captured; geometry/material adapter and reconstruction algorithm are not installed");
	}
};
}
void LightReconWorldLoad::Finish(int surfaces, int gridSamples, int lightmapTextures)
{
	if (s_lightmapCapture.Needed())
		s_lightmapCapture.Freeze();
	if (s_lightgridCapture.Needed())
		s_lightgridCapture.Freeze();
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
	Rend2LightRecon::NativeWorldCache cache(R_LightReconCacheRoot());
	load_.Finish(facts, source, &cache);
	// A completed build/cache hit needs no resident source images. Currently the
	// renderer has no complete cache key/solver, so BuildRequired keeps a bounded
	// immutable input snapshot for a future session and CPU inspection.
	if (s_reconstructedLighting.Report().state == Rend2LightRecon::WorldState::Available)
	{
		s_lightmapCapture.Reset();
		s_lightgridCapture.Reset();
	}
	finished_ = true;
}

void R_ClearReconstructedLighting(void)
{
	s_lightmapCapture.Reset();
	s_lightgridCapture.Reset();
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
void LightReconBspCapture::GridLayout(const float* origin, const float* spacing, const int* bounds)
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
	const void* pixels, bool hdr)
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
			path ? path : "", width, height, static_cast<const uint8_t*>(pixels), static_cast<size_t>(values));
}
void LightReconBspCapture::Missing(int index, bool deluxe, const char* path, bool hdr)
{
	using namespace Rend2LightRecon::Core;
	if (!Needed()) return;
	if (index < 0) { s_lightmapCapture.Fail(Code::InvalidInput, "Negative missing lightmap index"); return; }
	s_lightmapCapture.Missing(static_cast<uint32_t>(index), deluxe ? LightmapRole::DeluxeDirection : LightmapRole::Lighting,
		hdr ? LightmapEncoding::ExternalRgb32F : LightmapEncoding::ExternalRgba8, path ? path : "");
}

Rend2LightRecon::ConsumerState R_LightReconConsumer(const Rend2LightRecon::Snapshot& snapshot, Rend2LightRecon::Consumer consumer)
{
	Rend2LightRecon::UseSettings settings;
	settings.master = r_lightReconUse && r_lightReconUse->integer != 0;
#define LIGHTRECON_CONTROL(name, maximum, description)
#define LIGHTRECON_CONSUMER(name, kind, description) settings.requested[static_cast<size_t>(Rend2LightRecon::Consumer::kind)] = name && name->integer != 0;
#include "tr_lightrecon_cvars.inc"
#undef LIGHTRECON_CONSUMER
#undef LIGHTRECON_CONTROL
	// An existing fog/PBR/Forward+/LTC toggle alone is not an implementation of
	// canonical consumption. Future adapters must also check their real feature
	// prerequisites here, without setting any cvar or changing the shared asset.
	return Rend2LightRecon::EvaluateConsumer(snapshot, settings, consumer, Rend2LightRecon::FeatureSupport());
}

void R_LightReconInfo_f(void)
{
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
	for (size_t i = 0; i < kConsumerCount; ++i)
	{
		const Consumer c = static_cast<Consumer>(i);
		const ConsumerState state = R_LightReconConsumer(snapshot, c);
		ri.Printf(PRINT_ALL, "  %s requested=%d effective=%d capability=%s feature=%d: %s\n", ConsumerCvar(c),
			state.requested ? 1 : 0, state.effective ? 1 : 0, StatusName(state.capabilityStatus),
			state.featureAvailable ? 1 : 0, GateReasonName(state.reason));
	}
}
