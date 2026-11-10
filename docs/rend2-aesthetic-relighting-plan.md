# План завершения Aesthetic Relighting стоковых карт

Дата исходной редакции: **2026-11-10**, Australia/Sydney. Статус: **план работ, не отчёт об успешной реконструкции**.

## 1. Цель и границы продукта

Получить воспроизводимый **data-only** набор современного освещения для оригинальных стоковых карт: визуально мотивированные источники, согласованные с геометрией, цветными световыми пятнами, относительной яркостью, тенями и композицией сцены.

Мы **не восстанавливаем исторический список ламп Raven**. Исторические типы, XYZ, мощности, закон затухания и настройки компилятора не являются обязательной целью или условием пригодности для ремастера. Оригинальные lightmaps/lightgrid — свидетельства и визуальный ориентир. Современная физика и художественные ограничения выбираются явно и сохраняются вместе с данными.

Включено:

- источники, привязанные к реальным лампам, экранам, светящейся BSP-геометрии и статическим моделям;
- вероятные скрытые источники только при достаточной пространственной/направленной/теневой мотивации;
- выбор формы, RGB, мощности, размера, диапазона, направления/cone и только обоснованных локальных вариантов положения;
- современное прямое освещение, поддержанные солнце/небо, ограниченное непрямое/фоновое представление;
- оригинальные геометрия и наблюдения, офлайн-проверки, сохранение параметров, полей, неопределённости и происхождения;
- диагностические изображения/геометрические отчёты и воспроизведение результата из сохранённых данных.

Не включено: новые художественно придуманные лампы, изменение BSP/lightmaps, игровой relighting, GPU/Forward+/LTC/fog/NPC consumers, world shading, применение или смешивание нового света в игре, решение double lighting. Будущий потребитель данных — отдельный проект.

### Результаты A/B/C/D

| Класс | Что он означает | Допустимый результат |
|---|---|---|
| **A** | Визуально подтверждённый светильник/излучающая область, согласованные окружающее освещение и геометрия | Пригодный для ремастера современный источник с происхождением и ограничениями. Это не историческая сертификация. |
| **B** | Вероятный источник без явно видимого светильника, поддержанный независимыми пространственными/направленными/теневыми признаками | Условный современный источник с более строгим обоснованием и сохранёнными альтернативами. |
| **C** | Поддержанное распределение освещения, которое нельзя убедительно отнести к лампе | Типизированное ограниченное фоновое/непрямое/appearance field без выдуманного физического источника. |
| **D** | Необъяснённый, противоречивый или недостаточно поддержанный сигнал | Явная область неопределённости и ссылки на оригинальные данные/fallback; не фиктивная лампа и не успех реконструкции. |

A/B могут быть пригодны при историческом `exactLight=Unknown`. Нельзя поднимать `Resolved`, историческую точность, вероятность существования или SI-калибровку из факта хорошего fit. Нельзя понизить неизвестную геометрию до пустоты. Ограниченная аппроксимация маски/материала допустима только с сохранёнными границами, предположениями и проверкой чувствительности.

## 2. Проверенная исходная точка

Исходный коммит при подготовке плана: `68585a75e` (ASR23), ветка `rend2-unified-wip`. Предоставленный checkout — `OpenJK-rend2`; записанный remote — `keyten/BetterJK`, хотя проект в заданиях называется `keyten/Jedi-Knight-2026`. Ничего не переименовывать: каждое выполнение записывает фактические revision/branch/remote.

На момент инспекции есть незакоммиченные изменения в `lightrecon_aesthetic_tool.cpp`, `lightrecon_grid_geometry_tool.cpp` и `tools/rend2/stock_aesthetic/` (разработка ASR24). Они содержат полезную сборку опыта на оригинальных картах. **Это рабочее состояние, а не замороженный итог**: сначала сверить его текущие результаты и переиспользовать завершённые части, не перезаписывать параллельную работу.

| Возможность | Что уже имеется | Как использовать в этом плане |
|---|---|---|
| Оригинальные данные и идентичность | BSP/lightmaps/lightgrid, shader/image/model provenance, `.jkobs`, геометрия, слепые folds, инвентарь стоковых карт | Переиспользовать; не строить второй capture pipeline. Исторический инвентарь — 57 BSP, актуальный знаменатель проверить. |
| Светящиеся BSP-маски | ASR09 уже вычисляет UV-clipped polygon unions и тёмные промежутки | Нужен корректный native bridge, а не повторная реализация извлечения. |
| Статические MD3 | ASR10/11 уже описывают экземпляры и излучающие области | Подключить при сохранении transform/instance/occlusion uncertainty. |
| Геометрическая допустимость | ASR17/18 исправили сторону и контакт: 368/620 кандидатов допустимы в фиксированной выборке, 320 условно | Это не 368 реконструированных ламп. Сохранить отрицательные геометрические проверки. |
| Современная форма | ASR19: point/spot/rectangle/tube, exact original unions, причины и неопределённость | Подключить реальные clipped masks и не выдавать плоскую полосу за цилиндр. |
| Настройка и отбор | ASR20/21: положительные RGB, fixed gauge, bounded nuisance, sparse selection, независимый held-out | Это база авторитетного fit; не плодить ещё один optimizer в каждом benchmark. |
| Дополнительный поиск | ASR22: bounded proposal/refit, разделение direct/indirect/solar ambiguity | Подключить к единой современной модели и политике A/B/C/D. |
| Слепая проверка | ASR23: 14/21 найденных корней и 5 ложных у all-cues; area-off не меняет результат; spot/finite-source случаи остаются проблемой | Сохранить как регрессионные свидетельства, но не превращать исторические source-truth gates в критерии стокового ремастера. |
| Положительный стоковый результат | Опубликованный ASR20 опыт на mp/duel4 дал нулевой direct response и baseline-only fit; более новая stock_aesthetic работа требует сверки | Первая цель — причинно проверенный настоящий стоковый участок, без бесконечного ожидания идеальной исторической идентификации. |
| Данные/поля | Canonical asset, calibration/provenance/configurations, Fields, Probes, Attribution и disk/chunks уже существуют | Использовать существующие семантики; специфическую aesthetic policy хранить явно, не менять молча старые контракты. |

Важный технический разрыв: `effectiveRadiusWorld` уже есть в canonical geometry, но текущая `Shape::Validate` запрещает его ненулевое значение. `rangeWorld` описан как culling metadata, а не автоматически как художественное затухание. Поэтому фраза «используем стандартную современную физику» требует отдельного решения и реализации **современного** поведения с неизменной исторической веткой.

Ключевые документы для исходной сверки:

- [ASR18](rend2-mounted-admission-asr18.md), [ASR19](rend2-modern-shapes-asr19.md);
- [ASR20](rend2-aesthetic-fit-asr20.md), [ASR21](rend2-sparse-source-selection-asr21.md), [ASR22](rend2-iterative-discovery-asr22.md);
- [ASR23](rend2-blind-benchmark-asr23.md) и [численные результаты](../tools/rend2/blind_benchmark/results/v1/tables.md);
- [BSP fixtures](rend2-bsp-fixtures-asr09.md), [model emission](rend2-model-emission-asr11.md), [association](rend2-fixture-association-asr15.md);
- [CPU fields](rend2-lighting-field-builder-p01.md), [attribution](rend2-contributions-p06.md).

## 3. Стратегия и промежуточные результаты

| Рубеж | Требуемый результат | Что нельзя выдавать за этот результат |
|---|---|---|
| **G0 — зафиксирован продукт** | Критерии эстетики, A/B/C/D, современная модель, appearance-domain, gauge, assistance и output semantics | «Решатель сошёлся» или выбор исторического профиля |
| **G1 — первый участок** | Настоящий стоковый интерьер с несколькими мотивированными источниками, ненулевым откликом, независимой проверкой и сохранёнными данными | Контрольная синтетическая сцена, легальные XYZ или zero-power baseline |
| **G2 — одна карта** | Согласованная вся карта: комнаты/переходы, shared sources, global/C, количественное покрытие, review и replay | Один удачный ROI или склейка независимо экспонированных комнат |
| **G3 — половина набора** | Не менее половины зафиксированного инвентаря реально принято по whole-map gates; при 57 картах — минимум 29 | Половина запущенных процессов или 29 карт с единственной успешной комнатой |
| **G4 — весь набор** | Каждый исходный BSP учтён; все заявленные complete-карты проходят финальные gates, C/D бюджеты и replay | «Все обработаны» при существенном неизвестном/оригинальном fallback |

Политика определяет численные пороги **до итогового теста**. Универсальный процент ошибки RGB не назначается в этом плане без эксперимента: старое точечное запекание может отличаться от убедительного современного area light. Но геометрическая допустимость, прозрачное покрытие, отсутствие выдуманной определённости и независимая проверка — обязательны.

A/B, C и D учитываются отдельно по фиксированным областям/площади, значимым помещениям и источникам. Сохранение исходного освещения в D — допустимый честный продуктовый fallback, но не новая реконструкция. Полностью отрендеренная/измеренная геометрия, обработанные карты, принятые карты и площадь принятого relighting — разные показатели. Существующие световые стили/модельные состояния не исчезают из знаменателя.

Рекомендуемый основной путь: основание → современный evaluator и BSP bridge → наблюдаемые receivers → пилот → global/C/selection → whole-map orchestration → одна карта → representative bank → половина → оставшиеся причины отказа → финальный набор. MD3 и локальное уточнение неизвестной геометрии становятся обязательными там, где без них не проходит целевая область. Output contract можно готовить сразу после пилота; он не должен ждать масштабного прогона.

## 4. Типы задач, размер и правила изменения плана

- **Research / исследовательская:** изучить код/данные/источники и записать проверяемые выводы. Может предлагать изменения, но не переписывает направление или критерии сама.
- **Decision / Experiment / управленческая или экспериментальная:** принять документированное решение, при необходимости после ограниченного эксперимента. Может переписывать roadmap, промпты, зависимости и добавлять задачи.
- **Implementation / исполнительская:** реализовать полностью конкретный согласованный механизм, интеграцию и содержательные проверки. Не подменять реализацию эскизом, заглушкой, приблизительным описанием идеи или отчётом без работающего пути.

**S** — один локальный исследовательский/контрактный результат с узкой областью изменений. **M** — ограниченная интеграция/эксперимент с несколькими интерфейсами и содержательной проверкой. Это размер сложности и объёма инженерной работы, не обещание часов. Длительность пакетного расчёта карт измеряется отдельно.

Первоначально крупные направления разделены на S/M-задачи:

- point/spot и finite area/tube;
- BSP-footprint bridge и MD3-instance bridge;
- фиксированный RGB fit и локальные shape/pose параметры;
- наблюдаемость receivers и итоговый выбор пилота;
- global sun/sky и C/indirect representation;
- планирование карты и согласование регионов;
- output contract и exporter/replay;
- replay/cache orchestration и решения о качестве корпуса.

В текущей редакции **33 задачи: 3 Research, 12 Decision/Experiment, 18 Implementation; 2 размера S и 31 размера M**. Задачи M с повышенным риском — exact native footprint integration, model occlusion, local geometry refinement, C fitting и map reconciliation. При обнаружении необходимости нового большого алгоритма управленческая задача обязана разбить работу на новые S/M tickets **до** исполнения; увеличивать scope незаметно нельзя.

### Протокол живого плана

1. У каждой задачи стабильный ID. Статусы: Pending, Running, Done, Blocked, Deferred, Superseded. Старые ID не переиспользовать; отменённые строки сохранять.
2. **Любая задача** дописывает высокоуровневое summary в раздел «Журнал результатов»: область, результат, свидетельства, ограничения, принятые решения и следующий шаг. Подробности могут находиться в отдельном отчёте, на который дан путь.
3. Done означает завершённый deliverable задачи. Для эксперимента это может быть честный отрицательный результат и принятое решение; он не превращает Failed product gate в Passed.
4. Только Decision/Experiment может менять направление, критерии, очередность, scope и текст будущих промптов или создавать новые задачи. Research/Implementation обновляют свой статус/summary и предлагают изменения для решения.
5. Изменение записывается в changelog: основание, evidence, старое/новое решение, затронутые ID, новые policy/data versions и последствия для замороженных тестов.
6. Результаты и критерии старого эксперимента не переписывать. Если final evidence использовалось для нового решения, оно становится development evidence; для нового итогового утверждения нужна независимая проверка.
7. При новом содержимом репозитория сначала сверить completed/in-flight work. Не дублировать завершённые возможности и не перезаписывать параллельные изменения.
8. Промпты ниже автономны: каждый повторяет цель, scope, ограничения и требования к завершению. Они не ссылаются на другие новые промпты; зависимости находятся только в таблице. Названные артефакты/политики — явные входные контракты, не знание предыдущего чата.
9. Отсутствующий обязательный input/policy обозначать как Blocked с конкретным требуемым артефактом. Не подставлять произвольное решение; независимую часть работы завершить.
10. Рекомендуемые новые отчёты — `docs/rend2-relighting/`; versioned policies/orchestration — `tools/rend2/aesthetic_relighting/` с переиспользованием существующих модулей. Это не требование создавать второй solver. Licensed stock payloads держать в приватных/ignored выходных каталогах.
11. Пересмотр направления разрешён только внутри цели Aesthetic Relighting и data-only scope. Управленческая задача не получает разрешения подключать игровой свет, менять карты или возвращать историческую идентификацию как обязательное условие продукта.

**С чего начать:** AR-01 сверяет текущее состояние и свежую работу stock_aesthetic. Затем AR-02 и AR-03 фиксируют продуктовые критерии и допустимый observation domain; AR-04 выбирает современную модель. После этого можно исполнять конкретные интеграции. Не запускать весь список как независимые задачи одновременно: входные политики и доказательства должны реально существовать.

## 5. Очередь задач и зависимости

Все статусы здесь означают состояние **нового плана**, а не отрицание уже написанного ASR-кода. Если цель уже закрыта свежей работой, сначала проверить доказательства и зафиксировать переиспользование, а не переписать её заново.

| ID | Тип | Размер | Задача | Входные зависимости / условие | Статус |
|---|---|---|---|---|---|
| [AR-01](#ar-01) | Исследовательская | S | Audit the current relighting path and reconcile work in progress | — | Pending |
| [AR-02](#ar-02) | Управленческая / экспериментальная | M | Freeze aesthetic success criteria, A–D outcomes and coverage accounting | AR-01 | Pending |
| [AR-03](#ar-03) | Исследовательская | M | Specify an honest appearance target and material-response model | AR-01 | Pending |
| [AR-04](#ar-04) | Управленческая / экспериментальная | M | Freeze the modern lighting and appearance policy | AR-02, AR-03 | Pending |
| [AR-05](#ar-05) | Исполнительская | M | Implement finite-size modern point and spot evaluation | AR-04 | Pending |
| [AR-06](#ar-06) | Исполнительская | M | Complete area, clipped-sheet and tube evaluation parity | AR-04, AR-05 | Pending |
| [AR-07](#ar-07) | Исполнительская | M | Connect exact BSP luminous footprints to native evaluation | AR-04, AR-06 | Pending |
| [AR-08](#ar-08) | Исполнительская | M | Connect static model luminous regions without inventing occlusion | AR-07; условно до принятия карты с существенными MD3-светильниками | Pending |
| [AR-09](#ar-09) | Исполнительская | M | Implement fixture-bounded source poses and emitting-side placement | AR-05, AR-06, AR-07 | Pending |
| [AR-10](#ar-10) | Исполнительская | M | Resolve local geometry uncertainty where evidence permits | AR-09; по диагностированным блокерам | Pending |
| [AR-11](#ar-11) | Исполнительская | M | Commission source-to-receiver influence and observable sampling | AR-06, AR-07, AR-09 | Pending |
| [AR-12](#ar-12) | Управленческая / экспериментальная | M | Freeze a real-stock pilot bank and assistance budget | AR-02, AR-11 | Pending |
| [AR-13](#ar-13) | Исполнительская | M | Integrate appearance-guided RGB fitting for fixed fixtures | AR-04, AR-06, AR-11, AR-12 | Pending |
| [AR-14](#ar-14) | Исполнительская | M | Fit only justified local shape and pose parameters | AR-09, AR-13 | Pending |
| [AR-15](#ar-15) | Исполнительская | M | Produce offline visual evidence and spatial quality reports | AR-11, AR-13 | Pending |
| [AR-16](#ar-16) | Управленческая / экспериментальная | M | Accept or reject the first stock region | AR-12, AR-13, AR-14, AR-15 | Pending |
| [AR-17](#ar-17) | Исследовательская | M | Design scene-wide sunlight and nonlocal illumination handling | AR-16; допустимо раньше для диагностики | Pending |
| [AR-18](#ar-18) | Управленческая / экспериментальная | M | Choose global and background model capacity | AR-17, AR-02 | Pending |
| [AR-19](#ar-19) | Исполнительская | M | Integrate modern sun and sky hypotheses | AR-18 | Pending |
| [AR-20](#ar-20) | Исполнительская | M | Represent supported nonlocal illumination as category C | AR-18 | Pending |
| [AR-21](#ar-21) | Исполнительская | M | Unify evidence-aware sparse source selection | AR-13, AR-14, AR-18 | Pending |
| [AR-22](#ar-22) | Исполнительская | M | Add only motivated category B sources in an iterative loop | AR-19, AR-20, AR-21 | Pending |
| [AR-23](#ar-23) | Управленческая / экспериментальная | M | Pass a modern aesthetic adversarial validation bank | AR-19, AR-20, AR-21, AR-22 | Pending |
| [AR-24](#ar-24) | Исполнительская | M | Plan complete map work from original geometry | AR-16, AR-23 | Pending |
| [AR-25](#ar-25) | Исполнительская | M | Reconcile shared sources and lighting across map regions | AR-24, AR-21, AR-22 | Pending |
| [AR-26](#ar-26) | Управленческая / экспериментальная | M | Accept the first complete stock map | AR-08 при необходимости, AR-10 при необходимости, AR-24, AR-25, AR-27, AR-28 | Pending |
| [AR-27](#ar-27) | Управленческая / экспериментальная | S | Freeze a faithful data-only output contract | AR-02, AR-04, AR-16 | Pending |
| [AR-28](#ar-28) | Исполнительская | M | Export and replay validated aesthetic reconstruction data | AR-27, AR-13, AR-20 | Pending |
| [AR-29](#ar-29) | Исполнительская | M | Make corpus processing resumable and dependency-safe | AR-24, AR-25, AR-28 | Pending |
| [AR-30](#ar-30) | Управленческая / экспериментальная | M | Validate transfer to a representative stock-map bank | AR-26, AR-29 | Pending |
| [AR-31](#ar-31) | Управленческая / экспериментальная | M | Reach the half-corpus acceptance milestone | AR-30 | Pending |
| [AR-32](#ar-32) | Управленческая / экспериментальная | M | Resolve the remaining corpus gaps without an endless research loop | AR-31 | Pending |
| [AR-33](#ar-33) | Управленческая / экспериментальная | M | Accept the complete original-stock relighting dataset | AR-32 и все добавленные обязательные ремонты | Pending |

Ветви AR-27/28 (контракт/экспорт) можно вести после пилота параллельно развитию global/C. AR-08 и AR-10 не обязаны задерживать простой пилот, но их отсутствие учитывается в покрытии. AR-23 проверяет именно современный aesthetic pipeline; ASR23 остаётся неизменённым историческим свидетельством. AR-32 — явная точка изменения направления для последних сложных карт, а не разрешение бесконечно расширять scope.

## 6. Самостоятельные промпты для задач

Каждый блок ниже можно передать отдельному исполнителю без этого чата и без других промптов. План и названные существующие файлы доступны из репозитория. Тип и размер продублированы внутри блока.

<a id="ar-01"></a>

### AR-01 — Audit the current relighting path and reconcile work in progress

Тип: **Исследовательская**. Размер: **S**.

```text
Task ID: AR-01
Task type: Research
Estimated size: S
Title: Audit the current relighting path and reconcile work in progress

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Produce a current, evidence-backed implementation and capability map for data-only aesthetic relighting, including any stock_aesthetic work that has appeared since this plan was written.

Required inputs and inspection:
Inspect git status/log, docs/rend2-aesthetic-fit-asr20.md, docs/rend2-sparse-source-selection-asr21.md, docs/rend2-iterative-discovery-asr22.md, docs/rend2-blind-benchmark-asr23.md, tools/rend2/stock_aesthetic/, tools/lightrecon_test/aesthetic_fit.{h,cpp}, aesthetic_sparse.inc, iterative_discovery.{h,cpp}, shared/rd-rend2/lightrecon/{emitter_shape,shape_selection,emitter_support,producer,field_builder,contributions}.*, and the existing fixture catalogues.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Inventory completed code, experimental adapters, uncommitted work, frozen measurements and genuinely missing connections. Identify one authoritative implementation for fitting, source support, visibility and each observation domain.
2. Trace a real fixture from shader/image geometry through source support, receiver selection, modern coefficients, fitting, validation and saved data. Distinguish available catalogue masks from masks actually consumed by native evaluation.
3. Document finite effective-radius rejection in the current Shape validator, rangeWorld's existing culling semantics, footprint/support assumptions, and the distinction between aesthetic eligibility and exactLight.
4. Record present stock results and their scopes, original asset/capture availability and the actual repository remote. Mark concurrent uncommitted work as provisional rather than duplicating or certifying it.
5. Write docs/rend2-relighting/implementation-audit.md with a reuse map, blocker table, authoritative artifact paths and recommended minimal pilot route.

Definition of Done:
- The audit names the current revision, local modifications, APIs, native callers and specific blockers with file references.
- Completed ASR09/11 mask extraction and ASR20–23 numerical tools are distinguished from missing native/orchestration integration.
- Claims about stock success, scene-wide coverage and tests are tied to actual receipts; unknowns remain explicit.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not implement a second fitter, start a new benchmark, modify concurrent work, or treat an old report's failure as proof that the current code still fails. This task produces a substantive research document, not a speculative rewrite.
Produce a substantive written investigation with code/data references, limitations and actionable conclusions. Read relevant primary sources if external technical evidence is needed. Separate measured facts, assumptions and recommendations. Do not start speculative implementation or new experiments outside the stated investigation.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-01" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This research task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-02"></a>

### AR-02 — Freeze aesthetic success criteria, A–D outcomes and coverage accounting

Тип: **Управленческая / экспериментальная**. Размер: **M**.

```text
Task ID: AR-02
Task type: Decision / Experiment
Estimated size: M
Title: Freeze aesthetic success criteria, A–D outcomes and coverage accounting

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Adopt a measurable product contract for aesthetic relighting that can accept useful modern reconstructions without historical source certification, while preventing fictitious success and unmotivated lights.

Required inputs and inspection:
Inspect current docs/rend2-relighting/implementation-audit.md if present, docs/rend2-aesthetic-source-baseline-asr01.md, docs/rend2-blind-benchmark-asr23.md, and the current stock_aesthetic policies/results. Evaluate the real repository state rather than assuming any report is final.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Define A as a visibly supported fixture with compatible lighting/geometry, B as a plausible unobserved source with independent spatial/directional/shadow support, C as supported lighting distribution without an identified lamp, and D as unresolved/conflicting evidence with original-data fallback.
2. Separate aesthetic-use eligibility from exactLight, historical type/XYZ/intensity, statistical existence confidence and calibrated SI. Define allowed bounded approximations and explicit assistance levels.
3. Freeze region/map acceptance measures: geometric safety, motif/color consistency, relative brightness ordering, shadows/dark controls, parsimony, worst-region errors, independent visual review and material/transfer sensitivity. Choose and justify numerical thresholds before final evaluation.
4. Define coverage denominators from the original map inventory and fixed eligible surface/region census. Track A/B direct reconstruction, C field support and D/original fallback separately; processing a map or preserving its original lighting does not count as reconstructing it.
5. Set distinct gates for a pilot region, one whole map, at least half the frozen map corpus, and the full corpus. Define allowed D budgets and unresolved high-salience regions; no substantial D area may be hidden in a 'complete' label.
6. Write a versioned policy and decision rationale under docs/rend2-relighting/ and tools/rend2/aesthetic_relighting/policies/. Exercise the policy on successful, empty, unknown, area-vs-point and false-lamp counterexamples.

Definition of Done:
- A machine-readable, versioned acceptance policy and readable decision record exist, including hard gates and tunable aesthetic measures.
- A/B can be accepted for remaster use with unknown historical identity; C/D have meaningful outputs and explicit coverage accounting.
- Historical XYZ matching and exact luxel equality are not stock acceptance requirements; low aggregate RGB error alone cannot pass.
- Any newly required implementation work is added to the plan as bounded, independently executable prompts.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not inherit historical-identifiability gates as aesthetic blockers, lower thresholds after inspecting final-test failures, or invent physical calibration. Do not turn a workflow decision into runtime integration.
Prepare the concrete experiment/decision materials, run the authorized bounded evaluation if required, and issue a supported decision with alternatives, limitations and plan consequences. A negative result is valid evidence; do not manufacture a pass or leave only a proposal to investigate later.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-02" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This is a decision/experiment task: you may revise the roadmap, dependencies, future prompts and add tasks when evidence justifies it. Record every structural/policy change and its rationale in the plan change log. Preserve superseded tasks, failed results and frozen criteria; new tasks need self-contained English prompts and S/M sizing. A changed direction must have concrete deliverables and exit gates.
```

<a id="ar-03"></a>

### AR-03 — Specify an honest appearance target and material-response model

Тип: **Исследовательская**. Размер: **M**.

```text
Task ID: AR-03
Task type: Research
Estimated size: M
Title: Specify an honest appearance target and material-response model

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Determine how original encoded lightmaps can guide modern relighting without being silently treated as linear irradiance, and recommend a bounded, implementable comparison model.

Required inputs and inspection:
Read tools/lightrecon_test/aesthetic_fit.{h,cpp}, shared/rd-rend2/lightrecon/{decode,bake_forward,emitter_shape,amplitude_fit}.*, docs/rend2-local-light-evidence-asr08.md, docs/rend2-aesthetic-fit-asr20.md, and current source texture/material capture and calibration contracts.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Trace original RGB8 decoding, quantization, saturation, shared-peak compression, material variation and the existing forward-encoded appearance route.
2. Compare a fixed forward appearance operator, explicitly approximate target transforms, same-material relative/contrast features, and any independently supported linear observations. State which quantities each can and cannot support.
3. Specify a fixed brightness/color gauge and how exposure, receiver response, RGB power and background avoid becoming mutually compensating free parameters.
4. Define training-only treatment of any fitted material-group nuisance, its bounds and validation behavior. Explain why current PBR albedo or an assumed inverse gamma cannot recover historical incident light by itself.
5. Recommend the minimum pilot model plus a later multi-material extension. Identify source-color priors, reflection tint, dark controls and saturation handling that preserve the scene's visual motifs.
6. Write docs/rend2-relighting/appearance-domain-research.md with equations, explicit assumptions, adversarial cases and concrete API changes.

Definition of Done:
- The report distinguishes encoded appearance, approximate linear appearance and physically attested irradiance.
- The proposed loss has a consistent domain, fixed gauge, nonnegative source parameters and bounded nuisance behavior.
- The recommendation can be implemented with existing evaluators and identifies experiments that would falsify it.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not claim to recover Raven's bake recipe, divide encoded lightmaps by modern albedo, subtract encoded RGB as energy, or write code that merely renames RGB8 as linear. Do not require historical calibration to permit a clearly labeled aesthetic approximation.
Produce a substantive written investigation with code/data references, limitations and actionable conclusions. Read relevant primary sources if external technical evidence is needed. Separate measured facts, assumptions and recommendations. Do not start speculative implementation or new experiments outside the stated investigation.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-03" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This research task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-04"></a>

### AR-04 — Freeze the modern lighting and appearance policy

Тип: **Управленческая / экспериментальная**. Размер: **M**.

```text
Task ID: AR-04
Task type: Decision / Experiment
Estimated size: M
Title: Freeze the modern lighting and appearance policy

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Choose one explicit modern source-transport and appearance policy for the relighting output, including the meaning of source size and artistic range, so subsequent fitting has a fixed model.

Required inputs and inspection:
Inspect docs/rend2-relighting/appearance-domain-research.md, current acceptance policies, shared/rd-rend2/lightrecon/emitter_shape.*, source-support code, tr_lightrecon_types.h, and small owned emitter fixtures. If a required policy is missing, record the missing artifact instead of guessing its semantics.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Choose a physically motivated inverse-square far-field point/spot model with finite near-source behavior, an explicit effective-size interpretation, smooth artistic range behavior and a normalized angular convention.
2. Choose rectangle, original clipped-sheet and tube integration semantics, one/two-sided emission, treatment of tube end caps, and justified segmented approximations.
3. Distinguish effective radiometric size, occupied fixture geometry, contact clearance and range metadata. Do not silently change rangeWorld from culling metadata into a different canonical semantic.
4. Run small near/far, energy-normalization, scale/gauge, grazing-angle and mounting experiments. Choose one model and record rejected alternatives and known approximations.
5. Choose the appearance-domain route, material assumptions, fixed transfer/gauge and baseline/background limits. Set tolerances for native evaluation, numerical refinement and approximation sensitivity.
6. Write a versioned modern-lighting policy, appearance policy and implementation decision record. Update downstream prompt details through artifact contracts, without making them depend on this conversation.

Definition of Done:
- Equations, units, limits, source-size/occupancy distinctions, angular normalization and range behavior are unambiguous and frozen.
- Aesthetic model identity can be hashed and saved alongside existing canonical data without reinterpreting old assets.
- The chosen policy permits area/spot improvements even when an old point-light bake has lower literal RGB error.
- Required implementation changes are split into S/M tasks; unresolved model choices are not delegated as vague coding instructions.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not search for a historical falloff, fit exposure jointly with source power without fixing the gauge, implement GPU/runtime consumers, or choose a convenient near-field clamp without stating its physical/approximation meaning.
Prepare the concrete experiment/decision materials, run the authorized bounded evaluation if required, and issue a supported decision with alternatives, limitations and plan consequences. A negative result is valid evidence; do not manufacture a pass or leave only a proposal to investigate later.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-04" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This is a decision/experiment task: you may revise the roadmap, dependencies, future prompts and add tasks when evidence justifies it. Record every structural/policy change and its rationale in the plan change log. Preserve superseded tasks, failed results and frozen criteria; new tasks need self-contained English prompts and S/M sizing. A changed direction must have concrete deliverables and exit gates.
```

<a id="ar-05"></a>

### AR-05 — Implement finite-size modern point and spot evaluation

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-05
Task type: Implementation
Estimated size: M
Title: Implement finite-size modern point and spot evaluation

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Implement the complete modern-only point/spot evaluation specified by the frozen aesthetic policy, including nonzero effective size and artistic range behavior, while preserving historical evaluation.

Required inputs and inspection:
Inspect the current modern-lighting policy under tools/rend2/aesthetic_relighting/policies/, docs/rend2-relighting/, shared/rd-rend2/lightrecon/emitter_shape.*, tr_lightrecon_types.h, source-support and prepared-block identity code. The current Shape validator rejects nonzero effectiveRadiusWorld; do not mistake the existing field for implemented support.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Integrate the selected model into a native modern-only evaluator or narrowly scoped modern branch, reusing prepared geometry, visibility, occupancy and parameter structures.
2. Implement finite near-field response, inverse-square far-field behavior, normalized spot cones, artistic range transitions and exact parameter-domain validation.
3. Keep effective source size distinct from occupied fixture volume. Use the selected mounting/emission-node policy; an arbitrary empty-air sphere is not a mounting proof.
4. Include all model/radius/range/angular parameters in prepared identities and diagnostics. Preserve unavailable results for unknown optical paths.
5. Add independent numerical references and tests for near/far limits, continuity, radius/range changes, cone edges, blocked and unknown paths, and unchanged historical outputs.
6. Expose the full evaluated behavior through the existing aesthetic fitting path and offline output metadata; a disconnected helper is insufficient.

Definition of Done:
- Nonzero effective-size point and spot sources are evaluable, fit-ready and reproducible under the frozen modern policy.
- Numerical references validate the chosen equations and documented approximation error; no nonfinite or negative response is admitted.
- Historical source expansion, exact-contact guards, storage semantics and regression witnesses retain their previous behavior.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not remove validation checks globally, implement only a preview formula or a renderer-specific approximation, or claim that a softened ideal source proves a finite bulb body exists.
Implement the specified mechanism completely through its real native/orchestration path, with meaningful regression and reference checks. Do not substitute a loose conceptual approximation, a disconnected helper, a stub or a report for working integration. If the work is larger than M, complete the bounded independent work and document a concrete split for a decision; do not silently expand the ticket.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-05" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This implementation task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-06"></a>

### AR-06 — Complete area, clipped-sheet and tube evaluation parity

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-06
Task type: Implementation
Estimated size: M
Title: Complete area, clipped-sheet and tube evaluation parity

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Make all supported finite modern sources obey the same frozen energy, visibility, range and appearance conventions as the point/spot evaluator.

Required inputs and inspection:
Inspect the frozen modern-lighting policy, Shape::Prepare/EmissionNodes, SourceSupport, existing rectangle/original-union/tube quadrature tests and the aesthetic fitting API. Reuse implemented finite integration rather than replacing it with another solver.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Implement or reconcile finite rectangle, original clipped-sheet and tube response under the chosen policy, including angular and range conventions.
2. Keep emission area, radiance-like amplitude and point-intensity-like amplitude explicit. Preserve one/two-sided semantics and the declared open-cylinder end-cap rule.
3. Implement the allowed segmented-tube approximation with saved component membership, bounded geometric/radiometric error and common physical-fixture identity.
4. Preserve holes, gaps and separate components in irregular emission. Unsupported geometry must remain Unknown or have a separately measured bounded approximation.
5. Verify partially blocked sources, receiver footprints and quadrature convergence against independent higher-accuracy references.
6. Integrate all finite-source parameters, approximation choices and source support into fitting and prepared/cache identities.

Definition of Done:
- Rectangle, exact sheet and tube hypotheses pass native integration and cross-type normalization/reference tests.
- Changing segmentation or quadrature does not silently change total emission, fixture count or canonical identity.
- Area and tube behavior is used by actual fitting and exported diagnostics, not just standalone tests.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not approximate every panel with a point, fill a nonrectangular mesh's bounding rectangle, invent a tube body from aspect ratio, or change historical polygon expansion.
Implement the specified mechanism completely through its real native/orchestration path, with meaningful regression and reference checks. Do not substitute a loose conceptual approximation, a disconnected helper, a stub or a report for working integration. If the work is larger than M, complete the bounded independent work and document a concrete split for a decision; do not silently expand the ticket.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-06" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This implementation task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-07"></a>

### AR-07 — Connect exact BSP luminous footprints to native evaluation

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-07
Task type: Implementation
Estimated size: M
Title: Connect exact BSP luminous footprints to native evaluation

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Use the existing shader/image-clipped BSP emitting polygons as actual native aesthetic source support instead of substituting a whole BSP face.

Required inputs and inspection:
Inspect tools/rend2/bsp_fixtures/catalogue.py and its geometry/mask helpers, tools/rend2/shape_selection/, shared/rd-rend2/lightrecon/{shape_selection,emitter_support,emitter_shape}.*, and current stock_aesthetic adapters. The catalogue already clips RGB mask support; the native bridge is the gap to verify.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Read the existing catalogue's pinned BSP, shader, image, UV-transform, stage, triangle-alias, component and uncertainty records.
2. Build an immutable native footprint lease with explicit interpretation and generation identity. Prove clipped polygons lie within their pinned source triangles and declared support bounds.
3. Preserve lit/dark gaps, multiple stages, component identities and per-layer color provenance. Retain optical alpha/layer uncertainty separately from conditional RGB-mask support.
4. Extend only the necessary modern-only geometry/support adapter. Newly clipped vertices must not be mislabeled as unchanged original BSP triangle vertices.
5. Feed the actual footprint to native placement and quadrature, association, fitting and diagnostics. Whole-face approximation remains an explicit alternative with measured bounds.
6. Verify repeated/clamped/affine UVs, thin strips, dark gaps, overlapping UVs, irregular unions, stale image hashes and independent blockers.

Definition of Done:
- A stock catalogue fixture can be evaluated and fitted using its clipped emitting geometry with original surface identity and image provenance.
- Area, centroid and source contribution match independent polygon/quadrature references within the declared tolerance.
- Unsupported optics stay conditional/unknown; old .jkobs data and historical source-support interpretation remain readable and unchanged.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not rebuild already working mask extraction, promote RGB thresholds to optical transparency, pretend a fitted rectangle is the exact luminous mask, or stop at JSON import without native fitting integration.
Implement the specified mechanism completely through its real native/orchestration path, with meaningful regression and reference checks. Do not substitute a loose conceptual approximation, a disconnected helper, a stub or a report for working integration. If the work is larger than M, complete the bounded independent work and document a concrete split for a decision; do not silently expand the ticket.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-07" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This implementation task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-08"></a>

### AR-08 — Connect static model luminous regions without inventing occlusion

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-08
Task type: Implementation
Estimated size: M
Title: Connect static model luminous regions without inventing occlusion

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Make eligible original static-model luminous fixtures usable as aesthetic source anchors with correct instance transforms, emitting support and explicit occlusion-participation limits.

Required inputs and inspection:
Inspect tools/rend2/md3_instances/, tools/rend2/model_emission/, docs/rend2-md3-instances-asr10.md, docs/rend2-model-emission-asr11.md, native fixture-footprint adapters, SourceSupport and static visibility interfaces.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Reuse existing MD3 decoding, skin/material resolution, selected poses, instance manifests and clipped emission polygons.
2. Adapt eligible instance-local regions into pinned world-space source support with correct nonuniform-scale area/normals and stable instance/component identities.
3. Define explicit geometry-provider identities for model facets; do not fabricate BSP ordinals or collapse repeated instances into one source.
4. Handle emitting self-facets with narrowly scoped identity rules while preserving opaque housing and all independent walls. Missing or unverified model occupancy remains explicit.
5. Route admitted sources through modern evaluation, association and fitting; retain unsupported dynamic/animated/unknown-state records as coverage gaps.
6. Verify transformed instances, repeated assets, mixed skins, BSP-duplicate suppression, housing occlusion, unknown alpha and missing models.

Definition of Done:
- At least one eligible stock static-model fixture reaches native geometry/evaluation and data-only fitting or receives a precise diagnosed blocker.
- Instance identity, transformed area and occlusion provenance survive output and replay.
- Unsupported model geometry never becomes empty space or a silently transparent light housing.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not import every model as an opaque box, invent bake-time mover states, add runtime model lighting, or claim catalogued emission automatically participates in the old bake.
Implement the specified mechanism completely through its real native/orchestration path, with meaningful regression and reference checks. Do not substitute a loose conceptual approximation, a disconnected helper, a stub or a report for working integration. If the work is larger than M, complete the bounded independent work and document a concrete split for a decision; do not silently expand the ticket.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-08" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This implementation task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-09"></a>

### AR-09 — Implement fixture-bounded source poses and emitting-side placement

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-09
Task type: Implementation
Estimated size: M
Title: Implement fixture-bounded source poses and emitting-side placement

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Provide admissible modern source poses, size bounds and local stand-off alternatives tied to real fixtures, without reviving global room-wide position search.

Required inputs and inspection:
Inspect SourceSupport, regions_support.cpp, source_orientation.h, modern emitter policies, exact fixture adapters and tools/rend2/stock_aesthetic/. The current stock experiment's fixed outward point offset is an explicit approximation to review, not a universal fixture dimension.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Derive initial pose and bounded alternatives from actual luminous geometry, housing dimensions, mounting side and explicitly recorded assumptions.
2. Distinguish point/spot proxy size, finite occupied body, emitting sheet, recess and front clearance. Allow a nonzero stand-off only with fixture evidence or a clearly bounded approved approximation.
3. Validate every alternative against retained brushes and independent visual geometry; exclude only precisely identified emitting self-geometry where justified.
4. Expose bounded size, direction, cone and local pose ranges to fitting with stable fixture-family IDs. No alternative may jump to another fixture or pass through a wall.
5. Preserve multiple plausible poses as alternatives and retain Unknown when support is insufficient.
6. Test wall/ceiling mounts, recesses, thin sheets, tube tangent bodies, crossing walls, wrong-side emission, unknown alpha and disconnected fixtures.

Definition of Done:
- Point/spot/area/tube hypotheses have auditable source-to-fixture pose bounds and correct emitting-side admission.
- The modern fitter can use safe local alternatives while unrelated solid/optical constraints remain enforced.
- Fixed arbitrary offsets are replaced by explicit supported policies or remain labeled bounded approximations.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not clamp negative distances into validity, exempt a whole mounting brush, treat all unknown geometry as free, or move lamps anywhere in the room to reduce RGB error.
Implement the specified mechanism completely through its real native/orchestration path, with meaningful regression and reference checks. Do not substitute a loose conceptual approximation, a disconnected helper, a stub or a report for working integration. If the work is larger than M, complete the bounded independent work and document a concrete split for a decision; do not silently expand the ticket.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-09" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This implementation task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-10"></a>

### AR-10 — Resolve local geometry uncertainty where evidence permits

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-10
Task type: Implementation
Estimated size: M
Title: Resolve local geometry uncertainty where evidence permits

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Reduce avoidable Unknown source/receiver paths using original local geometry and bounded refinement while preserving genuinely unavailable geometry and optics.

Required inputs and inspection:
Inspect visibility extraction, BSP support predicates, original patch control geometry, receiver_support, ASR17/18 diagnostics and current fixture influence reports. Work only on diagnosed local failure classes.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Classify Unknowns as patch approximation, bounds-only omission, numerical boundary ambiguity, missing instance state, unknown optics or stale identity.
2. Implement bounded local patch refinement or conservative separation proofs for the selected resolvable classes, preserving the original error envelope until a proof/refinement replaces it.
3. Keep refined geometry and tolerance in the scene/prepared identity. Apply the same interpretation to source admission, visibility and receiver comparison.
4. Report unresolved optical/mover/missing-data cases with original support IDs and scoped coverage loss.
5. Compare exact failure reasons before/after on fixed owned and stock witnesses, including known collisions and alpha controls.
6. Expose work/memory limits and stop explicitly when the required refinement cannot be completed.

Definition of Done:
- A diagnosed avoidable local Unknown class is resolved using retained evidence with quantified error/work, or its external blocker is precisely demonstrated.
- All known solid intersections, unknown optics and unavailable model states retain safe behavior.
- Coverage reports show original and resolved denominators; no removed failure disappears from accounting.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not globally lower patch accuracy, convert conservative AABBs to empty geometry, change historical tracing tolerances indiscriminately, or make Unknown mean black.
Implement the specified mechanism completely through its real native/orchestration path, with meaningful regression and reference checks. Do not substitute a loose conceptual approximation, a disconnected helper, a stub or a report for working integration. If the work is larger than M, complete the bounded independent work and document a concrete split for a decision; do not silently expand the ticket.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-10" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This implementation task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-11"></a>

### AR-11 — Commission source-to-receiver influence and observable sampling

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-11
Task type: Implementation
Estimated size: M
Title: Commission source-to-receiver influence and observable sampling

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Ensure real fixture hypotheses are compared against geometrically relevant original surfaces and explicit dark/shadow controls, with complete diagnoses for zero or unavailable response.

Required inputs and inspection:
Inspect tools/lightrecon_test/aesthetic_region.h, lightrecon_aesthetic_tool.cpp, tools/rend2/stock_aesthetic/, ASR14/15 patch/association code, native Shape prepared coefficients and original receiver footprints.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Compute unit-amplitude source-response columns before reading target RGB, using the exact evaluator, occupancy policy and source variants that fitting will use.
2. Separate emitting-side, receiver-facing, distance/range, blocked, unknown-path, zero-coefficient and positive-coefficient reasons per original receiver.
3. Choose training/validation/final partitions from original geometry, material groups, spatial blocks and footprint gaps. Retain known-dark/blocked controls and account for every excluded receiver.
4. Avoid choosing a material solely because it has many samples while every candidate has zero influence there. Require a declared minimum informative support without hiding unsupported parts of the declared region.
5. Keep every alternative on comparable evaluation denominators or explicitly report the common supported scope and its coverage loss.
6. Produce a diagnostic matrix/plate for the old mp/duel4 zero-response fixture and additional visible fixtures, with original IDs and causal failure counts.

Definition of Done:
- Zero-response and unknown-response cases are mechanically distinguishable and reproducible.
- At least one real stock fixture has independently selected informative multi-block receiver support, or a precise blocker explains why no tested fixture does.
- The sampling path never uses final RGB to choose membership and includes contradiction/dark controls.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not fix the zero-response problem by assigning nonzero coefficients, making unknown rays transparent, dropping shadowed samples, or selecting only receivers that already fit.
Implement the specified mechanism completely through its real native/orchestration path, with meaningful regression and reference checks. Do not substitute a loose conceptual approximation, a disconnected helper, a stub or a report for working integration. If the work is larger than M, complete the bounded independent work and document a concrete split for a decision; do not silently expand the ticket.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-11" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This implementation task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-12"></a>

### AR-12 — Freeze a real-stock pilot bank and assistance budget

Тип: **Управленческая / экспериментальная**. Размер: **M**.

```text
Task ID: AR-12
Task type: Decision / Experiment
Estimated size: M
Title: Freeze a real-stock pilot bank and assistance budget

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Choose a small but substantive original-stock pilot with visible, geometrically motivated sources and freeze its evidence, assistance level, alternatives and acceptance policy before fitting.

Required inputs and inspection:
Inspect current tools/rend2/stock_aesthetic/ results, exact fixture catalogues, influence/receiver diagnostics, current acceptance and modern-lighting policies, and original stock captures/assets. Reuse an existing valid pilot instead of starting over.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Select one simple interior region with several real visible fixtures, useful color/brightness motifs, static occluders and independent receiver blocks. Include difficult/negative companion regions selected before scoring.
2. Freeze all selected regions, fixture identities, masks/approximations, geometric alternatives, materials, transfers, budgets and train/validation/final partitions with hashes.
3. Declare any manual fixture inventory, mask correction, material grouping or local pose prior. Separate assistance levels; supplied anchors cannot be reported as blind discovery.
4. Check that proposed A sources are motivated by visible fixtures and illumination structure; do not demand historically exact RGB/type/coordinates.
5. Choose a limited background model and compare alternatives that could explain the same observation, including C/D where appropriate.
6. Write docs/rend2-relighting/pilot-decision.md and a machine-readable pilot manifest. Update the plan based on concrete feasibility evidence.

Definition of Done:
- The pilot bank uses original stock data, multiple source hypotheses and meaningful independent positive/negative evaluation support.
- All assumptions, manual interventions and selection decisions are frozen before final scoring.
- A geometry/input failure yields a documented decision and bounded repair tasks, not substitution of a synthetic scene labeled as stock.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not keep trying regions until one produces a favorable final score, use held-out RGB to choose fixtures/materials, or demand an untouched full-map autonomous solver before obtaining a declared assisted pilot.
Prepare the concrete experiment/decision materials, run the authorized bounded evaluation if required, and issue a supported decision with alternatives, limitations and plan consequences. A negative result is valid evidence; do not manufacture a pass or leave only a proposal to investigate later.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-12" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This is a decision/experiment task: you may revise the roadmap, dependencies, future prompts and add tasks when evidence justifies it. Record every structural/policy change and its rationale in the plan change log. Preserve superseded tasks, failed results and frozen criteria; new tasks need self-contained English prompts and S/M sizing. A changed direction must have concrete deliverables and exit gates.
```

<a id="ar-13"></a>

### AR-13 — Integrate appearance-guided RGB fitting for fixed fixtures

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-13
Task type: Implementation
Estimated size: M
Title: Integrate appearance-guided RGB fitting for fixed fixtures

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Fit complete modern fixed-fixture RGB amplitudes under the chosen appearance policy, with balanced observations and bounded background, using the existing aesthetic fitter.

Required inputs and inspection:
Inspect the frozen pilot and appearance policies, tools/lightrecon_test/aesthetic_fit.{h,cpp}, aesthetic_sparse.inc, native modern prepared responses and the original receiver snapshots.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Route the authoritative modern coefficients and actual footprint geometry into Aesthetic::FitAndValidate; remove adapter/model mismatches rather than cloning another fitter.
2. Implement the selected comparison operator, robust interval/appearance loss, saturation behavior and material-group handling in its declared domain.
3. Fit nonnegative RGB amplitudes with finite bounds and a fixed gauge. Fit only the allowed training background/nuisance with the same capacity in the no-local baseline.
4. Balance spatial regions, normal groups, material groups and represented area. Preserve correlated sample identities and comparable alternative coverage.
5. Record contributions, zero-response diagnostics, bound hits, parameter correlations, held-out readiness and approximation sensitivity.
6. Validate relative-linear references, encoded appearance, outliers, duplicate sampling, correlated lamps, zero channels and stock pilot inputs.

Definition of Done:
- The real pilot can run from frozen input to fitted fixed-geometry source parameters and validation selection with no hand-written fitted powers.
- Background cannot absorb arbitrary per-receiver error and low RGB residual cannot overrule geometric/evidence hard gates.
- The implementation is integrated with native evaluation and saves the complete replayable fitted configuration.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not fit stock RGB8 directly as physical irradiance, refit the exposure gauge with power, give every luxel its own free background, or stop after implementing an isolated NNLS example.
Implement the specified mechanism completely through its real native/orchestration path, with meaningful regression and reference checks. Do not substitute a loose conceptual approximation, a disconnected helper, a stub or a report for working integration. If the work is larger than M, complete the bounded independent work and document a concrete split for a decision; do not silently expand the ticket.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-13" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This implementation task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-14"></a>

### AR-14 — Fit only justified local shape and pose parameters

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-14
Task type: Implementation
Estimated size: M
Title: Fit only justified local shape and pose parameters

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Extend the fixed-fixture aesthetic fit to supported radius/size, spot axis/cone and small fixture-local pose alternatives without changing physical fixture identity or using global position search.

Required inputs and inspection:
Inspect frozen fixture-local pose bounds, modern-lighting/appearance policies, Aesthetic::Fixture variants, source-support checks, Shape quadrature and the pilot's fixed-geometry baseline.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Implement bounded parameterization or deterministic variants for the dimensions allowed by actual fixture evidence; unsupported dimensions remain frozen.
2. Jointly refit RGB on training for every accepted geometry variant, normalize energy correctly when area/size changes, and invalidate prepared coefficients on every relevant parameter change.
3. Validate each complete source support and independent optical path; for continuous moves certify the required support/sweep or evaluate explicitly separate static hypotheses.
4. Freeze geometry choices before validation selection and all choices before final audit. Keep equivalent variants and meaningful parameter uncertainty.
5. Compare against the fixed-geometry baseline to show whether each extra degree of freedom improves held-out visual structure without moving away from the fixture.
6. Test radius/range coupling, narrow cones, wall mounts, body size, source aliases and repeated deterministic runs.

Definition of Done:
- All enabled parameter changes have explicit fixture-derived bounds, support admission and replayable identities.
- An improved fit remains attached to its real fixture and does not exchange increased brightness for arbitrary size/pose changes without diagnostics.
- Unsupported or weakly observed parameters stay fixed/ambiguous rather than receiving invented precision.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not move sources through walls, expand a panel beyond its luminous footprint, optimize positions across the room, use final targets for parameter search, or label local coordinate convergence as a global optimum.
Implement the specified mechanism completely through its real native/orchestration path, with meaningful regression and reference checks. Do not substitute a loose conceptual approximation, a disconnected helper, a stub or a report for working integration. If the work is larger than M, complete the bounded independent work and document a concrete split for a decision; do not silently expand the ticket.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-14" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This implementation task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-15"></a>

### AR-15 — Produce offline visual evidence and spatial quality reports

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-15
Task type: Implementation
Estimated size: M
Title: Produce offline visual evidence and spatial quality reports

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Make aesthetic relighting reviewable through reproducible data-only visual diagnostics and spatial reports, without installing a runtime lighting consumer.

Required inputs and inspection:
Inspect tools/rend2/stock_aesthetic/report.py, current geometry plates/OBJ exports, Fields/Attribution diagnostics, original receiver geometry, source-response data and frozen appearance/acceptance policies.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Generate original-reference and reconstructed-appearance plates using one fixed declared display transform, exposure and color convention; show source footprints, normals, shadows and original receiver support.
2. Provide geometry-aware views or offline surface-atlas/mesh previews sufficient to assess visible light motifs, wall sides, occlusion, dark gaps and room transitions.
3. Show direct A/B contributions, C/background and D/original fallback as distinct layers, plus missing/unknown geometry masks.
4. Report per-region/material/normal-group and worst-block errors, color/brightness ordering, bound hits, source-to-fixture distance and source-count/alternative information.
5. Include all selected pilot regions, including failures. Record assistance, approximations and the named review method; automated score output is not a fabricated human approval.
6. Make all report images, sampled data and summary metrics reproducible from frozen artifacts, with licensed geometry/images kept in the appropriate private output.

Definition of Done:
- A reviewer can inspect the actual pilot result and its failure areas without reading solver internals or launching a modified game.
- Original and reconstructed views use comparable fixed presentation; auto exposure or selective views cannot hide overlighting.
- Coverage/unknown/fallback and source provenance are visible alongside quality metrics.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not modify world shading, add lights to maps, build a GPU viewer/website as a substitute for reconstruction, or deliver only an attractive screenshot with no spatial/error provenance.
Implement the specified mechanism completely through its real native/orchestration path, with meaningful regression and reference checks. Do not substitute a loose conceptual approximation, a disconnected helper, a stub or a report for working integration. If the work is larger than M, complete the bounded independent work and document a concrete split for a decision; do not silently expand the ticket.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-15" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This implementation task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-16"></a>

### AR-16 — Accept or reject the first stock region

Тип: **Управленческая / экспериментальная**. Размер: **M**.

```text
Task ID: AR-16
Task type: Decision / Experiment
Estimated size: M
Title: Accept or reject the first stock region

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Establish one genuinely useful data-only aesthetic relighting result on original stock geometry, or produce a precise failed decision that changes the work plan.

Required inputs and inspection:
Use the frozen pilot manifest, modern/appearance policies, complete fitted artifacts, independent final partition and offline visual reports. Read existing stock_aesthetic receipts and reuse successful work only when its scope and hashes match.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Freeze the selected candidate configuration from training/validation, then open and evaluate the final data once under the declared policy.
2. Review attachment, color motifs, brightness hierarchy, shadows/dark controls, visible fixture extent and parsimony; assess C/D separately from direct-source coverage.
3. Test removal of meaningful sources and alternative background/shape explanations without tuning on final targets. Record all evaluated scopes and failed companions.
4. Issue Accepted, Partial, Failed or Blocked with quantitative gates, explicit assumptions, assistance level and a named visual assessment.
5. If final results drive a design change, retire that final partition to development status and select a new independent evaluation scope; preserve old results.
6. Write docs/rend2-relighting/pilot-acceptance.md, append the plan result and revise the roadmap/prompts if needed.

Definition of Done:
- An accepted pilot contains nonzero, visually motivated sources and independently checked lighting structure, not just legal geometry or a converged process.
- Failures have causal diagnostics and bounded next actions; no silent ROI replacement or target leakage occurs.
- The report states exactly what was reconstructed, which parts are C/D, and whether the result was assisted or autonomous.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not equate historical source recovery, exact luxel reproduction or low total MSE with aesthetic success. Do not count a zero-power fixture or nuisance-only result as reconstructed local lighting.
Prepare the concrete experiment/decision materials, run the authorized bounded evaluation if required, and issue a supported decision with alternatives, limitations and plan consequences. A negative result is valid evidence; do not manufacture a pass or leave only a proposal to investigate later.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-16" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This is a decision/experiment task: you may revise the roadmap, dependencies, future prompts and add tasks when evidence justifies it. Record every structural/policy change and its rationale in the plan change log. Preserve superseded tasks, failed results and frozen criteria; new tasks need self-contained English prompts and S/M sizing. A changed direction must have concrete deliverables and exit gates.
```

<a id="ar-17"></a>

### AR-17 — Design scene-wide sunlight and nonlocal illumination handling

Тип: **Исследовательская**. Размер: **M**.

```text
Task ID: AR-17
Task type: Research
Estimated size: M
Title: Design scene-wide sunlight and nonlocal illumination handling

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Specify how global sun/sky, source-linked bounce and unexplained lighting distributions should participate in the modern aesthetic reconstruction without being converted into fictitious lamps.

Required inputs and inspection:
Inspect shared/rd-rend2/lightrecon/{sun_fit,indirect,transport_mesh,field_builder,field_basis,probes,contributions}.*, tools/lightrecon_test/iterative_discovery.*, docs/rend2-local-light-evidence-asr08.md, field/appearance reports and the ASR23 false-local-source cases.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Map existing physical, relative and observation-appearance field capabilities, geometry/visibility assumptions, material requirements and remaining implementation gaps.
2. Compare bounded source-linked bounce with conservative C appearance fields and global sun/sky hypotheses. Describe a useful pilot-to-whole-map progression.
3. Define attribution/identifiability limits and how omitted transport, sky portals, colored reflections and movers affect confidence and coverage.
4. Specify spatial support that respects walls/openings and prevents a free background from explaining arbitrary direct-light structure.
5. Recommend practical model families, independent evidence requirements, numerical controls and alternatives for missing albedo/transfer.
6. Write docs/rend2-relighting/nonlocal-lighting-research.md with concrete APIs, data contracts and a bounded experiment matrix.

Definition of Done:
- The report identifies which existing routes can represent C and which quantities remain unavailable.
- Sun, local direct light, bounce, appearance background and D fallback have distinct semantics and attribution.
- Recommendations permit useful aesthetic approximations without claiming full GI or requiring impossible historical knowledge.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not treat a doorway or reflected hotspot as a lamp by default, subtract RGB8 components as energy, infer angular sky radiance from mean RGB, or propose unlimited per-receiver residual fields.
Produce a substantive written investigation with code/data references, limitations and actionable conclusions. Read relevant primary sources if external technical evidence is needed. Separate measured facts, assumptions and recommendations. Do not start speculative implementation or new experiments outside the stated investigation.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-17" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This research task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-18"></a>

### AR-18 — Choose global and background model capacity

Тип: **Управленческая / экспериментальная**. Размер: **M**.

```text
Task ID: AR-18
Task type: Decision / Experiment
Estimated size: M
Title: Choose global and background model capacity

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Freeze a useful but restrained global/background model that reduces false lamps and preserves the visual composition of actual stock scenes.

Required inputs and inspection:
Inspect docs/rend2-relighting/nonlocal-lighting-research.md, pilot diagnostics, ASR23 sun/sky and bounce controls, modern/appearance policies and the available native global/field evaluators.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Compare no-local baselines with identical nuisance capacity, explicit sun/sky candidates, source-linked transport where supported and bounded C distributions.
2. Use controlled false-lamp cases and a preselected stock interior/exterior bank with training, validation and independent final partitions.
3. Choose background dimensionality, smoothness, wall-aware support, material assumptions, energy/power bounds and uncertainty/attribution reporting.
4. Measure whether model capacity suppresses real visible lamps or explains away high-frequency direct-light structure; include source-removal and dark-control checks.
5. Select the simplest supported model that meets the declared aesthetic policy. Retain alternatives when source-vs-field attribution is not distinguishable.
6. Write versioned nonlocal/attribution policies and a decision record. Split any unresolved implementation expansion into concrete S/M tasks.

Definition of Done:
- The chosen global/C model has explicit bounded capacity and a frozen validation/acceptance policy.
- Neither a more flexible field nor an extra local lamp wins automatically from lower training error.
- Modern aesthetic assumptions are stated independently of historical compiler identity and updated in the work plan.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not fit an unconstrained residual image, relabel background as calibrated GI, require perfect global decomposition before any local success, or tune a new background using final-test values.
Prepare the concrete experiment/decision materials, run the authorized bounded evaluation if required, and issue a supported decision with alternatives, limitations and plan consequences. A negative result is valid evidence; do not manufacture a pass or leave only a proposal to investigate later.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-18" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This is a decision/experiment task: you may revise the roadmap, dependencies, future prompts and add tasks when evidence justifies it. Record every structural/policy change and its rationale in the plan change log. Preserve superseded tasks, failed results and frozen criteria; new tasks need self-contained English prompts and S/M sizing. A changed direction must have concrete deliverables and exit gates.
```

<a id="ar-19"></a>

### AR-19 — Integrate modern sun and sky hypotheses

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-19
Task type: Implementation
Estimated size: M
Title: Integrate modern sun and sky hypotheses

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Include supported modern sun/sky alternatives in the same aesthetic prediction and validation path as local sources, with correct geometric sky reach and no double attribution.

Required inputs and inspection:
Inspect the frozen nonlocal and modern-lighting policies, Solar/sun_fit, original shader/sky evidence, static visibility, environment/field representations and Aesthetic/Joint adapters.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Generate bounded directional/environment alternatives from captured sky/source evidence and the chosen modern model, preserving uncertain direction/color/intensity where necessary.
2. Evaluate actual sky visibility and occlusion for the same receiver support used by local fitting; unknown sky reach remains unavailable.
3. Jointly fit allowed global and local amplitudes in a fixed gauge, sum compatible contributions before appearance encoding, and preserve one explicit ownership of sun/sky terms.
4. Compare solar-only, local-only, combined and C alternatives with equal nuisance capacity and frozen validation.
5. Save the selected environment/global configuration and equivalent alternatives with source/profile provenance.
6. Verify direct sun, diffuse sky, indoor lamps, mixed sun/local, occluded sky and the false-local-source negative cases.

Definition of Done:
- Supported stock exterior/interior regions can include global illumination hypotheses without fabricated local lamps or duplicated sun contribution.
- Known blocked and unknown sky paths remain distinct; no mean-grid direction becomes a certified sun automatically.
- Global and local output parameters are replayable under the same aesthetic policy and data-only contracts.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not infer an exact Raven sun, remove uncertain sky blockers, assume every bright opening is a physical source, or implement CSM/fog/renderer consumers.
Implement the specified mechanism completely through its real native/orchestration path, with meaningful regression and reference checks. Do not substitute a loose conceptual approximation, a disconnected helper, a stub or a report for working integration. If the work is larger than M, complete the bounded independent work and document a concrete split for a decision; do not silently expand the ticket.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-19" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This implementation task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-20"></a>

### AR-20 — Represent supported nonlocal illumination as category C

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-20
Task type: Implementation
Estimated size: M
Title: Represent supported nonlocal illumination as category C

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Implement the selected bounded C representation and supported source-linked transport without inventing local emitters for unexplained illumination.

Required inputs and inspection:
Inspect the frozen nonlocal/attribution policy, existing Fields::Build/ObservationAppearance, source-linked Indirect evaluation, probe topology, visibility support and canonical conditional-field quantity/calibration contracts.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Reuse the chosen existing field/transport route and implement only missing native fitting/integration needed by the frozen policy.
2. Represent supported nonlocal appearance or relative illumination with explicit quantities, domains, spatial support, bounds and provenance; leave unsupported angular/SI values unavailable.
3. Constrain interpolation across walls/openings using retained geometry and preserve unresolved visibility instead of smoothing light through obstacles.
4. Keep local direct, sun/sky, source-linked bounce, C and D ownership explicit, with no automatic positive residual used to close an energy equation.
5. Fit or build C from training only and evaluate its frozen capacity on validation/final data; use the same competing capacity in local-light alternatives.
6. Test reflected color, bright doorways, sparse observations, overflexible backgrounds, wall separation and missing material/transport evidence.

Definition of Done:
- Ambiguous illumination can be saved as a useful, bounded C result with independent validation instead of an invented lamp.
- C cannot reproduce arbitrary per-luxel targets or silently absorb every visible source.
- Attribution, spatial scope and uncertainty survive native evaluation and output.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not turn raw RGB8 residuals into additive irradiance, manufacture angular information, claim higher-bounce recovery from one-bounce support, or activate replacement of baked lighting in the game.
Implement the specified mechanism completely through its real native/orchestration path, with meaningful regression and reference checks. Do not substitute a loose conceptual approximation, a disconnected helper, a stub or a report for working integration. If the work is larger than M, complete the bounded independent work and document a concrete split for a decision; do not silently expand the ticket.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-20" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This implementation task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-21"></a>

### AR-21 — Unify evidence-aware sparse source selection

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-21
Task type: Implementation
Estimated size: M
Title: Unify evidence-aware sparse source selection

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Select a compact, visually motivated set of A/B sources under the authoritative aesthetic objective while preserving area/spot hypotheses and attribution alternatives.

Required inputs and inspection:
Inspect Aesthetic::SelectSparse and aesthetic_sparse.inc, ASR15 association, native fixture adapters, modern/global/C policies and ASR23 duplicates plus finite-source rejection cases.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Use stable physical-fixture/component/family identities and independent visible evidence; retain exact aliases without merging distinct neighboring lamps by color alone.
2. Apply hard geometry/domain gates first, then evidence-aware validation and complexity costs that do not systematically replace visible panels with unsupported point proxies.
3. Refit competing subsets and removals on training, compare frozen validation configurations, and retain equivalent alternatives instead of averaging them.
4. Include the same global/C capacity in every competing configuration. A visible but unlit or unsupported fixture need not receive nonzero power.
5. Diagnose duplicate emitters, correlated columns, weak observable contribution, bound hits, hidden-source evidence and source-versus-field ambiguity.
6. Reproduce two-color, overlapping-white, area/spot, redundant ceiling proxy, genuine multiple-fixture and reflected-light negative cases.

Definition of Done:
- The selected source set has explicit fixture evidence, stable IDs, removal rationale and replayable alternatives.
- Finite-source selection succeeds when the visible emitting geometry and independent appearance evidence support it.
- Training-only improvement, pure color proximity or count penalties cannot silently decide aesthetic eligibility.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not optimize source count for future GPU slots, force every catalogue entry to emit, merge repeated instances into one lamp, or solve duplicates by suppressing all hidden sources.
Implement the specified mechanism completely through its real native/orchestration path, with meaningful regression and reference checks. Do not substitute a loose conceptual approximation, a disconnected helper, a stub or a report for working integration. If the work is larger than M, complete the bounded independent work and document a concrete split for a decision; do not silently expand the ticket.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-21" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This implementation task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-22"></a>

### AR-22 — Add only motivated category B sources in an iterative loop

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-22
Task type: Implementation
Estimated size: M
Title: Add only motivated category B sources in an iterative loop

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Discover and validate remaining plausible sources after fitting visible fixtures and nonlocal illumination, while leaving unsupported residuals as C/D.

Required inputs and inspection:
Inspect tools/lightrecon_test/iterative_discovery.*, Hidden/adaptive/grid proposal modules, frozen attribution/evidence policies, the authoritative modern evaluator and sparse selection results.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Reuse the existing bounded discover/refit loop and connect it to the same modern/global/C prediction used for A sources.
2. Require independent local spatial/directional/shadow/fixture-context evidence for B search; a bright residual or a lower RGB error alone is insufficient.
3. Generate bounded source families within original geometry, preserve alternatives, and reject known-solid or unknown required source/receiver support.
4. Jointly refit existing and proposed sources from training, pay the declared complexity cost, and require frozen validation improvement over the incumbent without unacceptable region regression.
5. Recompute the unexplained structure after each accepted addition and stop explicitly on no supported improvement, ambiguity, limits or unavailable evidence.
6. Verify that colored bounce, solar ambiguity, saturation and reflected doorways do not automatically become local lamps; retain all stop/accept/reject receipts.

Definition of Done:
- Additional B sources can be accepted for aesthetic use with provenance and uncertainty while historical identity stays unknown.
- Unsupported residuals produce C/D decisions rather than forced local lights.
- The final held-out partition cannot initialize, refit, accept or rerank sources.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not infer a source merely because the direct model is incomplete, copy the old historical residual objective into the modern path, scan unrestricted room XYZ, or call conditional B candidates historically confirmed.
Implement the specified mechanism completely through its real native/orchestration path, with meaningful regression and reference checks. Do not substitute a loose conceptual approximation, a disconnected helper, a stub or a report for working integration. If the work is larger than M, complete the bounded independent work and document a concrete split for a decision; do not silently expand the ticket.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-22" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This implementation task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-23"></a>

### AR-23 — Pass a modern aesthetic adversarial validation bank

Тип: **Управленческая / экспериментальная**. Размер: **M**.

```text
Task ID: AR-23
Task type: Decision / Experiment
Estimated size: M
Title: Pass a modern aesthetic adversarial validation bank

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Decide whether the assembled modern source/field pipeline is ready for whole-map stock reconstruction using blind controlled tests and independent visual stock-region review.

Required inputs and inspection:
Inspect the frozen aesthetic acceptance policy, current integrated native pipeline, docs/rend2-blind-benchmark-asr23.md and its untouched receipts, and predeclared stock pilot/companion regions.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Preserve ASR23 as historical evidence and create a new policy/version for the modern aesthetic objective rather than silently reinterpreting its source-recovery gates.
2. Freeze development, validation and final scene families covering spots, finite panels/tubes, multiple nearby lights, sunlight, colored bounce, openings, walls, masks and no-local-light cases.
3. Evaluate detection/geometry, appearance composition, shadows, false unmotivated sources and C/D coverage separately. Historical type/XYZ truth is only a controlled diagnostic, not a stock requirement.
4. Run relevant ablations with equal declared limits and the same authoritative fitter; verify area/spot behavior contributes where appropriate rather than remaining a no-op.
5. Audit leakage, comparable receiver denominators, assistance, model capacity, repeats and source/input hashes.
6. Issue a readiness decision. Turn failed causal classes into specific bounded tasks and update the roadmap; do not merely append more benchmarks indefinitely.

Definition of Done:
- Independent evidence supports or rejects whole-map readiness under predeclared aesthetic gates.
- Known duplicates, finite-source omissions and global-light ghosts have measured dispositions, including explicit unresolved limitations.
- Any plan change is justified by evidence and preserves failed/frozen results.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not tune on final scenes, require exact historical source counts for stock, declare success from mean error while salient regions fail, or weaken geometry and Unknown handling to pass.
Prepare the concrete experiment/decision materials, run the authorized bounded evaluation if required, and issue a supported decision with alternatives, limitations and plan consequences. A negative result is valid evidence; do not manufacture a pass or leave only a proposal to investigate later.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-23" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This is a decision/experiment task: you may revise the roadmap, dependencies, future prompts and add tasks when evidence justifies it. Record every structural/policy change and its rationale in the plan change log. Preserve superseded tasks, failed results and frozen criteria; new tasks need self-contained English prompts and S/M sizing. A changed direction must have concrete deliverables and exit gates.
```

<a id="ar-24"></a>

### AR-24 — Plan complete map work from original geometry

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-24
Task type: Implementation
Estimated size: M
Title: Plan complete map work from original geometry

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Create a bounded scene-wide work graph that covers an entire original stock map and supplies reproducible local fitting jobs without choosing regions by favorable RGB results.

Required inputs and inspection:
Inspect SceneSchedule/scene_schedule.*, region graphs, original receiver/fixture inventories, current stock_aesthetic orchestration, and the frozen aesthetic map-coverage policy.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Partition original static geometry into meaningful connected regions and overlap/boundary scopes, with stable IDs and a fixed eligible-area census.
2. Assign fixture components, global hypotheses, C support and original receiver samples to jobs while retaining shared sources and cross-region visibility.
3. Generate spatial/material/normal-balanced training, validation and final support before fitting, with footprint/filter margins and explicit correlation handling.
4. Represent missing models, unknown geometry, unsupported materials, dark regions and no-source regions as first-class jobs/outcomes.
5. Bound scheduling and memory without coupling source selection to renderer limits; distinguish budget-exhausted work from completed empty results.
6. Export an inspectable map work graph, all denominators, adjacency/overlap records and deterministic local request manifests.

Definition of Done:
- Every eligible original region belongs to a documented work scope, including unsupported and no-light cases.
- Jobs and evaluation membership are target-independent and reproducible across order changes.
- A map cannot appear complete merely because only easy regions were scheduled.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not discover rooms from fitted residuals alone, drop failed jobs from the map denominator, impose a future runtime light budget, or create a second independent region identity system unnecessarily.
Implement the specified mechanism completely through its real native/orchestration path, with meaningful regression and reference checks. Do not substitute a loose conceptual approximation, a disconnected helper, a stub or a report for working integration. If the work is larger than M, complete the bounded independent work and document a concrete split for a decision; do not silently expand the ticket.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-24" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This implementation task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-25"></a>

### AR-25 — Reconcile shared sources and lighting across map regions

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-25
Task type: Implementation
Estimated size: M
Title: Reconcile shared sources and lighting across map regions

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Combine regional fits into one consistent map-level data configuration with stable sources, a common brightness gauge and explicit unresolved boundaries.

Required inputs and inspection:
Inspect the complete map work graph, authoritative aesthetic fitter, source/fixture lineage, global/C models, source contributions and existing joint/scene orchestration.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Identify shared physical fixtures by attested aliases/instance/component identity, preserving genuinely separate nearby lights and mutually exclusive variants.
2. Resolve overlapping regional proposals into consistent source membership and refit shared RGB/global parameters on the combined training support with the fixed gauge.
3. Use bounded connected-component and overlap refits with explicit shared-parameter dependencies; do not require a new monolithic whole-map optimizer.
4. Propagate cross-room source influence and respect walls/openings; quantify boundary disagreement, C continuity and region-specific coverage.
5. Prevent room-by-room exposure, background or transfer choices from hiding seams or creating incompatible powers for the same source.
6. Retain competing map configurations and D boundaries when the evidence cannot resolve a conflict.
7. Validate adjacent warm/cool rooms, a shared corridor fixture, a doorway, overlapping region schedules and a multiroom stock candidate.

Definition of Done:
- One map-level source/configuration identity reproduces all accepted regional fits within the declared consistency tolerances.
- Duplicate sources and brightness seams have explicit measured dispositions.
- Unresolved joins remain Partial/D and are not concealed by independent room exposures.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not concatenate local light lists and call the map solved, merge by proximity/color only, smooth fields across opaque walls, or use held-out room data to repair training parameters.
Implement the specified mechanism completely through its real native/orchestration path, with meaningful regression and reference checks. Do not substitute a loose conceptual approximation, a disconnected helper, a stub or a report for working integration. If the work is larger than M, complete the bounded independent work and document a concrete split for a decision; do not silently expand the ticket.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-25" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This implementation task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-26"></a>

### AR-26 — Accept the first complete stock map

Тип: **Управленческая / экспериментальная**. Размер: **M**.

```text
Task ID: AR-26
Task type: Decision / Experiment
Estimated size: M
Title: Accept the first complete stock map

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Accept, partially accept or reject one whole original stock map as a data-only aesthetic relighting deliverable under frozen coverage and visual-quality gates.

Required inputs and inspection:
Inspect the frozen map acceptance policy, original inventory/census, complete map work graph, consistent fitted configuration, export validator and offline review packet.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Freeze the map configuration and all assumptions before evaluating its independent final support.
2. Review every declared room/region, major color motif, visible-fixture attachment, dark control, exterior/interior transition and unresolved high-salience location.
3. Report reconstructed A/B coverage, independently supported C coverage, D/original fallback, unsupported geometry and execution completeness separately.
4. Perform source-removal, alternative-shape/background and sensitivity checks on development/validation evidence; report final scores without refitting.
5. Verify the saved data can be loaded and reproduce the reviewed predictions without the fitting process or original optimizer state.
6. Write a map acceptance record with stable release identity, limitations and concrete repair tasks. A failed or partial map cannot be relabeled complete.

Definition of Done:
- At least one map has a substantive accepted data-only result or a precise failed/partial decision with all original coverage retained.
- Success does not depend on unknown historical positions, source count or SI powers.
- A reviewer can reproduce the accepted scene from saved data and fixed display/appearance settings.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not report one successful room as an entire map, treat original-lighting fallback as newly reconstructed area, modify the BSP or activate relighting in the game.
Prepare the concrete experiment/decision materials, run the authorized bounded evaluation if required, and issue a supported decision with alternatives, limitations and plan consequences. A negative result is valid evidence; do not manufacture a pass or leave only a proposal to investigate later.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-26" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This is a decision/experiment task: you may revise the roadmap, dependencies, future prompts and add tasks when evidence justifies it. Record every structural/policy change and its rationale in the plan change log. Preserve superseded tasks, failed results and frozen criteria; new tasks need self-contained English prompts and S/M sizing. A changed direction must have concrete deliverables and exit gates.
```

<a id="ar-27"></a>

### AR-27 — Freeze a faithful data-only output contract

Тип: **Управленческая / экспериментальная**. Размер: **S**.

```text
Task ID: AR-27
Task type: Decision / Experiment
Estimated size: S
Title: Freeze a faithful data-only output contract

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Choose a complete representation of aesthetic A/B/C/D decisions and modern model policy using existing canonical contracts plus explicit offline metadata, without conflating aesthetic usability with historical certainty.

Required inputs and inspection:
Inspect tr_lightrecon_types.*, lightrecon/{disk,producer,contributions,field_builder,field_codec,shape_selection}.*, current calibration/conditional-field contracts, frozen modern-lighting and coverage policies, and a reviewed fitted region.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Map A/B sources to existing PhysicalEmitter/Candidate, geometry, relative photometry, provenance, configuration and alternatives without requiring exactLight or fabricating measured existence confidence.
2. Map C to a faithful supported conditional field/transport quantity and D to explicit original-observation/fallback references with region/receiver scope.
3. Specify the saved modern-law/radius/range policy, emitting footprint aliases, assistance, assumptions, coverage, quality receipts and input dependencies.
4. Use a versioned offline sidecar/manifest for aesthetic decisions and model details that are not canonical physical quantities. Preserve all current canonical type/codec semantics.
5. Test whether an existing format faithfully carries every required value. If a genuine semantic gap remains, document it and add a narrowly scoped decision/implementation task rather than stuffing data into unrelated fields.
6. Publish a readable data contract, machine-readable schema/validation examples, invalid counterexamples and a backward-compatibility decision.

Definition of Done:
- All A/B/C/D outcomes and selected/equivalent configurations have precise data representations and calibration/uncertainty semantics.
- Saving an aesthetically usable candidate does not mark it historically Resolved or bypass canonical validation.
- Existing assets remain interpretable, and a consumer-free replay can identify exactly which modern policy produced the result.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not extend canonical schemas casually, overload rangeWorld or exactLight with new meanings, invent a second physical-light taxonomy, or design a renderer replacement/double-lighting contract.
Prepare the concrete experiment/decision materials, run the authorized bounded evaluation if required, and issue a supported decision with alternatives, limitations and plan consequences. A negative result is valid evidence; do not manufacture a pass or leave only a proposal to investigate later.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-27" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This is a decision/experiment task: you may revise the roadmap, dependencies, future prompts and add tasks when evidence justifies it. Record every structural/policy change and its rationale in the plan change log. Preserve superseded tasks, failed results and frozen criteria; new tasks need self-contained English prompts and S/M sizing. A changed direction must have concrete deliverables and exit gates.
```

<a id="ar-28"></a>

### AR-28 — Export and replay validated aesthetic reconstruction data

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-28
Task type: Implementation
Estimated size: M
Title: Export and replay validated aesthetic reconstruction data

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Implement the complete saved-data path and standalone replay validator for the accepted aesthetic contract, without producing or activating runtime lights.

Required inputs and inspection:
Inspect the frozen aesthetic output schema/decision, existing Producer/disk/chunk/calibration APIs, conditional field codecs, contribution records and complete reviewed fit artifacts.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Serialize selected A/B source parameters, actual footprint/support references, global/C results, D references, alternative configurations and modern model-policy identity.
2. Preserve stable source/fixture/region lineage and explicit relative quantities, assumptions, assistance and review scope.
3. Write immutable dependency manifests and content hashes for BSP, shaders, images, models, observations, policies and derived geometry.
4. Validate all references, finite/bounded parameters, A/B/C/D domains, source-family exclusivity, coverage totals and canonical invariants before publishing files.
5. Implement standalone load/replay of predictions and diagnostic views from the saved data; do not require optimizer internals or hidden answer files.
6. Test round trips, stale inputs, unsupported versions, corrupt chunks, missing footprints, alias collisions and equivalence/unknown records.

Definition of Done:
- A complete fitted region/map can be saved, validated, reloaded and reproduce reviewed numerical predictions within frozen tolerances.
- The data contains both successful reconstruction and explicit C/D/unsupported scope; no missing piece is silently invented.
- No renderer state, GPU object, BSP edit or in-game light activation occurs.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not deliver only diagnostic JSON that cannot reproduce the selected model, bypass canonical validators, or mark export success as visual acceptance.
Implement the specified mechanism completely through its real native/orchestration path, with meaningful regression and reference checks. Do not substitute a loose conceptual approximation, a disconnected helper, a stub or a report for working integration. If the work is larger than M, complete the bounded independent work and document a concrete split for a decision; do not silently expand the ticket.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-28" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This implementation task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-29"></a>

### AR-29 — Make corpus processing resumable and dependency-safe

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-29
Task type: Implementation
Estimated size: M
Title: Make corpus processing resumable and dependency-safe

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Run and resume original-stock reconstruction jobs reliably across maps with immutable inputs, correct invalidation and bounded offline resources.

Required inputs and inspection:
Inspect tools/rend2/all_stock/, current stock_aesthetic and aesthetic_relighting orchestration, scene scheduling, prepared caches, dependency manifests and the validated output/replay contract.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Create a deterministic batch runner over a frozen original inventory with per-map/per-region preparation, fitting, validation, review-packet and export states.
2. Cache only compatible geometry/response data, keyed by map/assets, emitting support, modern model, pose/size/range, material/appearance policy and occupancy assumptions.
3. Resume interrupted jobs without reusing stale target-dependent state or mixing policy revisions; preserve failed attempts and source histories.
4. Distinguish Completed, Partial, Failed, Blocked, Budget and MissingInput from Accepted, and aggregate all coverage denominators.
5. Record time/RSS/work separately from quality; support bounded sharding without future renderer limits or silent truncation.
6. Test interruption, changed textures/models/policies, reordered jobs, corrupted artifacts and exact repeat/resume equivalence.

Definition of Done:
- A multi-map run can resume safely and produces the same semantic results as a clean run under the same frozen policy.
- Changed inputs invalidate all affected downstream artifacts while unaffected work is reused.
- Corpus reports retain every map and failed/unknown region rather than only successful outputs.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not call a finished process an accepted reconstruction, reuse caches across different modern laws or masks, delete earlier failures, or optimize quality by dropping expensive regions.
Implement the specified mechanism completely through its real native/orchestration path, with meaningful regression and reference checks. Do not substitute a loose conceptual approximation, a disconnected helper, a stub or a report for working integration. If the work is larger than M, complete the bounded independent work and document a concrete split for a decision; do not silently expand the ticket.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-29" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This implementation task may update its own status, evidence and result summary but must not silently rewrite product scope, acceptance gates, the roadmap or other prompts. Record proposed changes for an explicit decision.
```

<a id="ar-30"></a>

### AR-30 — Validate transfer to a representative stock-map bank

Тип: **Управленческая / экспериментальная**. Размер: **M**.

```text
Task ID: AR-30
Task type: Decision / Experiment
Estimated size: M
Title: Validate transfer to a representative stock-map bank

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Determine whether the first-map solution generalizes to stock scenes with different visual and geometric characteristics before large-scale acceptance.

Required inputs and inspection:
Inspect the original map inventory, accepted-map receipts, current failure taxonomy, fixed model/policies and batch runner. Use existing stock coverage reports to choose representative cases before final scoring.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Freeze a development/validation/final map bank covering colored interiors, outdoor sun/sky, screens/panels, strips/tubes, static model fixtures, mixed materials, complex occlusion and mover uncertainty.
2. Apply the same modern policy and assistance rules; identify any allowed per-map artistic settings in advance rather than fitting unrestricted exposure.
3. Evaluate map and area coverage, high-salience failures, A/B plausibility, C quality, D budgets, shadows and visual composition with the same acceptance procedure.
4. Separate automatic and assisted results and document manual effort by map/category.
5. Choose which specific capability gaps merit implementation and which can honestly remain documented C/D within the accepted product scope.
6. Update the plan and concrete prompts from measured failures; create a fresh final bank if final results informed changes.

Definition of Done:
- The generalization decision is based on representative original maps and frozen criteria, not repeated tuning of the first successful scene.
- Each unsupported scene class has a concrete implementation/decision disposition.
- The next corpus milestone has explicit policy, assistance and coverage commitments.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not substitute controlled compiled scenes for stock evidence, report only favorable maps, treat original source truth as available, or hide per-map manual corrections.
Prepare the concrete experiment/decision materials, run the authorized bounded evaluation if required, and issue a supported decision with alternatives, limitations and plan consequences. A negative result is valid evidence; do not manufacture a pass or leave only a proposal to investigate later.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-30" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This is a decision/experiment task: you may revise the roadmap, dependencies, future prompts and add tasks when evidence justifies it. Record every structural/policy change and its rationale in the plan change log. Preserve superseded tasks, failed results and frozen criteria; new tasks need self-contained English prompts and S/M sizing. A changed direction must have concrete deliverables and exit gates.
```

<a id="ar-31"></a>

### AR-31 — Reach the half-corpus acceptance milestone

Тип: **Управленческая / экспериментальная**. Размер: **M**.

```text
Task ID: AR-31
Task type: Decision / Experiment
Estimated size: M
Title: Reach the half-corpus acceptance milestone

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Establish accepted aesthetic relighting for at least half the frozen original-stock map inventory while reporting complete corpus execution and spatial coverage.

Required inputs and inspection:
Inspect the frozen original inventory, acceptance policy, representative-map decision, validated batch/export tools and per-map/region results. The historical local inventory has 57 maps; verify the current frozen denominator and override rules.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Freeze the milestone map set and acceptance criteria before the final evaluation. For a 57-map inventory, at least 29 maps must actually pass, not merely execute.
2. Run every selected map with the declared assistance policy and full original region census; keep unsuccessful and unprocessed maps visible.
3. Report accepted maps, A/B reconstructed area, C supported field area, D/original fallback, unknown geometry and high-salience exceptions separately.
4. Audit source provenance, duplicate fixtures, wall leakage, color motifs, region outliers, round-trip replay and repeatability on a stratified sample.
5. Classify remaining failures by cause and expected repair scope rather than promising a percentage from candidate counts.
6. Issue a milestone decision and revise remaining tasks/prompts with concrete S/M repairs and honest scope implications.

Definition of Done:
- At least half the frozen maps pass their whole-map aesthetic/data gates, or the report explicitly records that the milestone failed.
- A map count cannot hide poor area coverage or excessive original-lighting fallback.
- The remaining-map plan names substantive blockers, measurable next experiments and bounded implementation work.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not count processed maps, admitted candidates, copied original lighting or successful regions as accepted whole maps. Do not change the inventory denominator after seeing failures.
Prepare the concrete experiment/decision materials, run the authorized bounded evaluation if required, and issue a supported decision with alternatives, limitations and plan consequences. A negative result is valid evidence; do not manufacture a pass or leave only a proposal to investigate later.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-31" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This is a decision/experiment task: you may revise the roadmap, dependencies, future prompts and add tasks when evidence justifies it. Record every structural/policy change and its rationale in the plan change log. Preserve superseded tasks, failed results and frozen criteria; new tasks need self-contained English prompts and S/M sizing. A changed direction must have concrete deliverables and exit gates.
```

<a id="ar-32"></a>

### AR-32 — Resolve the remaining corpus gaps without an endless research loop

Тип: **Управленческая / экспериментальная**. Размер: **M**.

```text
Task ID: AR-32
Task type: Decision / Experiment
Estimated size: M
Title: Resolve the remaining corpus gaps without an endless research loop

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Turn the remaining original-map failures into a finite, prioritized repair program and decide which documented limitations are acceptable for the final data-only product.

Required inputs and inspection:
Inspect current all-map failure/coverage reports, frozen acceptance and output contracts, rejected visual packets and the implemented original-geometry/material/source adapters.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Group remaining failures into exact-footprint/material support, model/mover participation, curved/unknown geometry, source selection, global/C attribution, numerical scaling or genuinely insufficient evidence.
2. For each group choose a bounded repair, a targeted experiment, an explicitly assisted correction tied to original fixtures, or a C/D disposition consistent with product gates.
3. Create fully standalone English prompts for concrete new tasks, each with type, S/M size, inputs, action plan, DoD, non-goals and mandatory plan-result append. Split anything estimated L/XL before execution.
4. Preserve every failed run, original criterion and assistance distinction. If acceptance policy changes, document the rationale and evaluate on fresh independent scopes.
5. Set an exit decision for every remaining group; speculative research without a measurable product decision is not a completion path.
6. Update the plan, dependency table, result ledger and final-corpus acceptance inputs.

Definition of Done:
- No remaining failure is hidden behind a generic 'improve the solver' task.
- Every proposed change has evidence, a bounded deliverable and a measurable exit condition.
- Any allowed C/D limitation is visible in final coverage and cannot convert a substantially incomplete map into a complete one.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not author unmotivated extra lights to make a map pass, indefinitely increase model/background flexibility, invent unavailable geometry, or declare all maps complete by relabeling failures as artistic choices.
Prepare the concrete experiment/decision materials, run the authorized bounded evaluation if required, and issue a supported decision with alternatives, limitations and plan consequences. A negative result is valid evidence; do not manufacture a pass or leave only a proposal to investigate later.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-32" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This is a decision/experiment task: you may revise the roadmap, dependencies, future prompts and add tasks when evidence justifies it. Record every structural/policy change and its rationale in the plan change log. Preserve superseded tasks, failed results and frozen criteria; new tasks need self-contained English prompts and S/M sizing. A changed direction must have concrete deliverables and exit gates.
```

<a id="ar-33"></a>

### AR-33 — Accept the complete original-stock relighting dataset

Тип: **Управленческая / экспериментальная**. Размер: **M**.

```text
Task ID: AR-33
Task type: Decision / Experiment
Estimated size: M
Title: Accept the complete original-stock relighting dataset

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and actual revision/remote before editing. Preserve unrelated and concurrent changes; verify and reuse newer completed work.

Product objective:
Produce data-only Aesthetic Relighting of original stock maps: a compact set of visually motivated modern sources attached to real lamps, screens, luminous BSP/model geometry, plus justified nonlocal illumination. Original lighting is a visual reference, not a literal historical inverse problem. A means a visibly supported fixture, B a plausible independently motivated unseen source, C supported illumination distribution without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. A/B aesthetic suitability is independent of historical exactLight certification.

Mandatory boundaries:
Keep historical solver behavior and existing canonical/storage semantics intact. Do not modify BSP/lightmaps, author unmotivated new lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Save model/transfer/gauge assumptions and provenance; never promote a hypothetical lamp to historical truth. Approved bounded approximations must be explicit and validated.

Objective:
Deliver and audit the final data-only aesthetic relighting dataset for the entire frozen stock-map corpus with explicit per-map quality, coverage and residual limitations.

Required inputs and inspection:
Inspect the original inventory, final acceptance/assistance policies, all map/region receipts, validated exports, replay tools and every pending/blocked plan item. Use the current actual code and frozen artifact identities.
Use the current versions of named input artifacts. If a required decision/input is absent, record the precise blocker instead of inventing it, and complete useful independent inspection. No previous chat or another task prompt is required.

High-level action plan:
1. Freeze the release source/policy revision and complete inventory, then evaluate every map and region under the final independent acceptance protocol.
2. Audit A/B fixture motivation, shape/pose/color/power, global/C illumination, D fallback, unknown geometry, seams, shadows and major original color/brightness motifs.
3. Verify canonical/sidecar validation, selected/alternative configurations, asset hashes, missing-input behavior, round-trip replay and deterministic reruns.
4. Publish per-map Complete/Partial/Unsupported/Blocked status and separate numerical execution coverage from accepted reconstruction coverage.
5. Deliver a release manifest, machine-readable corpus summary, offline visual review index, reproduction instructions, known limitations and any explicitly remaining manual decisions.
6. Mark the plan complete only if its frozen completion gates actually pass. Otherwise record a partial release and concrete remaining tasks.

Definition of Done:
- Every original map has a reproducible saved result or a precisely reported non-success state; no map or unsupported area disappears.
- All maps claimed fully relit meet the declared aesthetic and data-quality gates with the permitted, disclosed C/D budget.
- The final data can be consumed later without changing the interpretation of existing assets or claiming historical source recovery.
- The result ledger and plan accurately distinguish complete delivery, partial coverage and unresolved work.
- Deliver the complete task artifact, appropriate verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not ship GPU/runtime consumers, modify world shading/BSP lighting, tackle double lighting, call renderer screenshots the data deliverable, or mark completion merely because all scripts exited successfully.
Prepare the concrete experiment/decision materials, run the authorized bounded evaluation if required, and issue a supported decision with alternatives, limitations and plan consequences. A negative result is valid evidence; do not manufacture a pass or leave only a proposal to investigate later.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md. Append a high-level result under "AR-33" in its Results Log, even when the outcome is Partial, Failed or Blocked. Include date/revision, actual scope, what was learned/implemented, measured result and artifact paths/hashes, A/B/C/D and coverage implications, assumptions/limitations, decision and concrete next action. Update the task's status honestly. A successful build/process is not aesthetic acceptance. Do not claim checks or measurements that were not performed. This is a decision/experiment task: you may revise the roadmap, dependencies, future prompts and add tasks when evidence justifies it. Record every structural/policy change and its rationale in the plan change log. Preserve superseded tasks, failed results and frozen criteria; new tasks need self-contained English prompts and S/M sizing. A changed direction must have concrete deliverables and exit gates.
```

## 7. Журнал результатов / Results Log

Этот раздел пополняется после **каждой** задачи. В таблице очереди меняется только её статус; содержательное summary всегда дописывается сюда. Технические подробности и большие таблицы — в отдельных отчётах с путями и версиями.

### Шаблон записи

```text
Task ID:
Date / revision / branch:
Task status: Done | Blocked | Deferred | Superseded
Outcome: Accepted | Partial | Failed | Unknown | Blocked
Actual scope and assistance:
High-level result:
Measured evidence and artifact paths / hashes:
A/B/C/D, coverage and uncertainty implications:
Decision / plan changes (decision tasks only):
Limitations and concrete next action:
```

Достаточно 5–12 содержательных предложений плюс ссылки; не копировать сюда весь технический отчёт. Если исследование/эксперимент завершён с отрицательным результатом, статус задачи может быть Done, но её Outcome и product gate остаются Failed/Unknown. Незавершённую реализацию не помечать Done.

### Planning baseline — 2026-11-10

План составлен по исходному коммиту `68585a75e`, ASR09–23, текущим native API и незакоммиченной разработке `stock_aesthetic`. Это действие создало документ и автономные задания; оно **не исполняло AR-01–AR-33**, не утверждало современную модель, не принимало карту и не меняло код реконструкции. Все новые задачи остаются Pending. Первое действие исполнителя — сверка свежих completed/in-flight результатов.

<!-- Append task result entries below. Do not replace prior entries. -->

## 8. Журнал изменений плана / Plan Change Log

| Дата | Основание / evidence | Решение | Затронутые задачи / версии | Последствия для проверки |
|---|---|---|---|---|
| 2026-11-10 | Текущий код и отчёты ASR09–23, рабочая разработка stock_aesthetic | Исходный data-only roadmap: 33 S/M-задачи; современная aesthetic цель отделена от исторической идентификации | AR-01–AR-33; численные политики ещё предстоит утвердить | Ни один исходный frozen benchmark не переписан; новые final gates требуют предварительной фиксации |

Управленческие/экспериментальные задачи добавляют сюда изменения. Новые задачи получают новые ID начиная с AR-34, оценку S/M и полный автономный английский промпт. При замене задачи сохраняются её прежняя запись и ссылка на основание пересмотра.

