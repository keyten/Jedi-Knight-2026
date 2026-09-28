# Jedi Academy 2026 renderer API naming

New renderer cvars use `r_<feature><Property>` in camelCase. Related controls
share a feature prefix. Enumerations use `Mode`, diagnostics use `Debug`, and
split comparisons use `Compare`. Ordinary commands use `r_<feature><Action>`.
Existing short command namespaces with subcommands remain supported.
Historical Rend2 names are unchanged.

Feature names at the beginning remain lowercase (`r_pbrDumpMaterials`,
`r_pomSilhouette`, `r_ssgiHalfRes`). Technical acronyms inside names use
`PBR`, `SSS`, `IBL`, `PCSS`, `LUT`, and `POM` consistently.

## Behavior changes

- `r_colorGrading` accepts only 0/1. Enable `r_colorGradingCompare 1` for the
  original/graded split while grading is enabled.
- `r_plantWind` accepts only 0/1. Set `r_plantWindStrength` (default 1, range
  0–4) separately to tune the bend amplitude.
- `r_weatherMaterialList` is a command that requests one rain rendering frame
  of drawn shaders with a non-default weather response or an exclusion.
  It stores no configuration state.
- Shader material keywords are `pomSilhouette`, `pomSilhouetteDistance`, and
  `pomSilhouetteSteps`.

## Families and semantic names

Volumetric controls share `r_volumetric`: `MultiScatter`, `Particles`,
`SelfShadowMaxLights`, `SelfShadowOutsideHeightFog`, and `FogRGBExtinction`.
Height fog uses `HeightOpaqueDistance`, `HeightMaxDensity`, and `HeightTopHeight`.
Lighting of particles uses `r_particleLighting`.

Spot light properties use `r_spotLight`: `Shadows`, `Cookies`, `CookieDebug`.
LTC lights use `r_ltcSaberAreaLights`, `r_ltcReloadLights`, `r_ltcExtractLights`,
`r_ltcList`, and `r_ltcNearest`. Forward+ uses `r_forwardPlusMaxShadowLights`.
Ordinary diagnostic commands also include `r_forwardPlusSpawnTestLights` and
`r_foliageInteractionList`.

Weather surface settings share `r_weather`: `Wetness*`, `Puddle*`, and `Runoff*`.
The combined diagnostic view is `r_weatherSurfaceDebug`. Height aware puddles
use `r_weatherPuddleUseHeightMap` and `r_weatherPuddleWaterLevelBias`.
Modern rain streaks use `r_rainStreakOpacity`, `r_rainStreakLighting`, and
`r_rainStreakDebug`; lens droplets use `r_rainLensDensity` and `r_rainLensDropSize`.

Grass geometry uses `r_grassCardMode`, `r_grassCardLodDist`, `r_grassCardWidth`,
and `r_grassCardDebug`. Persistent bends use `r_foliageBendField*`, including
`RecoveryTime` and the `Clear` command. Interaction limits use
`r_foliageInteractionMaxInteractors` and `r_foliageInteractionNPCs`.

Other clarified names include `r_aoDebug`, `r_ssgiHalfRes`,
`r_pomSelfShadowLightMode`, `r_ssrReceiverCull`, `r_ssrBlendStrength`,
`r_skinSSSMixedHeads`, `r_shaderProgramCache`, and `r_shaderProgramCacheMaxMB`.

Update custom configs and authored shaders to the new names. Renamed 2026 APIs
do not register legacy aliases. The bundled rendering menu and example configs
use the new names.
