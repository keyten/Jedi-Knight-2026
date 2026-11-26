# Lighting reconstruction (research)

[All rendering settings](../rendering-features.md)

**Players can usually skip this page.** These controls are for lighting research and physically authored cache assets; they do not automatically relight stock maps.

An experimental toolset that tries to work out, from a map's baked lighting, where its original lights were and how bright. Fitting runs offline. The engine can export observations, prepare reusable fields and show preview markers. Separate runtime adapters now exist for a resolved physical sun and authored reflected lighting on models. Both are gated and off by default; missing or unsuitable data keeps the legacy lighting.

Stock-map reconstruction is still research: the audited study accepted no nonempty physical source model across eight original maps and prepared no original-stock physical fields. Preview markers and a successful cache load do not prove that recovered lights are correct.

## Controls

- `r_lightReconBuild 0` — policy for the next main-world map load: **0** off, **1** cache only, **2** use cache or prepare, **3** force preparation. This never runs the offline light fitter.
- `r_lightReconUse 0` — live master switch for consumers; does not build or load an asset.
- `r_lightReconSun 0` — live request for the resolved physical sun adapter. Needs suitable sun data, `r_pbrPhysical 1`, nonzero `r_sunlightMode`, a depth prepass and initialized sun-shadow resources. Explicit sun overrides retain priority. Current cubemap/diffuse-IBL gates and energy checks can refuse it; see the C01 guide below.
- `r_lightReconEntities 0` *(restart)* — use a qualifying authored reflected-irradiance field for model/Ghoul2 diffuse lighting. It replaces that model's legacy diffuse-grid input; it does not replace world lightmaps or dynamic lights. Missing samples and unsupported materials fall back. Needs `r_lightReconUse 1` and a prepared matching asset.
- `r_lightReconFog`, `r_lightReconParticles`, `r_lightReconSpecular`, `r_lightReconIndirect`, `r_lightReconWorldDiffuse`, `r_lightReconShadows`, `r_lightReconArea`, `r_lightReconEnvironment` — reserved consumer requests, all 0. Availability depends on the actual adapter and asset; setting a request alone does not add lighting.

All controls default to 0 in SP and MP. `r_lightReconBuild` is sampled at map load; consumer/master switches are live except for the latched entity switch. `r_lightReconUse 0` is the live A/B switch for enabled consumers.

**Debug**

- `r_lightReconInfo` — asset state, prepared capabilities, consumer availability and fallback diagnostics, without building anything.
- `r_lightReconExport` — export the map's lighting observations for the offline tools.
- `r_lightReconObservations`, `r_lightReconLuxels`, `r_lightReconEvidence` — inspect preserved baked samples, lightmap-to-geometry mapping and detected light evidence.
- `r_lightReconPreview <name> | off` — markers from `lightrecon-preview/<name>.txt`. Needs `r_lightReconBuild 2` or `3` and a matching map; it never adds lights.

## Workflow

Export observations with `r_lightReconExport`, then run the offline tools. They estimate sun and sky, propose hidden and visible emitters, fit intensity and position, check visibility and bounced light, and report uncertainty. Reusable fields, persistent caches and bounded in-game diagnostics support inspection. Anything the data cannot determine stays unknown; relative appearance fits are not physical lights and are refused by physical consumers.

Guides: [producer workflow](../rend2-producer-s13.md), [controls](../rend2-reconstruction-controls.md), [resolved sun (C01)](../rend2-resolved-sun-c01.md), [model irradiance (C04)](../rend2-entity-irradiance-c04.md), [reusable-field validation](../rend2-reusable-lighting-validation-v02.md), [stock discovery audit](../rend2-stock-discovery-asr25.md), [in-game diagnostics](../rend2-lighting-diagnostics-v03.md), [per-source contributions](../rend2-contributions-p06.md).
