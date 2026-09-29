// Copyright (C) 2026 OpenJK contributors. GPL-2.0-or-later.
// Included once by each frontend's FxSystem.cpp.
void SFxHelper::ReportPhysicalSources() {
    PhysicalizationPrint("FX sources: tracking %d, generation %llu; last automatic frontend scene snapshots before renderer culling/budget\n",
        int(mPhysicalSources.Enabled()), (unsigned long long)mPhysicalSources.Generation());
    if (!mPhysicalSources.Enabled()) {
        PhysicalizationPrint("Enable fx_physicalizationSources 1 and spawn fresh FX; old particles remain untracked.\n");
        return;
    }
    for (int portal = 0; portal < 2; ++portal) {
        auto& scene = mPhysicalSourceScenes[portal];
        if (scene.generation != mPhysicalSources.Generation()) scene.Begin(mPhysicalSources.Generation());
        const auto rows = scene.Rows();
        unsigned density = 0, glow = 0;
        for (const auto& row : rows) { density += row.density; glow += row.glow; }
        const char* scope = portal ? "portal" : "world";
        PhysicalizationPrint("FXSOURCE_SCENE|%s|%llu|%d|%u|%u|%u|%u|%u\n", scope,
            (unsigned long long)scene.generation, scene.time, unsigned(rows.size()), density, glow, scene.untracked, scene.overflow);
        for (const auto& row : rows)
            PhysicalizationPrint("FXSOURCE_ROW|%s|%llu|%llu|%u|%u|%u\n", scope,
                (unsigned long long)row.source.generation, (unsigned long long)row.source.id,
                row.source.domain, row.density, row.glow);
    }
}
