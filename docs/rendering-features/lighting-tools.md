# Offline lighting reconstruction (research)

[All rendering settings](../rendering-features.md)

**Players can skip this page: nothing here changes how the game looks.**

An experimental research toolset that tries to work out, from a stock map's baked lighting, where its original lights were and how bright. The fitting runs offline in separate tools; the game only exports the map's lighting data and shows preview markers. The results are hypotheses: on stock maps they don't yet recover reliable real lights, and the normal renderer doesn't use them.

## Controls

- `r_lightReconBuild 0` — prepare source data at the next map load: **0** off, **1** use cached data only, **2** use cache or build, **3** always rebuild. This prepares observations; it never fits or relights.
- `r_lightReconUse 0` and `r_lightReconFog`, `r_lightReconEntities`, `r_lightReconParticles`, `r_lightReconSpecular`, `r_lightReconIndirect`, `r_lightReconWorldDiffuse`, `r_lightReconShadows`, `r_lightReconSun`, `r_lightReconArea`, `r_lightReconEnvironment` — requests for future renderer features that would use reconstructed light. None exists yet; all 0.

**Debug**

- `r_lightReconInfo` — what data is available, without building anything.
- `r_lightReconExport` — export the map's lighting observations for the offline tools.
- `r_lightReconObservations`, `r_lightReconLuxels`, `r_lightReconEvidence` — inspect the preserved baked samples, lightmap-to-geometry mapping and detected light evidence.
- `r_lightReconPreview <name> | off` — show markers from `lightrecon-preview/<name>.txt`. Needs `r_lightReconBuild 2` or `3` and a matching map; it never adds lights.

## Workflow

Export observations with `r_lightReconExport`, then run the offline tools. They estimate sun and sky, propose hidden light sources and their shapes, fit intensity and position, check shadows and bounced light, and report uncertainty; anything the data can't determine stays marked unknown.

Guides: [producer workflow](../rend2-producer-s13.md), [stock-map fit results](../rend2-stock-joint-fits-x12.md), [per-source contributions](../rend2-contributions-p06.md), [orchestration](../rend2-provider-orchestration.md).
