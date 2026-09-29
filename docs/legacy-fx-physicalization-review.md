# Legacy FX Physicalization: ревью и исследование

Дата: 2026-09-29. Статус: исследование и проект системы; первый runtime этап реализован.
Фактические команды, ограничения и принятые настройки описаны в
[документации первого этапа](legacy-fx-physicalization.md).

Предложение стоит принять с изменениями. Семантический слой над Raven EFX хорошо подходит существующему движку: художественные primitives продолжают работать, а выбранные дымовые primitives получают физические proxies. Основные изменения: анализировать варианты shader внутри primitive, разделить готовый template и состояние конкретного emitter, калибровать оптическую толщину вместо назначения произвольной постоянной плотности и отложить автоматическое агрегирование до появления контроля источников.

Пользователь подтвердил: сохранить характер оригинальных эффектов; неизвестные EFX обрабатывать только при высокой уверенности. Для автоматической среды приняты четыре режима: off, on, только opt-in, on кроме opt-out. Остальные инженерные решения ниже приняты в рамках первого этапа либо оставлены для следующих этапов.

## Что исследовано

- Оба JA FX пути: SP `code/cgame/Fx*`, MP `codemp/client/Fx*`; вызовы из game/cgame и `fx_runner`.
- Общий renderer: particle/local media, emission, material phase, sprite lighting, temporal history, лимиты и optional exports.
- Чистый corpus из `C:/Users/Keyten/Desktop/projects/OpenJK/build-rend2/base/assets0–3.pk3`.
- Отдельный PK3-only overlay всех 12 пакетов в той же `base`, включая PBR-паки, GGDynamicWeapons и тестовые overrides.
- Все EFX, соответствующие shader definitions и stages, nested effect graph; entity lumps всех BSP; потенциальные effect refs из строк JA SP/MP исходников.
- Характеристики 171 texture-map references из shader stages; визуальный просмотр 18 representative textures. Это не просмотр каждого кадра всех анимаций и не проверка картинки в игре.
- Первичные материалы PBRT и доклад Wronski о volumetric fog. Они подтверждают модель переноса, но не дают готового классификатора Raven art.

Инструмент: [`tools/fx/audit_stock_fx.py`](../tools/fx/audit_stock_fx.py). Полные локальные отчёты: [`stock/inventory.md`](../build/fx-audit/stock/inventory.md), [`stock/inventory.json`](../build/fx-audit/stock/inventory.json), [`installed/inventory.json`](../build/fx-audit/installed/inventory.json). Контактный лист: [`texture-contact-sheet.png`](../build/fx-audit/stock/texture-contact-sheet.png).

Архивы читаются напрямую, без распаковки коммерческих ассетов в repository. Производные отчёты находятся в ignored `build/`; в исходники добавлены только инструмент, синтетические тесты и это исследование.

## Реальные размеры corpus

| Метрика | Чистый stock | Overlay установленной base |
|---|---:|---:|
| EFX-файлы | 376 | 379 |
| Все primitive groups в текстах | 1528 | 1568 |
| Поддерживаемые текущим JA FX parser primitives | 1503 | 1545 |
| Particle / OrientedParticle | 688 / 72 | 694 / 72 |
| Light | 72 | 106 |
| Неисполняемые ForceFeedback groups | 25 | 23 |
| Shader names после снятия расширений | 140 | 140 |
| Прочитанные BSP | 57 | 59 |
| Размещения fx_runner | 1184 | 1186 |
| Разные effect refs в fx_runner | 79 | 79 |
| Ошибки syntax reader | 0 | 0 |

Все 376 stock EFX здесь приходят из `assets1.pk3`. `assets3.pk3` переопределяет четыре MP BSP, что учтено при подсчёте карт. Все 140 referenced shader names разрешились, как и все 171 texture-map reference после fallback `.tga → .jpg` и других расширений. Число 171 относится к именам map references, а не обязательно к уникальным image files.

40 существующих EFX отличаются в overlay: 37 из GGDynamicWeapons и 3 из `zz_volumetric_media_test.pk3`. GGDynamicWeapons добавляет ещё 3 EFX. Поэтому exact profile, сопоставленный только с путём stock файла, уже небезопасен для этой установки.

Частота размещений на картах:

| Effect | fx_runner | Карты | Приоритет |
|---|---:|---:|---|
| env/small_fire | 405 | 7 | smoke + flame; главный environmental regression case |
| env/small_fire_LOD | 169 | 1 | тот же набор ролей, отдельная проверка профиля |
| chunks/dustfall | 74 | — | отделить облако от отдельных пылинок |
| env/powerbolt | 46 | — | отрицательный контроль: energy, не fog |
| explosions/hugeexplosion1 | 46 | — | OrangeGlow и LingeringSmoke требуют разных ролей |
| env/waterfall_mist | 43 | 4 | большой mist proxy, overlap и perf |
| volumetric/steam_jet | 27 | — | Particle + Tail; одной Particle эвристики недостаточно |
| env/fire_wall | 26 | — | много flame/smoke частиц |
| volumetric/large_steam | 20 | — | кандидат на последующее агрегирование |
| volumetric/pressurized_steam | 16 | — | плотный повторяемый emitter |

574 размещения двух small_fire вариантов — 48.5% всех найденных fx_runner. Это **placement frequency**, а не доля реально активных FX или стоимость кадра. Например у 402 из 405 small_fire установлен DAMAGE flag; другие эффекты могут быть STARTOFF/ONESHOT и активироваться по сценарию. 1078 из 1184 runners не задают delay явно, что в SP означает default 200 ms. Проверять нужно отдельно SP/MP semantics.

В строках JA SP/MP исходников найдено 214 потенциальных effect roots; объединение с map roots и nested graph даёт 296 достижимых файлов. Строка в исходнике может находиться в комментарии или быть preload, а пути из конфигов, скриптов и динамически собранных строк могут отсутствовать в выборке. Остальные 80 файлов нельзя объявлять неиспользуемыми.

Найдены две отсутствующие nested ссылки: `effects/black_smoke.efx` и `effects/smoke.efx` из `sparks/spark_explosion / playfx`. Это данные corpus, не повод исправлять их автоматически или считать graph полностью замкнутым.

## Где исходное предложение требует исправления

### 1. Primitive — ещё не минимальная единица классификации

`CMediaHandles::GetHandle()` случайно выбирает один shader. В stock есть 22 sprite primitives, для которых простые vocabulary rules дают разные роли вариантам. Часть различий — недостаток словаря, часть требует отдельной проверки. Например `black_smoke2` смешивает `alpha_smoke`, `alpha_smoke2`, `black_smoke2`; это всё smoke, но с разным visual response. Есть и смеси bubble/flare.

Нужны profiles на **primitive + media alternative**, выбранные после той же RNG выборки, которая выбирает legacy shader. Нельзя второй раз вызывать `GetHandle()` для medium или отдельно случайно выбирать материал. Если все alternatives совместимы, допустим один общий profile.

410 из 760 sprite primitives вообще не имеют name. В 21 effect повторяются непустые имена primitive groups. Selector только по `mName` недостаточен. Для unnamed/duplicate cases нужны source ordinal, primitive type, expected shader set и fingerprint значимых полей. Exact rule должен отказываться от несовпадающего содержимого.

### 2. Shader с именем steam — не гарантия дыма

`gfx/misc/steam` встречается в 132 sprite primitives. В `rocket/explosion / LingeringSmoke` это дым; в `explosions/explosion1 / OrangeGlow` `steam + steam2` используются для оранжевого glow. Та же семья есть в sparks, saber impacts, muzzle flashes, lava, blood-like impacts и mist.

Поэтому `gfx/misc/steam* → smoke` нельзя включать как общее положительное правило. Steam family сообщает форму mask, а семантику задают effect/primitive context, кривые, цвет и exact profile. Additive blend сам по себе не доказывает emission participating medium.

Соседний Light может усиливать гипотезу fireball, но не превращает соседний steam puff в emissive fog: это часто LingeringSmoke после взрыва.

### 3. Пыль бывает отдельными частицами

Визуально `gfx/misc/dotfill_a` — sparse points; `gfx/misc/dust` — sparse streaks. В `rocket/explosion / Dust` и `chunks/dustfall / Dust` первый shader используется для отдельных частиц. В `bespin/dust` второй летит со скоростью 700–1400 units/s; исходный size 48–72 не означает заполненный дымовой шар такого радиуса.

У `chunks/dustfall` настоящий облачный puff называется **OrangeGlow** и использует `alpha_smoke`. Значит и положительные, и отрицательные name-only правила будут ошибаться. Dust-cloud и dust-grains должны быть разными ролями. Эти два point/streak shader по умолчанию не создают medium.

### 4. Growth/fade не являются обязательными признаками среды

`small_fire / Smoke` уменьшается с size 10–12 до 3–4. `fire_wall` black smoke тоже уменьшается. `noghri_stick/gas_cloud` имеет wave size; alpha wave бывает и у `bespin/dust`. Требование «растёт + долго живёт + линейно исчезает» потеряет настоящие среды.

Нужно читать flags/parm кривых, aliases `shader/shaders`, `vel/velocity`, `accel/acceleration`, `width/size`, а не только start/end. Reverse ranges встречаются в реальных данных; интервал нельзя интерпретировать как временную кривую.

### 5. Parser не знает, постоянный ли emitter

`count` — число spawns **на один вызов**; primitive `delay` — задержка этих spawns, а не период источника. Top-level `repeatDelay` используется scheduler looping, но не описывает частоту всех map/game callers. `.efx density` — расстояние между nested emitFx вдоль пути, не физическая плотность и не обычный spawn rate.

SP map `fx_runner` обычно генерирует EV_PLAY_EFFECT каждые `delay + random`, с STARTOFF/ONESHOT, движением и target orientation. Например комментарий в black_smoke рекомендует вызов раз в 100 ms, но map default — 200 ms. При count≈4.5 и life≈1 s получается соответственно около 45 или 22.5 живых частиц на источник. Одного template для выбора aggregate недостаточно.

При известных вызовах и стационарном режиме оценка:

```text
lambda = E[count_per_call] / E[call_interval_seconds]
E[N_alive] ≈ lambda * E[lifetime_seconds]
```

Рабочие оценки при SP map default 200 ms: `large_steam` ≈18.75 частиц, `pressurized_steam` ≈45, `waterfall_mist` ≈11, small_fire smoke ≈4.75. Они не учитывают startup, выключение, visibility, FX quality scaling и удаление частиц; это не measurements.

### 6. Локальные объёмы ещё не подключены к FX helper

Renderer export `GetRefFogVolumeAPI` существует, но в текущих SP/MP clients нет FX submission bridge для него: поиск показывает renderer exports и renderer-local callers. Для aggregate придётся добавить MP lookup/helper и SP cgame trap/engine forwarding, с безопасной обработкой отсутствующего export. Это дополнительная интеграция, а не просто заполнение существующих particle fields.

### 7. Не все заявленные параметры уже доступны

Per-particle albedo, anisotropy и emission есть. Particle ellipsoid имеет world-aligned aspect, без arbitrary axis; local volume ориентируемый. Density noise сейчас применяется к local/BSP/height media, а не к particle density. Independent per-archetype particle noise controls потребуют расширения renderer/API либо остаются отложенными.

Sprite lighting override сейчас задаётся shader-wide `particleLighting`, а не per EFX primitive. Один shader переиспользуется в разных ролях, поэтому менять shared shader ради одного profile нельзя. В v1 visual response = PreserveExistingClassifier. Per-instance overrides потребуют отдельного транспортного поля, а не перегрузки refEntity flags.

### 8. Результат не будет восстановлением физических свойств из EFX

Цвет sprite — authored RGB, а не measured scattering albedo. Alpha additive texture не задаёт attenuation; alpha-blended texture задаёт экранную mask, а не распределение вещества. Ни `g`, ни абсолютную sigma_t, ни emission радианс нельзя однозначно восстановить из этих файлов.

Это material **archetypes с художественной калибровкой**, а не автоматическое физическое измерение. Нельзя присваивать confidence 0.99 только потому, что сработал набор substring tests.

## Архитектура, которую стоит реализовать

```text
EFX parsing + canonical asset names + curve/range descriptors
    ↓
Pure AnalyzeEffect (whole-effect context, no random sampling)
    ↓
Immutable profiles per primitive/media alternative
    ↓
Legacy spawn: choose existing media once, sample existing fields once
    ↓
Resolved profile + actual spawned particle state
    ├─ legacy sprite / current sprite lighting
    ├─ optional particle medium
    └─ original Light / explicit spot

Phase 2 only:
source context + particle birth/death/motion records
    ↓
EmitterState → aggregate local medium, with one density owner
```

Общий независимый policy module и таблица rules должны использоваться обоими JA путями. SP `ParseEffect` находится в `code/cgame/FxScheduler.cpp:506`, MP — `codemp/client/FxScheduler.cpp:404`. Анализ запускается после всех groups, но не делает GPU state queries и texture decoding каждый кадр.

Canonical effect path: lowercase, slash normalization, `effects/` prefix, `.efx` suffix. Shader lookup снимает image extension, как renderer. Не менять заодно scheduler cache keys: это отдельный compatibility change.

Важно: `SEffectTemplate` сейчас сбрасывается `memset`, в том числе при выделении slot. Нельзя просто добавить туда `std::vector<std::string>` или другой nontrivial owned object. Предпочтительно хранить POD descriptor/profile ids в template и владеть строками/профилями в отдельном registry, очищаемом вместе со scheduler. Alternative — сначала исправить lifetime всех template slots.

Новые fields копируются в SP/MP `CPrimitiveTemplate::operator=` и effect copies. Dynamic template edits после копирования должны инвалидировать derived parameters либо использовать actual spawned size/life/alpha; нельзя продолжать пользоваться stale profile numbers.

### Схема профиля

Ниже эскиз, а не обещание готовых C++ типов/API:

```cpp
struct FxPhysicalProfile {
    FxMaterialRole material;       // smoke_dark, smoke_light, mist, dust_cloud,
                                   // gas, flame, fireball, grain, glow, unknown
    FxMediumPolicy medium;         // none, particle; aggregate in phase 2
    FxVisualPolicy visual;         // preserve; detail_overlay/replace later
    FxSpriteLightPolicy lighting;  // preserve current classifier in v1
    FxLightPolicy light;           // preserve point / explicit spot

    FxProfileSource source;        // explicit, exact, reviewed_family, heuristic
    FxEvidenceTier evidence;       // validated, candidate, ambiguous
    uint32_t ruleId;               // explanation / debug
    uint32_t authoredMask;         // explicitly authored domains/fields

    FxDensityMode densityMode;     // authored sigma or calibrated optical depth
    float opticalDepthAtFullFade;
    float albedo[3];               // renderer's documented color convention
    float anisotropy;
    float radiusScale;
    float aspect[3];
    float softness;
    float emissionPerUnit[3];      // independent from sigma_t
};

// Stored separately from immutable templates, phase 2:
struct FxEmitterState {
    FxSourceId source;             // owner + generation + attachment/view
    FxProfileId profile;
    FxDensityOwner representation;
    // transform/history, accepted spawn events, live particle records,
    // warm-up, stop/drain, budget selection, stable medium id
};
```

### Приоритеты

1. Явно заданная EFX metadata в соответствующем domain, включая explicit disable.
2. Проверенный exact effect/primitive/media profile с content signature.
3. Проверенная shader family + подходящий effect/structural context.
4. Общие осторожные rules; ambiguous → legacy.

Explicit medium и explicit light защищаются независимо. Если существует `volumetricMedia`, автоматика не дозаполняет его «улучшениями» из stock profile. Даже `extinction 0` может означать намеренное отсутствие extinction или pure emission; presence/authoring mask важнее boolean «сейчас плотность > 0». Нужен explicit opt-out для эффектов без volumetricMedia, например будущая отдельная policy group с `medium off`; окончательный синтаксис фиксируется при реализации.

Exact selector: canonical path + primitive type + уникальное имя либо source ordinal + expected shader alternative + relevant-content fingerprint. Для stock override с несовпадающим fingerprint — отказ от exact mapping и переход к разрешённой generic policy. Изменения GGDynamicWeapons нельзя маскировать совпадением пути.

Texture analysis делать offline и использовать для audit и vocabulary. Необходимость shipping metrics cache для модов не доказана; в v1 runtime texture readback/CV отсутствуют.

### Сохранение RNG и поведения

Analyzer читает `GetMin/GetMax`, не `GetVal()`. Spawn profile использует уже выбранный shader и уже sampled legacy values. Даже дополнительный вызов RNG только ради sigma variation меняет последующие legacy spawns, поэтому variation нужно либо убрать, либо получить из отдельного deterministic hash/PRNG, не трогая legacy random stream.

Сначала сохранить существующие размеры, кривые, сроки жизни, culling и lights. Near-camera media submission уже есть; depthHack/playerView/portal ограничения должны соответствовать имеющимся SP/MP путям. Все CreateEffect overloads должны проходить общий attach-profile helper: в SP уже есть `FX_SetVolumetricMedia`, вызываемый из двух путей, в MP блок прикрепления inline.

Direct `FX_AddParticle` / refEntity calls вне templates не получают semantic context автоматически. Большая часть исследованных stock EFX действительно проходит scheduler, но утверждение «любые FX вообще» слишком широкое. Для таких callers нужен explicit profile либо отдельный bounded API; renderer name guessing не добавлять.

## Калибровка density и emission

PBRT описывает transmittance через интеграл extinction и Beer–Lambert law. Поэтому сначала выбрать целевую optical depth, затем вычислить sigma_t для proxy размера, а не назначать одинаковую sigma для puff 4 и 128 units. [PBRT: Transmittance](https://www.pbr-book.org/4ed/Volume_Scattering/Transmittance)

Для сферического proxy с имеющимся smoothstep edge центральная эффективная длина:

```text
L_eff = 2 * radius * (1 - softness/2)
sigma_auto(t) = tau_profile * clamp(actual_alpha_fade(t), 0, 1) / max(L_eff(t), epsilon)
```

Это предлагает сохранить характер opacity при изменении размера. Для ellipsoid нужна явная выбранная reference длина; не подгонять sigma под направление текущей камеры, иначе физическая среда изменяется при повороте взгляда. Для v1 auto proxies разумно начать со сферы. Это новый auto density mode: существующий explicit `sigma * alpha` сохраняется и не переписывается.

Размер брать из actual `UpdateSize`, alpha — из actual `UpdateAlpha`, после flags/parm/random/clamp logic. Если переводить opacity в optical depth, `tau = -ln(1-opacity)` имеет смысл только для выбранной calibrated effective opacity, а не слепо для EFX alpha. Texture mask, blendFunc, authored RGB и useAlpha меняют visual response.

При Preserve для alpha-blended дыма attenuation sprite и proxy складываются. Следовательно целевая tau_auto должна быть лишь небольшой добавкой, проверенной A/B. Для additive smoke существующей alpha attenuation нет, но сохранённая additive яркость всё равно может исказить результат. Один коэффициент для этих двух случаев применять нельзя.

Стартовые optical-depth, albedo/g и emission numbers фиксировать после тестовых кадров representative profiles. Существующие ручные extinction ranges полезны как reference, но не доказывают универсальность. Archetype defaults — smoke_dark / smoke_light / mist / dust_cloud / gas; `flame` и `fireball` первоначально имеют medium off до отдельной emission calibration.

Albedo и phase описывают scattering; цветной gas не обязательно поглощает комплементарные цвета. В существующем particle API нет RGB extinction поля, а local API его уже имеет. В v1 gas можно окрасить scattering albedo; добавление colored absorption не выводить из green RGB автоматически. [PBRT: Volume Scattering Processes](https://pbr-book.org/4ed/Volume_Scattering/Volume_Scattering_Processes)

Emission интегрируется как отдельный source `j`, поддерживается и при нулевой extinction. Она не освещает стены/модели. Существующий EFX Light остаётся источником освещения. Не создавать новый light на каждую flame particle: это изменит художественную яркость, нагрузку на shadows и существующие lights.

Per-medium `g` и albedo остаются входом renderer; self-shadow/MS не требуют semantic toggles. Однако в реализации MS зависит также от включённого self-shadow mode, а оба эффекта имеют глобальные quality controls. Нельзя обещать их автоматическое включение просто по появлению smoke.

## Агрегирование: отдельный второй этап

Предложенный один ellipsoid на emitter — полезная аппроксимация, но не общая замена. Он может заполнить промежутки между редкими puffs, смешать независимые источники, выйти за стену и перестать совпадать с residual дымом после выключения источника.

Не объединять по `(effectName, nearestPosition)` и не считать объём вечным после первого `PlayEffect`. Требуется stable source identity с generation, world/view/portal scope и attachment; одноразовый burst получает собственный id. Передавать контекст из EV_PLAY_EFFECT и других callers, сохранять через scheduling и nested spawns. Unknown context → per-particle.

Для начала агрегировать только проверенные стационарные environmental sources, с достаточным overlap, совместимым материалом и простой кинематикой. Не брать bolted/relative, physics/bounce, moving trails и view-model эффекты. Startup наполняется по реально accepted spawns; shutdown перестаёт добавлять births, затем medium доживает до expiry последних records. Teleport, entity reuse, map restart, load/save и source destruction сбрасывают/инвалидируют соответствующее состояние.

Trajectory envelope должен учитывать spawn variance, lifetime range, size curve, coordinate flags и gravity по world Z. Для `p(t)=p0+v*t+0.5*a*t²` проверяются не только endpoints, но и extrema `t=-v/a` внутри lifetime. Фактический particle motion использует пошаговое обновление скорости/позиции, а late-time correction — отдельный legacy путь; аналитическая формула является approximation. Не исправлять motion bugs в рамках physicalization.

Лучше сохранить лёгкие records реальных particle proxies и вычислять extinction-weighted moments по живым частицам. Один ellipsoid допустим только при близком материале и хорошей заполненности; несколько compact groups обычно безопаснее огромного общего envelope. Broad static plume без collision-aware границ может fog-ить соседнюю комнату; такие profiles требуют проверки или сохраняют particles.

При смене представления сохранять интегральную extinction и emission:

```text
M_t = sum_i sigma_i * integral(shape_i dV)
sigma_aggregate = M_t / integral(shape_aggregate dV)
j_aggregate аналогично
albedo — sigma-weighted; g — scattering-weighted approximation
```

Один g не воспроизводит смесь разных phase functions точно; несовместимые материалы не смешивать. Эти moments не гарантируют одинаковую картинку, поэтому переход требует визуальной проверки.

**Density owner ровно один:** per-particle либо aggregate. Hybrid может означать legacy sprites + aggregate, но не полную плотность обоих proxies сразу. Для перехода допустим bounded crossfade с complementary weights и hysteresis. Stable ids сохраняются независимо от GPU sorting; renderer должен видеть removal, а не просто исчезновение без history transition.

Local-volume API принимает `depthForOpaque`, particle — sigma_t. В renderer коэффициент local extinction равен `-ln(1.5/255) / depthForOpaque` до density scales. При bridge переводить единицы и учитывать разные глобальные scale controls, иначе смена representation сама изменит плотность.

## Бюджет и качество

Текущие лимиты:

| Ресурс | Submission cap | GPU cap |
|---|---:|---:|
| Particle media | 1024 | 128 |
| Emissive particles среди выбранных media | — | 24 |
| Local volumes | 256 | 64 |

Particle slice-index pool — 2560; local volumes в текущем рабочем дереве используют XYZ membership masks, а slice lists служат CPU diagnostics. Это важное отличие от старого описания slice-only local culling. Local и particle UBO уже ограничены budget; добавлять arbitrary profile arrays без подсчёта layout нельзя.

Текущая particle selection importance основана на extinction footprint. Pure emissive particles могут иметь нулевую extinction и проигрывать другим кандидатам; emission slot cap — ещё один ограничитель. Перед широкой auto emission нужен отдельный audit selection importance и бюджета, иначе некоторые огненные proxies будут подаваться, но не показываться.

Использовать существующий deterministic renderer culling. В дальнейшем engine budget выделять explicit media прежде auto profiles и не давать auto emitters вытеснять authored env volumes; для этого потребуется источник/priority metadata или upstream quotas. Плотность выбранных survivors не умножать на долю выпавших: это породит яркостные скачки.

Снижение множества particle proxies до одного объёма не гарантирует снижение GPU time: большой envelope затрагивает больше froxels, включает lighting/self-shadow в пустых местах. Нужны измерения CPU/GPU и occupied-cell cost. Переход LOD по одной distance без hysteresis и density matching откладывается.

Froxel rendering хорошо объединяет medium и lights, но хранит низкочастотную информацию; это объясняет сохранение текстурных деталей в sprites и ограничение Replace. [Wronski, SIGGRAPH 2014](https://www.realtimerendering.com/advances/s2014/wronski/bwronski_volumetric_fog_siggraph2014.pdf)

## Первый набор profiles

| Asset / selector | Семантика | Решение |
|---|---|---|
| volumetric/black_smoke, droid_smoke | dark smoke | particle; existing manual blocks — приоритет |
| volumetric/black_smoke2 | smoke, разные alternatives | profiles per chosen shader |
| rocket/explosion / LingeringSmoke | light smoke | particle; additive sprite сохраняется |
| thermal/explosion / LingeringSmoke | light smoke | отдельный exact profile |
| explosions/explosion1 / OrangeGlow | fire/glow из steam textures | medium off до emission calibration |
| explosions/explosion1 / LingeringSmoke | smoke | particle |
| noghri_stick/gas_cloud / Wcloud alternative | colored gas | particle; без auto emission/colored extinction |
| noghri_stick/gas_cloud / fxflare | sparkle | medium off |
| env/small_fire и small_fire_LOD / smoke group | warm smoke | particle, preserve alpha sprite, low supplemental tau |
| env/small_fire / flame groups | flame | сохранить sprites; emission — следующий подэтап |
| env/small_fire / sparks | sparks | medium off |
| env/fire / steam group | smoke от огня | particle, а не автоматический steam material |
| env/fire_wall / black_smoke2 | smoke | particle, perf stress case |
| chunks/dustfall / alpha_smoke group OrangeGlow | dust cloud | particle |
| chunks/dustfall / Dust, rocket/explosion / Dust | dust grains | medium off |
| bespin/dust / gfx/misc/dust | moving dust streaks | medium off |
| env/waterfall_mist | mist | particle initially; large-radius/overlap audit |
| volumetric/large_steam, pressurized_steam | steam | particle first; aggregate candidate phase 2 |
| volumetric/steam_jet / HeadFlare, snowpuff2 | jet mist candidate | exact review; name Flare не определяет роль |
| volumetric/steam_jet / Exhaust Tail | elongated jet | legacy first; explicit aggregate profile phase 2 |
| saber/force/energy/muzzle glow | emissive visual detail | medium off без exact approval |
| Light primitives | point light | preserve; spot/cookie только explicit или отдельно verified profile |

Это review selectors, не shipping runtime table. Значения medium не проставлены автоматически в stock .efx. Полноценная stock база должна содержать проверенные positive и negative decisions, причины и ожидаемые fingerprints.

## Метрики, проверки и последовательность реализации

Текущий vocabulary pass выдаёт 104 particle-medium **кандидата** среди 760 sprite primitives, 60 flame/fireball кандидатов, 237 negative/detail кандидатов и 359 mixed/ambiguous/unclassified. Это не измеренная precision/recall и не 99% coverage. `none` в JSON для непроверенного fire означает отсутствие автоматически разрешённого proxy, а не отсутствие физического потенциала.

Сначала сделать manual ground truth для приоритетных effects и выборки отрицательных controls. Считать отдельно: correct medium positives, false positives, false negatives, semantic correctness, exact signature mismatches, conservative fallbacks и число проверенных profiles. Map placement weighting полезно для очереди проверки, но для runtime weighting нужен capture counters по `(effect, primitive, shader alternative, source kind)`.

1. **Этот этап завершён:** воспроизводимый corpus reader, stock/overlay сравнение, texture metrics, map/source graph, исследование ограничений. Шесть синтетических regression tests проходят. Они проверяют lists/groups и пустые shader directives, malformed input, overlay, extension normalization/fallback, BSP bounds, mixed roles и explicit precedence.
2. **Runtime MVP:** shared policy module; canonical descriptors; fingerprints; explicit precedence/opt-out; exact reviewed smoke/mist/dust-cloud/gas profiles; per-alternative resolution; optical-depth density mode; debug explanation и counters. Никакого aggregate, runtime CV, auto spots/cookies или широкого auto emission. Начать с diagnostic/opt-in mode до визуальной приёмки.
3. **Emission subphase:** отдельно calibrate flame/fireball exact profiles и emission-aware selection. Сохранить lights; измерить HDR/bloom и emissive cap behavior.
4. **Source context:** stable owner/generation through callers/scheduling/nested FX; birth/death ledger; MP/SP fog-volume bridges; startup/drain/save/load/reset handling.
5. **Aggregate/LOD:** reviewed stationary sources, bounded groups, density/emission matching, wall leakage tests, temporal crossfade, budget arbitration. Только затем расширять unknown-content heuristics.

Проверки MVP: off-mode сохраняет legacy spawn/RNG; explicit zero/pure emission не перетираются; mixed alternatives согласованы; near camera, portal, view-model и FX copy paths; изменённые GGDynamicWeapons файлы не получают stale exact profiles; vanilla renderer без optional export работает. SP и MP build/runtime проверять раздельно.

Визуальная приёмка: rocket/thermal smoke, small_fire smoke/sparks/flame, chunks/dustfall, gas+sparkle, steam_jet, waterfall_mist; saber/energy отрицательные controls. Проверять в темноте/на свету, рядом с saber/dlight, при движении камеры, startup/stop и mass spawns. Отдельные A/B для sprite-only, proxy-only debug и combined image выявляют double attenuation и glow amplification.

Perf capture: CPU classify/register отдельно от frame submit/build; GPU inject/self-shadow/MS; submitted/culled/capped/uploaded counts и emissive slots, количество занятых froxels; representative stationary camera и camera movement при одинаковых quality settings. Пока игра с этой системой не запускалась, performance и итоговая картинка не измерены.

## Принятые инженерные решения

- Сохранить EFX scheduler и renderer integration; classification перед spawn, physical response по actual live state.
- Разделить material, representation, visual policy и light policy; добавить отдельные provenance/evidence/authoring domains.
- Использовать rules + guarded exact profiles; не ML и не per-frame texture analysis.
- Preserve legacy sprites и текущий sprite-light classifier в v1; point lights сохраняются.
- Не обещать автоматическое восстановление physical constants, 99% coverage или universal aggregate.
- Агрегирование, per-instance lighting overrides, particle noise, широкая emission и automatic spot/cookie — самостоятельные последующие этапы.

Важные продуктовые решения остаются за пользователем: насколько сильно менять original art и насколько активно обрабатывать unknown/modded EFX. Все исследования и предложенный MVP выполнены при консервативных допущениях, без изменения игрового renderer или ассетов.
