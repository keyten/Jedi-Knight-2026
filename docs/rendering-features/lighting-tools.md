# Offline lighting tools

[All rendering features](../rendering-features.md)

Controls marked `vid_restart` need a renderer restart. Debug views use 0 to return to normal unless stated otherwise. Performance labels are rough relative costs, not benchmark guarantees.

## [For mod authors] Offline lighting reconstruction (experimental)

*Requires captured original BSP observations and the separate offline tools, because normal map loading does not run the fitting pipeline.*

Studies where lights might have been when the map’s baked lighting was made. It can fit and export candidate lighting data offline, but currently does not relight normal gameplay. Preview markers are inspection aids, not new lamps. Potentially heavy offline computation, with no added gameplay rendering pass.

**Main controls**

- `r_lightReconBuild 0 | 1 | 2 | 3` — Off / cache only / cache or build / force build. Default 0; sampled at the next main-world map load. In-game policies 2/3 prepare source observations, not automatic fitted relighting.
- `r_lightReconUse 0 | 1` — Master request for future reconstructed-lighting consumers; default 0. The normal renderer has no active consumer adapters.
- `r_lightReconFog, r_lightReconEntities, r_lightReconParticles` — Independent requests for fog, character and particle consumers; all default 0.
- `r_lightReconSpecular, r_lightReconIndirect, r_lightReconWorldDiffuse` — Independent reflection, indirect and direct-world requests; all default 0.
- `r_lightReconShadows, r_lightReconSun, r_lightReconArea, r_lightReconEnvironment` — Independent shadow, sun, area-light and environment requests; all default 0.

**Debug**

- `r_lightReconEvidence` — Inspect classified emitter evidence and confidence.
- `r_lightReconExport` — Export a bounded original-observation dataset for offline analysis.
- `r_lightReconInfo` — Print world asset/build/consumer availability without starting a build.
- `r_lightReconLuxels` — Inspect original baked luxel-to-geometry mappings.
- `r_lightReconObservations` — Inspect preserved original BSP observations.
- `r_lightReconPreview <safe-name> | off` — Show data-only markers from `lightrecon-preview/<safe-name>.txt`. Requires `r_lightReconBuild 2/3` source capture and a matching BSP hash; it never runs a fit or adds runtime lights.

Use `r_lightReconExport` to save original observations for the offline tools. The offline tools can check and save fitted assets, while unknown calibration or geometry stays explicitly unknown. Use the [producer guide](../rend2-producer-s13.md), [stock-fit results](../rend2-stock-joint-fits-x12.md) and [contribution guide](../rend2-contributions-p06.md) for the research workflow.

Since the original audit, the CPU tools gained sun/sky estimation, hidden-source proposals, light-shape and intensity fitting, shadow checks, bounced-light fitting, uncertainty checks and bounded scene-wide scheduling. Later work also preserves per-source direct/indirect contributions. These are hypotheses from limited baked evidence: current stock-map attempts do not establish reliable original physical lights. [Details](../rend2-provider-orchestration.md).
