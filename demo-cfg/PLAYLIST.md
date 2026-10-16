# Точный плейлист CFG

Ниже — рекомендуемый порядок. Суффиксы `a/b` являются временным split-screen и
его выключением; после `b` основной эффект остаётся включён.

## 0. База

- `0.cfg` — выключает все новые технологии и делает один `vid_restart`.

## 1. Backend, изображение и материалы

- `01` OpenGL 4.3 backend; `02` GLSL cache; `03` material DDC.
- `04` AgX tone mapping (`04a/04b` split); `05` LUT (`05a/05b` split).
- `06` linear lighting; `07` physical PBR/light units.
- `08` AutoPBR; `09` auto roughness; `10` legacy shiny conversion.
- `11` perceptual roughness; `12` specular AA; `13` Burley diffuse.
- `14` GGX energy compensation (`14a/14b` energy split).
- `15` adaptive POM; `16` POM self-shadow; `17` silhouette POM;
  `18` automatic silhouette candidates.

## 2. Свет, тени, отражения и персонажи

- `19` Shadows 2.0; `20` PCSS; `21` GTAO (`21a/21b` split);
  `22` contact shadows.
- `23` Forward+; `24` spot lights; `25` cookies; `26` LTC area lights;
  `27` auto lamps; `28` saber line lights.
- `29` entity sun split; `30` shadowed model ambient; `31` GPU/L1 entity grid;
  `32` diffuse IBL.
- `33` auto-emissive; `34` HDR bloom; `35` SSR (`35a/35b` split);
  `36` SSGI (`36a/36b` split).
- `37` skin SSS (`37a/37b` split); `38/38a` hair/eye classification debug.

## 3. Volumetrics и небо

- `39` froxel fog; `40` height fog; `41` density noise.
- `42` directional static-light reconstruction; `43` recovered lights;
  `44` light portals.
- `45` volumetric FX; `46` lit sprites; `47` legacy FX physicalization;
  `48` volumetric emission.
- `49` media self-shadow; `50` multiple scattering; `51` RGB extinction.
- `52` atmosphere; `53` clouds; `54` cloud shadows.

## 4. Дождь и растительность

- `55` weather culling; `56` rain streaks; `57` rain dynamic light;
  `58` distance LOD; `59` rain volume.
- `60` wetness; `61` persistence; `62` puddles; `63` layered edges;
  `64` real ripples; `65` runoff; `66` trails; `67` impact splashes;
  `68` rain lens; `69` acid-rain profile.
- `70` auto foliage; `71` grass cards; `72` coherent wind;
  `73` leaf flutter; `74` frond bending; `75` character interaction.

## 5. Вода и финал

- `76` modern surface (`76a/76b` встроенный old/new split).
- `77` volumetric liquid; `78` Snell/TIR; `79` waves; `80` dispersion;
  `81` geometry; `82` interaction solver; `83` bathymetry.
- `84` flow; `85` spatial field; `86` wall impact; `87` splashes;
  `88` foam field; `89` intersection foam; `90` shoreline.
- `91` surface caustics; `92` underwater particles; `93` rain ripples;
  `94` wind response; `95` stream whitewater.
- `96` waterfall sheets; `97` spray/mist; `98` impact pool.
- `99` physical auto camera; `99a` shutter-coupled motion blur.

Не обязательно снимать все 99 пунктов отдельными сценами. Backend/loading,
debug-only и узкие dependency steps лучше собрать в короткие главы; наиболее
сильные самостоятельные A/B — `04`, `05`, `14`, `21`, `35`, `36`, `37`, `76`.
