# Анализ новой серии коммитов

## Граница сравнения

- Локальная ветка `rend2` (`1a6a64342`) является предком текущей ветки, но
  прямой диапазон `rend2..HEAD` содержит 414 коммитов: туда попадают старый
  SP/MP-unification и последующие upstream merge/fix серии.
- Авторский rendering overhaul начинается с `00b8a5703` (`Enhanced tone mapping
  + linear lighting`). Его родитель `788c19e97` — практическая база
  «оригинального rend2» для видеосравнения.
- Диапазон `00b8a5703^..HEAD` содержит 202 first-parent коммита: 199 Keyten и
  три Dmitriy (`e035589de`, `f8b39e5f9`, `da08edc14`).
- Даты серии: 21 сентября — 15 октября 2026. По дням:
  `3, 8, 15, 13, 15, 4, 15, 11, 5, 6, 18, 17, 5, 1, 3, 3, 5, 8, 4, 2, 12,
  13, 8, 8` коммитов соответственно.

Иными словами, правило «примерно один коммит на технологию» верно для первого
слоя (21–27 сентября), но не для итоговой ветки: крупные подсистемы затем имеют
review/fix/performance/validation серии. В демо они объединены с исходной
технологией.

## Классификация всей серии

### 1. Output, camera и единицы света

Начало: `00b8a5703` tone mapping/linear lighting, `7bde8d012` LUT. Финальная
физическая калибровка: `80bdb6aee` light units, `b4f550f70` camera,
`b40647933` emissive brightness. Сюда же относятся GGX/roughness/energy commits
`8d7b5dfa8`, `2b6089187`, `634c44d7c`, `266fc26c9`.

Показывать как одну причинную цепочку: linear scene values → physical material
response → exposure/tone map → bloom/LUT. Камеру лучше включать в финале, чтобы
она не меняла экспозицию всех предыдущих A/B.

### 2. Материалы

`0cd86d32b`/`d42770717` emissive, `0130fa618` AutoPBR, `6962fabb2` legacy
conversion, `0efa28bde` roughness variation, `55f6ffeb1` POM self-shadow,
`c287ee215` + `5252f61a9` + `97cc6083f` silhouette POM, `b1adafee7` SSS,
`2d2ffbf71` hair/eyes, `a92a55f18`/`8db425fb6` specular AA.

Hair/eye authored models and corrected GGX math do not all have a master off
switch. Их корректнее показывать материалом/diagnostic view, а не обещать
идеальный runtime rollback каждого математического fix.

### 3. Экранное освещение и отражения

`93fb54ce8` GTAO/contact, `9c1e850c4` + `52660944a` Forward+,
`d5614b7bf`/`bfb2ce4b5`/`a2d0046ff` SSR, `fc5f85258` SSGI,
`4d970397a` modern bloom, `a7f3ac51e` motion blur. Эти эффекты имеют самые
честные встроенные split modes и идут в начале визуальной части.

### 4. Тени и источники света

`5f55edd8b` CSM вырос в Shadows 2.0/PCSS и alpha-caster fixes
(`0e6149956`, `9a0024277`, последующая документация/валидация). LTC:
`bd149dc49`, `78a945e40`, review/calibration/shadow/saber commits конца сентября
и начала октября. Spot/cookie lights: `ffd5eea03`, `f6ee1fcb5`.
Character-shadow improvements culminate in `2e1f98f32` и `6f2c61fdb`.

### 5. Entity и static lighting

`e02f0fcc6` entity sampling, затем directional baked volumetric reconstruction
(`108198982`, `7e1ffc5e9`) и vertical slice `5d0a03b7e`; итоговый audit —
`05a91f7e9`. Light portals и calibrated LTC входят в ту же reconstruction
семью (`4abeb3550`). В демо сначала показывается legacy-grid sun split, затем
GPU/L1 grid: эти режимы альтернативны по реализации.

### 6. Volumetric pipeline и FX

Основа: `239d3ee7e` froxel fog. Далее height/noise/local volumes, directional
baked light, volumetric FX/sprite lighting (`835f98362`, `6b991a75d`), emission
(`22dacfd2e`), per-medium phase (`520838778`), self-shadow (`1ceb205c7`), RGB
extinction (`4789c8b27`). GL 4.3 compute path и fallback доведены серией
`8f8d4ba01`…`86b38e529` и `42651db6a`.

Legacy FX physicalization — серия `e1c37622a`, `000c7c28d`, `2efbdac4e`,
`5d2189a03` с отдельными audit/docs. Это не новый визуальный pass само по себе,
а автоматическое назначение уже существующих физических medium/emission ролей.

### 7. Atmosphere и clouds

`13fc62bdc` atmosphere/aerial perspective и `2455fe5e0` volumetric clouds.
Их лучше снимать на дальнем плане после локального fog, иначе зритель примет
аэрозольную перспективу за обычный height fog.

### 8. Rain, wetness и precipitation state

Начальные коммиты: weather culling, rain exposure/streaks/splashes/lens,
procedural puddles/runoff. Финальная связная система: `35d8e2d10` unified state,
`bb27fecf5`/`933cf8693` persistence, `17d11aa33` puddle edge, `1a0f16741`
surface response, `f701b2964` real ripples, `38abe870c` rain atmosphere,
`fab2de27b` dynamic lights, `0912ef1b2` distance LOD,
`af2cb2a18`/`43905578a` runoff trails, `f9886ff81` acid profile,
`ff3f92b42` rain-on-real-water.

### 9. Foliage

`a9556c9e5` classification, `5010f1e84` grass, `acdda3729` wind,
`c6600e7fc` flutter, `70283ae4c` interaction, `e6fc2c783` physical bend;
поздние `f53fecc92`, `08f805613`, `f9e45464a` доводят поведение. Снимать в
движении: статичный split плохо показывает coherence и сохранение bend state.

### 10. Water surface, volume и solver

`deddcdad2` modern surface, `c6e584ba4`/`d0740bcde` volume integration,
`316741c05` Snell/TIR. Water bodies/waves/geometry/flow/interaction проходят
через `d9905ca73`, `2c4b59b19`, `0b0abb643`, `9ce5856bb`, `b8654888c`,
`e83479dce`, `2ec1e172a`, `845474f72`, `4ff98bf0a`, `5600e7a25`,
`a783498dc`, `a7cbcdf07`, `5ec894e54`, `57f20d565` и review/fix commits до
`98ad4779f`.

Это одна dependency ladder, а не независимые флажки: surface → geometry →
interaction/bathymetry → flow → foam/shoreline → whitewater/waterfall.

### 11. Waterfalls

`d8c7c5cc8` modern sheets, `e51dc3e81` mist, `42d16544c` impact,
`dc61fc9a2` large-dam profile; whitewater commits находятся рядом с общей
water-серией. Видеоряд лучше завершать большим waterfall shot после того, как
зритель уже видел базовую воду.

### 12. Performance, loading, UI и исправления

GL 4.3/menu/progress/fallback: `8f8d4ba01`, `7a17eca96`, `1ec8e27ab`,
`678eed174`, `e2ac31374`. Shader cache: `afd083fa2`; material DDC:
`6ae1a6eb2`; map loading/profiling: commits 1–2 октября (`bf78d8ca9` through
`f44c6ce69`, включая несколько явно названных `profiling`). Water optimization
phases: `53632cd54`, `c3dc90dc7`, `fce104a8a`, `f5c0b095c`, `8a54ecb88`.

Эти изменения показывать цифрами/консолью, а не пытаться выдать за визуальный
эффект. Fix/review commits не получают отдельный CFG: они определяют текущее
качество соответствующей технологии. Rename/README/merge Дмитрия также не
являются renderer demo steps.

## Вывод для монтажа

Историю коммитов не стоит повторять хронологически. Сильная драматургия — от
понятного изображения и материалов к свету, затем к объёму/погоде и в финале к
самой большой связанной системе воды. Точный порядок и команды находятся в
`PLAYLIST.md`; зависимости и ограничения — в `README.md`.
