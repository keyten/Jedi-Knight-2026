# Legacy FX Physicalization: первый этап

Система добавляет мягкую объёмную среду к выбранным Particle / OrientedParticle
в SP и MP. Оригинальные спрайты, динамические источники света, траектории,
сроки жизни и последовательность RNG сохраняются. По умолчанию автоматика выключена.

## Управление из консоли

Для отображения среды нужен rend2 с поддержкой froxel fog и particle media:

```text
set r_volumetricFog 2
set r_volumetricParticles 1
set fx_physicalization 2
set fx_physicalizationOptIn "volumetric/black_smoke;rocket/explosion"
```

| `fx_physicalization` | Автоматическая среда |
|---|---|
| 0 | Всё выключено |
| 1 | Всё включено; оба списка игнорируются |
| 2 | Выключено, кроме эффектов из opt-in |
| 3 | Включено, кроме эффектов из opt-out |

«Всё» означает эффекты, которым классификатор разрешил автоматическую среду.
Ни один режим и ни один список не заставляют неизвестный эффект пройти
классификацию. Значения режима вне 0–3 выключают автоматику.

Пример исключений при общем включении:

```text
set fx_physicalization 3
set fx_physicalizationOptOut "rocket/explosion;thermal/explosion"
```

Списки заменяются целиком. Пустая строка очищает список:

```text
set fx_physicalizationOptIn ""
set fx_physicalizationOptOut ""
```

Имена разделяются пробелами, запятыми или точками с запятой. Для нескольких
имён нужна строка в кавычках. Допустимы `rocket/explosion` и
`effects/rocket/explosion.efx`; регистр и направление слешей нормализуются.
Совпадение точное, без wildcard, наследования каталогов и распространения
на дочерние EFX. Nested effect проверяет собственное имя. Если имя есть
в обоих списках, режим 2 использует только opt-in, режим 3 — только opt-out.

Режим и списки архивируются в конфиг. Переключение действует на следующем
обновлении FX, включая уже живущие частицы; повторная загрузка карты не нужна.
Штатный блок `volumetricMedia` имеет приоритет: автоматика его не изменяет,
а эти четыре режима управляют только автоматической средой. Общий renderer
переключатель `r_volumetricParticles 0` выключает отображение всей particle media.

## Классификация

Точная таблица содержит 31 проверенный вариант shader в 20 штатных EFX:
дым, пар/туман, пылевые облака и газ. Ключ состоит из полного пути EFX,
FNV-1a checksum исходных байтов, номера поддерживаемого primitive и имени shader.
Изменение файла снимает точное совпадение; checksum проверяет версию ассета,
а не является механизмом безопасности. Таблица не содержит текстов или текстур
коммерческих ассетов.

Неизвестный или изменённый EFX получает generic-профиль только при сочетании:

- Однозначный отдельный token `smoke` в пути или имени primitive.
- Один из известных мягких smoke shaders: `black_smoke[2]`, `alpha_smoke[2]`.
- Конечные параметры: life 500–8000 ms, максимальный size 3–96 units,
  скорость до 100 units/s, count до 16, линейное исчезновение alpha.
- Нет physics, relative/view/depth-hack, специальных spawn flags,
  параметрических size/alpha, большой gravity/acceleration и исключённых
  контекстов вроде muzzle, fire, explosion, saber или glow.

Generic steam, пылевые точки/полосы, пламя и свечение остаются без автосреды.
Если один renderer handle соответствует конфликтующим shader-ролям,
автоматика также отказывается. Вариант определяется по уже выбранному shader
частицы, без дополнительного случайного выбора.

Среда — слабый сферический proxy, радиус 0.75 от актуального sprite size,
softness 0.7. Optical depth масштабируется текущей alpha и переводится в
extinction с учётом радиуса. Начальные optical depth 0.06–0.10 — осторожные
художественные настройки поверх сохранённого спрайта, а не измеренные
физические свойства. Автоматических emission, lights и агрегированных объёмов
в этом этапе нет. Профили проверяют EFX и имя shader; изменения shader scripts
или текстур модами пока не fingerprint-ятся.

## Диагностика и проверка

```text
set fx_physicalizationDebug 1
```

При регистрации EFX выводятся путь, primitive, shader, материал и основание:
`stock signature`, `high-confidence family` или `conservative fallback`.
Для уже зарегистрированных эффектов нужна перезагрузка карты. Диагностика
доступна и в Release-сборке. При первой отправке автоматического proxy также
выводятся actual radius, alpha, extinction, strength и world origin. Сам факт
upload не гарантирует видимость: облако может оказаться за геометрией или
моделью игрока. В SP можно зарегистрировать и проиграть эффект:

```text
set developer 1
fxplay volumetric/black_smoke2 160
r_volparticles
```

`r_volparticles` показывает renderer submission/culling/caps; debug views
`r_volumetricFogDebug 26` и `28` показывают плотность и границы proxies.
Renderer сохраняет свой лимит выбранных particle media, по умолчанию 128;
при нагрузке часть proxies может не попасть в GPU. Opt-in режим удобен
для последовательной визуальной настройки отдельных эффектов.

Для воспроизводимого сравнения есть [fx-stage1-demo.cfg](../tools/fx/fx-stage1-demo.cfg).
Положить его в активную game directory, загрузить карту и выполнить
`exec fx-stage1-demo.cfg`. Рядом установить [physicalization_smoke.efx](../tools/fx/physicalization_smoke.efx)
в `effects/test/`. Это наш синтетический неподвижный puff, использующий
установленный smoke shader; он проверяет также high-confidence generic path.
Demo создаёт puff,
замораживает FX-время и оставляет переключение `fx_physicalization 0` / `2`
для сравнения одной и той же геометрии. Завершить тест: `fx_freeze 0`.
Debug view 26 помогает увидеть собственно среду поверх исходных спрайтов.

`fx_physicalizationStrength` масштабирует только автоматическую optical depth
и действует на живые частицы. По умолчанию 1; диапазон 0–16. Значения 4–8
полезны для диагностики и настройки, поскольку добавка поверх сохранённого
спрайта при 1 может быть слабо различима. Demo задаёт 8 для заметного сравнения;
после теста вернуть `fx_physicalizationStrength 1`. Этот параметр не влияет на
ручные `volumetricMedia`, в отличие от общего `r_volumetricParticlesScale`.

При тесте установленной modded base нельзя рассчитывать на различие
`rocket/explosion` или `volumetric/black_smoke`: пакет
`zz_volumetric_media_test.pk3` уже задаёт им ручную среду, на которую
`fx_physicalization` не действует. `volumetric/black_smoke2` в этом пакете
не заменён. Команда `fxplay` зарегистрирована также в SP engine: до загрузки
карты она выводит пояснение, с несовместимой game DLL — сообщение об её
обновлении, после успешного вызова — подтверждение пути и расстояния.

Общий C++11 harness проверяет режимы, смену списков, fingerprints, альтернативы,
отсутствие RNG-вызовов и optical depth. Corpus replay проверяет все 1375
штатных media alternatives: 31 точное совпадение и 1344 legacy. Этот replay
проверяет таблицу; runtime дополнительно применяет ограничения flags/authoring.
Команды генерации и тестирования описаны в [tools/fx](../tools/fx/README.md).

Проверено при реализации: SP game DLL и MP engine собраны в RelWithDebInfo;
137 проверок C++ harness, 1375 corpus alternatives и 6 тестов audit прошли.
В SP на stock `t1_fatal` проверены живые частицы `volumetric/black_smoke`
со списком длиннее 255 символов. При замороженном FX-времени последовательность
режимов 0, 1, 2, 3, 0, 2 дала renderer submission counts 0, 58, 2, 56, 0, 2:
два дымовых proxy возвращаются при повторном opt-in, остальные 56 относятся
к окружению карты и выключаются режимом 2. MP runtime и визуальная приёмка
каждого профиля ещё не выполнены.

Дополнительно проверена установленная modded base: `fxplay`, точный profile
`black_smoke2` и generic high-confidence test smoke доходят до GPU; off/on
действует на замороженные FX. В синтетическом тесте `strength 8`, radius 36
и расстояние 32 units дают видимое затемнение обычной сцены. Это диагностическая
настройка, а не новая плотность по умолчанию для штатных EFX.

Дизайн и результаты исследования: [ревью предложения](legacy-fx-physicalization-review.md).

## Следующий этап: состав EFX и адаптация параметров

Два независимых экспериментальных флага, в SP и MP:

| Cvar | Default | Что меняет |
| --- | --- | --- |
| `fx_physicalizationComposite` | `0` | Разрешает high-confidence дымовой primitive внутри составного взрыва |
| `fx_physicalizationAdaptive` | `0` | Подбирает radius, softness и optical depth уже разрешённой среды |

Только значение `1` включает флаг. Общие четыре режима `fx_physicalization`,
opt-in/opt-out и strength продолжают действовать. Оба флага выключены по
умолчанию: новая классификация может добавить proxies и увеличить GPU-нагрузку,
а изменённый радиус может затронуть больше froxels даже при том же числе proxies.
При обоих `0` нет дополнительного анализа состава, декодирования текстур или
чтения shader/texture файлов. Добавленные поля занимают немного памяти в
templates и media; прежняя классификация и параметры первого этапа сохраняются.

Включать флаги следует **до загрузки карты** или затем перезагрузить карту:
дополнительные профили вычисляются при регистрации EFX. Если эффект был
зарегистрирован с включёнными флагами, выключение и повторное включение
действуют на уже живые частицы. Выключенный composite подавляет только новую
среду, выключенный adaptive возвращает параметры первого этапа. Для среды,
разрешённой только composite, adaptive самостоятельно её не включает.

Анализ состава различает обычную среду, fireball, flash, light,
дискретные точки/полосы, дочерние эффекты и ручную среду. Новый дым разрешён
только при сочетании живого света и короткого известного fireball/flash в
том же EFX, явном имени smoke/lingeringsmoke, одном из четырёх проверенных
soft-smoke shaders и всех прежних численных ограничениях generic smoke.
Контексты muzzle/saber/force/glow, physics, view hacks, сложные spawn/curve
flags и случайные size/alpha/RGB остаются исключены. Дочерние ссылки не
передают разрешение и свет рекурсивно: каждый EFX проверяется самостоятельно.
Ручной `volumetricMedia` имеет приоритет. Исходные sprites и lights сохраняются.

Adaptive использует только производные метрики шести stock smoke/steam/cloud
текстур: среднюю маску, радиус 90% её веса и среднюю маску на периферии.
Текстуры не декодируются в runtime. Размеры и рост берутся из EFX;
optical depth дополнительно снижается при большом максимальном числе
автоматических частиц в одной партии EFX. Это осторожная художественная
эвристика, не восстановление физических свойств из изображения и не подсчёт
живых частиц/пространственного перекрытия между независимыми emitters.
Adaptive не разрешает неизвестный эффект по одной текстуре и не добавляет
proxies к уже разрешённым частицам.

При adaptive проверяются VFS fingerprints исходных texture и shader script;
несовпадение или отсутствие файла оставляет базовые параметры. Проверки
кэшируются только на время регистрации одного EFX, размер чтения ограничен
2 MiB. Это защита от замены этих файлов, а не доказательство выбора shader
renderer-ом: дополнительное определение того же shader в другом script либо
особый выбор image format renderer-ом может остаться незамеченным. Для такой
модифицированной базы adaptive лучше оставить выключенным.

Во втором этапе автоматическая emission, новые lights, агрегированные объёмы
и широкая классификация неизвестных shaders не включались. Следующие изменения
описаны ниже и также имеют отдельные флаги.

Для проверки есть [fx-stage2-demo.cfg](../tools/fx/fx-stage2-demo.cfg) и
[physicalization_explosion.efx](../tools/fx/physicalization_explosion.efx).
EFX устанавливается в `effects/test/`, cfg — в активную game directory.
После загрузки карты выполнить `exec fx-stage2-demo.cfg`. Demo включает флаги,
создаёт синтетический взрыв и замораживает FX-время после вспышки.
`fx_physicalizationComposite 0/1` убирает/возвращает дополнительную дымовую
среду, `fx_physicalizationAdaptive 0/1` меняет её плотность и границы.
Ручная steam-среда в этом же тесте продолжает работать независимо от флагов.
При strength 1 разница может быть небольшой; demo использует 8.
Для просмотра среды отдельно: `r_volumetricFogDebug 26`, затем вернуть `0`.
Завершить: `fx_freeze 0`, `fx_physicalizationStrength 1`, оба новых флага `0`.

Проверено: SP game DLL и MP engine собраны в RelWithDebInfo; 186 C++ проверок,
1375 stock alternatives первого этапа и 9 Python тестов прошли. Повторная
генерация шести shape records дала идентичный header. В SP с установленной
modded base на `t1_fatal` synthetic fixture дал counts 69 → 70 → 70 → 69 → 69
для composite/adaptive 0/0, 1/0, 1/1, 0/1 и master off. При холодной регистрации
с обоими флагами 0 количество осталось 66 на всех шагах, дополнительного
анализа fixture не было. При намеренном изменении checksum shader script
composite сохранился, adaptive отсутствовал, counts 64 → 65 → 65 → 64 → 64.
Разные baselines относятся к ручным средам окружения и fixture между запусками;
сравниваются шаги внутри каждого замороженного запуска. Проверена регистрация
adaptive для штатных steam/smoke/dust EFX. Эти проверки не измеряют GPU-регрессию
и не заменяют визуальную приёмку каждого эффекта; MP runtime ещё не проверен.

## Третий этап: emission и точное объединение

| Cvar | Default | Поведение при `1` |
| --- | --- | --- |
| `fx_physicalizationEmission` | `0` | Дополнительная pure emission для точных stock flame/fireball profiles |
| `fx_physicalizationAggregate` | `0` | Объединение полностью совпадающих автоматических неэмиссивных сред |

Оба независимы от composite/adaptive. Все четыре общих режима и opt-in/opt-out
продолжают ограничивать автоматические среды. Emission требует включения перед
регистрацией EFX/загрузкой карты; отключение действует на живые частицы.
Aggregate работает в renderer и переключается сразу, без повторной регистрации.
Обычные defaults остаются `0`. Только отдельный тестовый launcher явно включает
emission; aggregate проверяется отдельным demo. При выключенных флагах нет
дополнительных emission file checks, powers для tint, сортировки/объединения
сред или двойного прохода для выделения glow slots. Небольшие поля provenance
в CPU records добавлены постоянно; GPU layout и размер public particle API
сохранены, новые биты flags используются только обновлёнными FX/renderer.

Emission разрешена только для неизменённых EFX `env/fire`, `env/small_fire`,
`env/small_fire_lod` и `rocket/explosion`: шесть primitives, 18 shader alternatives.
Проверяются EFX, ordinal, фактически выбранный shader, его исходный shader script
и все изображения stages. Изменённые/неизвестные EFX, physics/view primitives
и ручные `volumetricMedia` не получают её. Ограничение проверки overrides такое
же, как у adaptive: другой script с тем же shader name либо особый renderer
image-format override может остаться незамеченным.

Это небольшая художественная добавка scene-linear radiance поверх сохранённых
sprites, не восстановленная энергия исходной текстуры. Учитываются текущий
sprite radius, current RGB и плавное появление/затухание по реальному времени
жизни. Extinction у нового glow равна нулю; новые lights не создаются. Стены
и модели продолжают освещаться прежними источниками. Density strength не
масштабирует emission; глобальный renderer `r_volumetricEmission` действует.
При изменении glow снижается temporal history только для новых automatic glows.

В renderer новые automatic glows ранжируются после существующих authored/density
media. Ручная emission получает slots первой; автоматика использует максимум
четыре свободных slots из прежнего общего лимита 24. `r_volparticles` показывает
emissive count и glows dropped. Это не гарантирует сохранения всех ручных сред
при достижении более раннего общего submission cap 1024. Эффекты сверх emission
budget не усиливают оставшиеся glows. Само свечение и новые proxies могут
увеличить CPU/GPU time, поэтому feature остаётся opt-in через отдельный флаг.

Первое объединение намеренно **точное**: только automatic density records
с абсолютно одинаковыми center, extents, softness, albedo и phase, без emission.
Складывается extinction, representative id выбирается детерминированно.
Форма не расширяется, промежутки не заполняются, ручные среды не объединяются.
Плотность в каждой точке сохраняется с точностью float; старые individual
proxies одновременно с объединённым не инжектируются. Оригинальные FX particles
и sprites живут как прежде. Death/drain, выключение флага и смена representative
id проходят через существующее temporal pairing и vanished records.

Это может уменьшить GPU upload/slice lists у стационарных совпадающих puffs,
но большинство разнесённых/движущихся clouds сохранят per-particle medium.
Сортировка имеет CPU cost; гарантированного ускорения нет. При заполненном GPU
budget объединение может изменить отбор proxies. Более широкие compact groups,
plumes и emitter LOD требуют следующего этапа: stable owner/generation через
callers, scheduler, nested spawns и save/load. Объединение по одному имени EFX
или ближайшей позиции не реализовано.

Для ручного теста запустить `test-fx-stage3-sp.cmd`, загрузить карту:

```text
exec fx-stage3-emission-demo.cfg
// сравнение emission: fx_physicalizationEmission 0 / 1
// отдельно glow: r_volumetricFogDebug 33, обычный вид: 0
fx_freeze 0
exec fx-stage3-aggregate-demo.cfg
// сравнение upload: fx_physicalizationAggregate 0 / 1; r_volparticles
```

Emission demo использует существующий `env/small_fire`. В modded базе
`rocket/explosion` уже заменён ручным medium, поэтому новые автоматические
профили к нему не применяются. Aggregate demo использует наш
`effects/test/physicalization_cluster.efx`: четыре совпадающих puffs дают
один GPU proxy. Изображение должно сохраниться — здесь различие ожидается
в renderer counts. Завершить тест: `fx_freeze 0`, debug `0`, strength `1`,
оба новых флага `0`. Общие режимы и списки demo тоже изменяют для изоляции теста.

Проверено при реализации: SP DLL/exe, MP engine и оба rend2 DLL собраны;
298 C++ проверок, 1375 stock alternatives первого этапа и 12 Python тестов
прошли. В SP exact coalescing снизило число uploads на три при прежнем числе
submissions и вернуло их после отключения. На multiple rocket spawns emission
дала четыре glow slots, лишние glows были отброшены. В отдельной проверке
23 ручных emissive particles сохранили все slots; с автоматикой стало 24,
то есть ей достался один spare slot. Подмена checksum explosions.shader
подавила automatic emission, не повлияв на smoke coalescing. Изолированные
debug-33 кадры off/on показали появление glow. Повторные SP проверки второго этапа с выключенными новыми флагами прошли.
Полная визуальная приёмка HDR/bloom
в обычных сценах, GPU performance capture и MP runtime ещё не выполнены.

## Измеренное покрытие ассетов

Под «подхватывает EFX» здесь понимается хотя бы одна часть, разрешённая для
автоматического medium/emission. Это не полная замена EFX и не процент вызовов
во время прохождения. Измерено реальным SP parser и shared classifier через
`fxaudit`, включая flags, fingerprints, фактические shader handles и authoring
priority. Все запрошенные файлы зарегистрировались; master/opt lists при
подсчёте eligibility игнорируются. Собственные тестовые EFX исключены.

| База | EFX с автоматической частью | Medium: первый/второй этап | С emission: третий этап |
| --- | --- | --- | --- |
| Stock assets0–3 | 22 / 376 = **5.85%** | 29 / 760 sprite primitives; 33 / 1375 shader alternatives | 35 / 760 primitives; 51 / 1375 alternatives |
| Установленная modded base + fs_game | 19 / 379 = **5.01%** | 26 / 766 primitives; 30 / 1384 alternatives | 31 / 766 primitives; 45 / 1384 alternatives |

Composite пока не увеличил число штатных/установленных игровых файлов:
сохраняются прежние confidence vetoes. Он проходит на синтетическом составном
взрыве, исключённом из этой таблицы. Adaptive и exact coalescing не разрешают
новые эффекты. Emission добавляет части уже распознанных EFX, поэтому процент
файлов остаётся прежним, а процент primitives/alternatives растёт.
Stock 33 medium alternatives включают 31 exact и две high-confidence generic;
прежний corpus replay с нулевыми generic parameters проверял только exact table.
Покрытие файлов может вырасти при новых reviewed profiles или новых уверенных
семантических правилах. Высокий процент сам по себе не цель: многие EFX —
beams, sparks, energy, lights и decals, которым дымовая среда не нужна.

Установленная база измерена с чистым isolated home, штатным base/fs_game overlay
и loose EFX в fs_game; произвольные overrides из обычного пользовательского
home не учтены. Реальная игровая частота, динамические callers и GPU visibility
здесь не измерялись. Воспроизводимые JSON с пофайловыми результатами находятся
в `build/fx-coverage`; команды описаны в tools/fx README.


## Следующий подэтап: runtime source context

`fx_physicalizationSources 0|1` (archive, default **0**) включает только
отслеживание источников и bounded диагностический snapshot. Он не меняет
классификацию, sprites, форму, плотность, emission или renderer budgets.
Включается на лету; для действующего ID нужно создать новый FX. Живым частицам
и уже отложенным FX, родившимся при выключенном флаге, владельцы не назначаются.

В SP и MP каждый независимый корневой `PlayEffect` получает `(generation,id)`.
Immediate/delayed primitives, nested fxRunner, impact/death FX и emitter children
переносят контекст родителя. Scope RAII восстанавливает внешний контекст; child
не может убрать inherited attachment/view/physics ограничения. Переход между
world и portal без достоверного совпадения scope даёт неизвестный источник.
Direct primitive calls вне scheduler также остаются без owner.

Повторы в штатных bolted loop slots используют runtime sidecar с прежним ID;
stop, expiry и reuse освобождают его. Clean/FX_Stop/map initialization,
load и выключение/повторное включение флага инвалидируют поколение. MP также
инвалидирует его при откате FX time. SP loop save chunks и `refVolParticle_t`
не изменены: восстановленный loop получает нового runtime owner. Bolted sources
помечаются attached и не являются кандидатами для будущего пространственного
объединения. Повторные независимые world calls, включая события map fx_runner,
пока считаются отдельными bursts: стабильного entity generation через этих
callers ещё нет. Угадывания владельцев по имени/позиции нет.

`developer 1; fxsources` в SP и MP выводит последние frontend snapshots world и
portal отдельно. Snapshot собирается только при включённом Sources, максимум
1200 tracked submissions на сцену; overflowing diagnostics явно считаются.
Manual media в этот отчёт не попадают. Snapshot пересоздаётся при каждом FX_Add,
а не хранит вечный объём после первого birth. Счётчики описывают фактические
автоматические frontend submissions **до** renderer culling/limits; renderer
acceptance и GPU cost ими не измеряются.

Machine-readable строки:

- `FXSOURCE_SCENE|scope|generation|fxTime|rows|density|glow|untracked|overflow`.
  `fxTime=-1` означает отсутствие нового snapshot; scope может иметь последний
  snapshot своего прохода, поэтому время выводится явно.
- `FXSOURCE_ROW|scope|generation|id|domain|density|glow`.
  Domain bitmask: portal=1, attached=2, view=4, physics=8, world=0.
  Один owner с разными restrictions выводится разными rows.
- `fx_physicalizationDebug 1` дополнительно печатает один
  `FXSOURCE|effect|generation|id|domain|particleId` на реально поданный proxy за
  поколение. `generation=0,id=0` означает неизвестный или stale owner.

Проверено: 319 shared checks, 1375 stock shader alternatives, сборки SP game/exe
и MP exe. SP runtime fixture проверяет два независимых coincident bursts,
отложенные puffs и runners, emitter/death children, наследование physics,
cold enable и generation reset. Тест предыдущего emission/aggregate этапа с
Sources=0 отдельно проверяет отсутствие регрессии. MP runtime и реальный
save/load/bolted loop lifecycle пока отдельно не прогонялись; сохранённый формат
loop record проверен по коду. Производительность Sources=1 ещё не профилировалась.

Это основа source-context этапа, а не завершение широких aggregate/LOD.
Дальше нужны явные owner/generation для стационарных environmental callers,
ledger с birth/expiry и координатами действительно живых records, versioned
bridge к renderer, затем объединение с сохранением массы, проверкой overlap/
wall leakage и temporal handoff. Каждый из этих изменений должен иметь свой
выключенный по умолчанию флаг. Покрытие ассетов остаётся **5.85% stock / 5.01%
installed** из предыдущего измерения: правила eligibility не менялись.

Для ручного теста: запустить `test-fx-stage4-sp.cmd`, загрузить карту и выполнить
`exec fx-stage4-sources-demo.cfg`. Демо создаёт два синтетических источника с
дочерними FX, останавливает время и печатает `fxsources`. Визуальная форма этого
подэтапа прежняя; проверять нужно ID и domain в консоли. Вернуть время:
`set fx_freeze 0`. Default новых флагов — 0; archive сохраняет выбранные значения после тестового
запуска. Для отключения этого подэтапа: `set fx_physicalizationSources 0`.
