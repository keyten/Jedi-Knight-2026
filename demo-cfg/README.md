# Видеодемо: порядок показа Rend2 overhaul

База сравнения — `788c19e97` (родитель первого авторского feature-коммита
`00b8a5703`). В first-parent истории от `00b8a5703` до текущего `HEAD` — 202
коммита: 199 от Keyten и 3 служебных/документационных от Dmitriy. Повторные
ревью, исправления и оптимизации объединены с технологией, которую они доводят
до текущего состояния.

## Как запускать

Скопировать эту папку в каталог игры, из которого доступны CFG, и начать с:

    exec demo-cfg/0.cfg

Затем выполнять файлы по имени. CFG накопительные: обычный файл включает только
одну технологию и не сбрасывает уже включённые. Файлы `*-split-on.cfg` и
`*-split-off.cfg` включают и выключают встроенное сравнение одной технологии.
После `0.cfg` и после файлов, содержащих latched-параметры, `vid_restart`
выполняется автоматически.

Для чистого before/after зафиксировать камеру, FOV, время суток и экспозицию.
Сначала снять кадр после `0.cfg`, затем не двигать камеру и выполнить следующий
CFG. Для temporal-эффектов дать истории стабилизироваться 1–2 секунды.

## Рекомендуемый монтаж

1. **Backend и загрузка** — OpenGL 4.3, GLSL cache, material DDC. Это короткая
   вставка с консолью/таймингами, не визуальный before/after.
2. **Изображение и физическая база** — tone mapping, LUT, linear lighting,
   physical PBR/light units, AutoPBR, roughness, specular AA, BRDF energy.
3. **Свет и тени** — Shadows 2.0, PCSS, GTAO, contact shadows, Forward+,
   spot/cookie/LTC lights, модельное освещение и entity sun split.
4. **Светящиеся материалы и отражения** — auto-emissive, HDR bloom, SSR, SSGI.
5. **Персонажи** — skin SSS, hair/eye materials, light-grid L1 probes.
6. **Объёмный свет** — froxel fog, height/noise, static-light reconstruction,
   light portals, volumetric particles, emission, self-shadowing, multiple
   scattering, RGB extinction.
7. **Дальний план** — atmosphere, volumetric clouds и cloud shadows.
8. **Погода** — rain streaks/LOD/light, wetness, puddles, real ripples, runoff,
   splashes, lens water, acid rain.
9. **Растительность** — auto classification, grass cards, wind, flutter,
   physical bend/contact.
10. **Вода** — modern surface (лучший встроенный split), volume/Snell, waves,
    geometry, interaction, flow, foam, shoreline, caustics, particles,
    whitewater и waterfalls.
11. **Камера** — physical exposure и motion blur как финальный cinematic shot.

## Что сгруппировано из истории

- Tone mapping/LUT/linear light: `00b8a5703`, `7bde8d012` и последующие
  physical-camera/PBR/light-unit/energy-conservation коммиты.
- AO/shadows/lighting: GTAO, contact shadows, CSM/PCSS, Forward+, LTC,
  spot/cookie lights, static-light reconstruction и последние entity-sun
  commits.
- Materials: emissive/auto-emissive, AutoPBR/convert, roughness variation,
  adaptive/self-shadow/silhouette POM, skin SSS, hair/eye and specular AA.
- Volumetrics: froxel rewrite, GL 4.3 compute path, local media, particles,
  emission, self-shadow, multiscatter, RGB extinction, rain volume, atmosphere
  and clouds.
- Weather/foliage: unified precipitation, persistent wetness, puddles, runoff,
  rain lens, physical splashes, wind and interaction.
- Water: the long October series is intentionally presented as one dependency
  ladder, from surface optics through geometry/solver/flow/foam/shoreline to
  waterfalls. Review/fix/optimization commits are represented by the final
  switches, not as separate demo stages.

## Сцены

- **Tone/PBR/emissive/bloom:** яркое небо + тёмный интерьер, лампы, сабер.
- **GTAO/contact/PCSS:** лестницы, тонкие перила, ноги персонажа, foliage.
- **Forward+/LTC/SSGI/SSR:** несколько саберов/выстрелов рядом со стеной и
  глянцевым полом.
- **Entity grid/static reconstruction:** `hoth3`, `vjun2`; сравнивать лицо и
  модель при переходе между освещёнными зонами.
- **Volumetrics/FX:** `t1_fatal` для smoke/steam; окно или дверь для shaft.
- **Water:** `t3_hevil` для течения/solver/foam; `yavin1` для берега и foliage.
- **Rain:** карта с реальным world-effect rain; держать одинаковую интенсивность
  осадков между дублями.

Color grading заметен только при наличии `maps/<map>.cube` либо собственного
`.cube`; CFG не подменяет художественный LUT. Hair/eye material models и часть
исправлений физического BRDF не имеют master-switch: для них подготовлены debug
CFG или они показываются внутри ближайшего зависимого блока.

Важно: `29` и `30` показываются на legacy entity grid, потому что sun split
требует `r_entityLightGrid 0`. Только затем `31` включает GPU/L1 grid. Флаг sun
split при этом не сбрасывается, но перестаёт быть применим — это сознательный
переход от одного варианта освещения моделей к следующему.

