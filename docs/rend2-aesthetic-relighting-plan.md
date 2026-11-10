# План завершения Aesthetic Relighting стоковых карт

Дата исходной редакции: **2026-11-10**, Australia/Sydney. Статус: **план работ, не отчёт об успешной реконструкции**.

**Редакция 2 — 2026-11-10:** учтены ASR24/25 и риск повторить длинный инфраструктурный путь без убедительного результата. Первый условный стоковый источник уже продемонстрирован; ближайший рубеж — связный участок с несколькими светильниками и независимой визуальной/пространственной проверкой. **Начинать с AR-34, затем AR-35 и AR-36, а не с последовательного исполнения AR-01–AR-33.** Эти три задачи имеют ограниченный бюджет и обязательное решение Go / Repair / Pivot / Stop. План не является гарантией завершения всего корпуса.

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

При исходной инспекции ASR24 находился в рабочем дереве. **В редакции 2 исходная точка обновлена:** ASR24 закоммичен как `bddf5b376`, пакет ASR25 подготовлен в индексе и содержит проверяемые отчёты/manifest. Публичные артефакты обоих manifest сверены по хешам при этом пересмотре. Их результаты использовать как существующие свидетельства, не реализовывать заново. Точное текущее состояние индекса/коммита всё равно сверять перед исполнением; staged-изменения ASR25 не перезаписывать.

| Возможность | Что уже имеется | Как использовать в этом плане |
|---|---|---|
| Оригинальные данные и идентичность | Все 57 оригинальных BSP доступны; ASR25 подтвердил только 37 существующих capture-файлов, 45 завершённых каталогов и 210 observation ROI на 35 картах | Старый manifest не доказывает наличие файла. Восстановление входов и ограничений каталогизации — отдельная измеренная ветвь; не строить второй capture pipeline. |
| Светящиеся BSP-маски | ASR09 уже вычисляет UV-clipped polygon unions и тёмные промежутки | Нужен корректный native bridge, а не повторная реализация извлечения. |
| Статические MD3 | ASR10/11 уже описывают экземпляры и излучающие области | Подключить при сохранении transform/instance/occlusion uncertainty. |
| Геометрическая допустимость | ASR17/18 исправили сторону и контакт: 368/620 кандидатов допустимы в фиксированной выборке, 320 условно | Это не 368 реконструированных ламп. Сохранить отрицательные геометрические проверки. |
| Современная форма | ASR19: point/spot/rectangle/tube, exact original unions, причины и неопределённость | Подключить реальные clipped masks и не выдавать плоскую полосу за цилиндр. |
| Настройка и отбор | ASR20/21: положительные RGB, fixed gauge, bounded nuisance, sparse selection, независимый held-out | Это база авторитетного fit; не плодить ещё один optimizer в каждом benchmark. |
| Дополнительный поиск | ASR22: bounded proposal/refit, разделение direct/indirect/solar ambiguity | Подключить к единой современной модели и политике A/B/C/D. |
| Слепая проверка | ASR23: 14/21 найденных корней и 5 ложных у all-cues; area-off не меняет результат; spot/finite-source случаи остаются проблемой | Сохранить как регрессионные свидетельства, но не превращать исторические source-truth gates в критерии стокового ремастера. |
| Положительный стоковый результат | ASR24: один принятый условный источник на mp/duel10; ASR25: дополнительный на mp/duel7. Есть геометрическое обоснование, validation/held-out и frozen source removal | Базовая жизнеспособность локального fit подтверждена условно. Не повторять поиск первого ненулевого источника; проверить пространственную/визуальную достаточность и перенос на несколько ламп. |
| Измеренные ограничения корпуса | ASR25: 20 отсутствующих captures; static-map guard блокирует 23 карты; work budget каталогов — 11; polygon budget — 1; section refusal — 2 | Эти классы пересекаются. Это не 53 доказанных физических неудачи реконструкции: нужен выбор конкретной ветви восстановления входов/локальных гарантий/бюджетов. |
| Данные/поля | Canonical asset, calibration/provenance/configurations, Fields, Probes, Attribution и disk/chunks уже существуют | Использовать существующие семантики; специфическую aesthetic policy хранить явно, не менять молча старые контракты. |

Важный технический разрыв: `effectiveRadiusWorld` уже есть в canonical geometry, но текущая `Shape::Validate` запрещает его ненулевое значение. `rangeWorld` описан как culling metadata, а не автоматически как художественное затухание. Поэтому фраза «используем стандартную современную физику» требует отдельного решения и реализации **современного** поведения с неизменной исторической веткой.

Ключевые документы для исходной сверки:

- [ASR18](rend2-mounted-admission-asr18.md), [ASR19](rend2-modern-shapes-asr19.md);
- [ASR20](rend2-aesthetic-fit-asr20.md), [ASR21](rend2-sparse-source-selection-asr21.md), [ASR22](rend2-iterative-discovery-asr22.md);
- [ASR23](rend2-blind-benchmark-asr23.md) и [численные результаты](../tools/rend2/blind_benchmark/results/v1/tables.md);
- [ASR24: условный стоковый источник](rend2-stock-aesthetic-asr24.md), [ASR25: полный аудит инвентаря](rend2-stock-discovery-asr25.md), [карты и причины остановки](../tools/rend2/stock_audit/results/v1/maps.md);
- [BSP fixtures](rend2-bsp-fixtures-asr09.md), [model emission](rend2-model-emission-asr11.md), [association](rend2-fixture-association-asr15.md);
- [CPU fields](rend2-lighting-field-builder-p01.md), [attribution](rend2-contributions-p06.md).

## 3. Стратегия и промежуточные результаты

### 3.1. Что именно изменили ASR24 и ASR25

| Свидетельство | Что установлено | Что ещё не установлено |
|---|---|---|
| ASR24, mp/duel10, BSP surface 942 | Принят один fixture-associated point-прокси; held-out MSE 174.230938 → 95.075591 (−45.43%), robust loss −33.22%; 93 различных согласующихся grid records; 17/17 held-out receivers меняются при удалении источника | Это один исходный receiver chart, 17 held-out rows в одном блоке; point-прокси с фиксированным 4 WU stand-off, условные transfer/material и фон. Это не реконструированная комната. |
| ASR25, mp/duel7, BSP surface 1657 | Второй цветной стоковый пример; held-out MSE 1196.505292 → 1000.383821 (−16.39%), robust loss −11.82%; 48 held-out rows в трёх блоках, source removal ухудшает результат | Максимальный остаток ≈177.59 RGB code; только три согласующихся grid records; яркое пятно остаётся необъяснённым. Пригодность всего участка для ремастера не принята. |
| ASR25, инвентарь | Учтены 57 BSP; завершены пять повторяемых numerical ROI; два bounded one-source результата; per-map статусы 2 AestheticCandidate / 1 NoAcceptedCandidate / 1 NoSupportedCandidate / 53 Blocked | «Две карты с принятым ROI» не означает «две карты реконструированы». SP-успеха и принятой площади whole-map пока нет. |

Итак, утверждение «на стоке вообще нет положительного результата» устарело. Но оно не заменяется утверждением «основная задача решена». Самая рискованная следующая проверка — **несколько реальных ламп, несколько окружающих поверхностей, тени/тёмные зоны и связная световая композиция**, а не ещё один удачный fit одного chart.

ASR24/25 использовали exploratory selection; их открытые final rows теперь служат development/regression evidence для новых решений. Для нового утверждения о переносе нужен заранее зафиксированный свежий геометрический/пространственный scope. Geometry plates и response plates — диагностика, не готовые remaster-скриншоты. Число grid records — corroboration, не вероятность исторического источника и не обязательный универсальный gate A-источника.

### 3.2. Короткая обязательная проверка до крупной реализации

**AR-34, 2026-11-10:** [решение и замороженный protocol v1](rend2-relighting/viability-decision.md) завершены с исходом **Blocked-SourceSupport**, а не с принятием multi-fixture участка. ASR25 уже закоммичен в `dff8ea77a`. Следующее действие — **AR-41 (S)**: установить причину отказа named contact plane на двух исходных наклонных светильниках. AR-35 заблокирован до предметного разрешения этого решения; при отрицательном результате AR-36 получает blocked receipt без выдуманного fit. V1 допускает один real-target fit/repeat после admission, одну M-интеграцию и максимум одну M-починку до открытия original final. Резерв не имеет контрольных пар для одного светильника и не разрешает автоматический повтор после final.

**AR-41, 2026-11-10 — текущая очередь:** [измеренная причина и отрицательное решение](rend2-relighting/contact-plane-diagnosis.md) завершены. Обе named-плоскости присутствуют; float-квантизация normal даёт residual `2.384185791015625e-7` WU против double-roundoff bound около `1.62445e-11` WU. Новый обоснованный oblique plane-witness contract отсутствует. **Следующий шаг — AR-36 по отрицательному receipt; AR-35 остаётся Blocked, integration unauthorized.** Done у AR-41 не принимает pilot и не разрешает numerical repair, новый scope или original-target fit.

1. **AR-34:** сверить существующие доказательства и выбрать один минимальный multi-fixture эксперимент. Не утверждать заново, что первый stock fit отсутствует. Зафиксировать criteria, assistance, исходную модель и причину именно этого эксперимента.
2. **AR-35:** завершить только недостающую проводку реального участка: фиксированные видимые fixtures → native coefficients → совместный fit → saved parameters → replay и пространственный/визуальный пакет. Если путь уже полностью реализован, проверить его и не переписывать.
3. **AR-36:** выполнить заранее ограниченный опыт и принять решение **Go / Targeted repair / Pivot model / Assisted route / Blocked / Stop**. Низкий MSE не заменяет визуальную/пространственную достаточность.

Исходный лимит короткой проверки: не более **двух новых ROI**, до **четырёх независимо мотивированных видимых светильников в ROI**, одна основная M-интеграция и не более одной дополнительной M-починки перед обязательным решением. Это граница доказательства жизнеспособности, не будущий лимит числа источников на карте и не GPU-бюджет. AR-34 должен конкретизировать численные evaluation/work limits до запуска. Расширение лимита — отдельная записанная управленческая задача с причиной, новым измеримым gate и новыми независимыми данными, а не автоматическое продолжение цикла.

Использовать существующую современную модель ASR20/24 как **явно условный диагностический baseline**. Полная реализация finite effective-radius point/spot, C field, MD3, сложных patches и whole-map exporter **не является условием начала** этой проверки. Успех простого point-прокси не закрывает будущие требования к современным формам/near-field; он лишь обосновывает дальнейшую инвестицию.

| Развилка | Обязательная управленческая задача | Результат решения |
|---|---|---|
| Достаточно ли текущих ASR24/25 доказательств; какой минимальный новый опыт нужен? | AR-34 | Замороженный viability protocol и ограниченный scope интеграции |
| Работает ли согласованная multi-light реконструкция; продолжать, чинить или менять модель? | AR-36 | Зафиксированный Go/Repair/Pivot/Assisted/Blocked/Stop, изменение дальнейших промптов при необходимости |
| Какие corpus blockers устранять первыми и какой ограниченный участок они разблокируют? | AR-37 | Приоритетная ветвь: captures/sections, catalogue limits, local mover safety; измеримый эффект по coverage |
| Нужна ли новая modern transport/appearance модель после короткой проверки? | AR-04 | Обоснованные final equations/domain, а не обязательная перепись всего evaluator заранее |
| Отдать свет A/B, солнцу/небу, C или D? | AR-18 | Проверенная capacity/attribution policy |
| Как честно сохранить модель и пригодность без исторической сертификации? | AR-27 | Совместимый data-only contract |
| Разрешены ли whole-map/corpus rollout и исключения? | AR-23, AR-26, AR-30, AR-31, AR-32, AR-33 | Отдельные решения о готовности/покрытии; нельзя автоматически масштабировать failed pilot |

### 3.3. Продуктовые рубежи

| Рубеж | Требуемый результат | Что нельзя выдавать за этот результат |
|---|---|---|
| **G0 — зафиксирован продукт** | Критерии эстетики, A/B/C/D, современная модель, appearance-domain, gauge, assistance и output semantics | «Решатель сошёлся» или выбор исторического профиля |
| **Gv — ранняя проверка жизнеспособности** | Замороженная multi-fixture stock-проверка и решение AR-36 на существующем рабочем пути | Переигрывание одного успешного chart, повторная реализация fit, бесконечное добавление инфраструктуры |
| **G1 — первый участок** | Настоящий стоковый интерьер с несколькими мотивированными источниками, ненулевым откликом, независимой проверкой и сохранёнными данными | Контрольная синтетическая сцена, легальные XYZ или zero-power baseline |
| **G2 — одна карта** | Согласованная вся карта: комнаты/переходы, shared sources, global/C, количественное покрытие, review и replay | Один удачный ROI или склейка независимо экспонированных комнат |
| **G3 — половина набора** | Не менее половины зафиксированного инвентаря реально принято по whole-map gates; при 57 картах — минимум 29 | Половина запущенных процессов или 29 карт с единственной успешной комнатой |
| **G4 — весь набор** | Каждый исходный BSP учтён; все заявленные complete-карты проходят финальные gates, C/D бюджеты и replay | «Все обработаны» при существенном неизвестном/оригинальном fallback |

Политика определяет численные пороги **до итогового теста**. Универсальный процент ошибки RGB не назначается в этом плане без эксперимента: старое точечное запекание может отличаться от убедительного современного area light. Но геометрическая допустимость, прозрачное покрытие, отсутствие выдуманной определённости и независимая проверка — обязательны.

A/B, C и D учитываются отдельно по фиксированным областям/площади, значимым помещениям и источникам. Сохранение исходного освещения в D — допустимый честный продуктовый fallback, но не новая реконструкция. Полностью отрендеренная/измеренная геометрия, обработанные карты, принятые карты и площадь принятого relighting — разные показатели. Существующие световые стили/модельные состояния не исчезают из знаменателя.

Актуальный основной путь после AR-41: **AR-34 → AR-41 → AR-36 (negative source-support receipt)**. AR-35 остаётся заблокирован до нового явного предметного разрешения; последующая цепочка выполняется только по решению AR-36: узкие доработки/политики → global/C/selection → whole-map orchestration → одна карта → representative bank → половина → конкретные оставшиеся причины отказа → финальный набор. AR-37 выбирает работу с измеренными corpus blockers; восстановление всего корпуса не задерживает опыт на уже доступных MP-картах. MD3 и локальное уточнение неизвестной геометрии обязательны там, где без них не проходит конкретная целевая область. Output contract можно готовить после короткой проверки; он не должен задерживать её диагностический saved-data/replay результат.

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

В редакции 2 с добавленным AR-41 **41 запись задач, из них 40 неархивных: 3 Research, 16 Decision/Experiment, 22 Implementation; 5 размера S и 36 размера M**. AR-16 имеет статус Superseded: отложенное первое принятие перенесено в раннюю проверку. Три целевых реализации corpus blockers остаются Deferred до выбора соответствующей ветви. Задачи M с повышенным риском — exact native footprint integration, model occlusion, local geometry refinement, C fitting и map reconciliation. При обнаружении необходимости нового большого алгоритма управленческая задача обязана разбить работу на новые S/M tickets **до** исполнения; увеличивать scope незаметно нельзя.

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
12. **Done у управленческой задачи не означает Go.** Выполненное отрицательное решение, Stop или Blocked не разблокирует длинную цепочку по факту наличия отчёта. Актуальный verdict должен явно называть разрешённый следующий scope/ремонт и ограничения; неразрешённые ветви остаются Deferred. Новый цикл ремонта получает собственный ограниченный gate, а не бесконечный повтор подбора на тех же final данных.

**С чего продолжить после AR-41:** AR-36 принимает bounded negative-preflight verdict по измеренному Blocked-SourceSupport receipt. AR-35 не получает разрешения на integration/repair от одного факта найденной причины. AR-01–AR-04 уточняют дальнейшую архитектуру и final model по необходимости; они не являются обязательным многозадачным прологом к новому stock-опыту. Не запускать весь список как независимые задачи: входные политики должны реально существовать, а продолжение — быть разрешено актуальным решением.

## 5. Очередь задач и зависимости

Все статусы здесь означают состояние **нового плана**, а не отрицание уже написанного ASR-кода. Если цель уже закрыта свежей работой, сначала проверить доказательства и зафиксировать переиспользование, а не переписать её заново.

| ID | Тип | Размер | Задача | Входные зависимости / условие | Статус |
|---|---|---|---|---|---|
| [AR-34](#ar-34) | Управленческая / экспериментальная | S | Rebase viability on ASR24/25 and freeze a minimal proof | —; расследование завершено, Outcome Blocked-SourceSupport | Done |
| [AR-41](#ar-41) | Управленческая / экспериментальная | S | Diagnose the frozen oblique named-mount refusal | AR-34; measured cause, missing justified oblique plane contract; negative receipt to AR-36 | Done — Outcome Blocked-SourceSupport |
| [AR-35](#ar-35) | Исполнительская | M | Complete only the missing multi-fixture pilot wiring | AR-41 receipt is negative; new explicit bounded authorization required | Blocked — AR-41 does not authorize integration, 2026-11-10 |
| [AR-36](#ar-36) | Управленческая / экспериментальная | M | Decide Go, Repair, Pivot or Stop on a real stock region | AR-41 measured negative receipt / unchanged viability-v1; no original-data fit exists | Pending — next action: negative preflight verdict |
| [AR-01](#ar-01) | Исследовательская | S | Audit the current relighting path and reconcile work in progress | AR-36; только разрешённая ветвь | Pending |
| [AR-02](#ar-02) | Управленческая / экспериментальная | M | Freeze aesthetic success criteria, A–D outcomes and coverage accounting | AR-01, AR-34 | Pending |
| [AR-03](#ar-03) | Исследовательская | M | Specify an honest appearance target and material-response model | AR-01 | Pending |
| [AR-04](#ar-04) | Управленческая / экспериментальная | M | Freeze the modern lighting and appearance policy | AR-02, AR-03, AR-36 | Pending |
| [AR-05](#ar-05) | Исполнительская | M | Implement finite-size modern point and spot evaluation | AR-04 | Pending |
| [AR-06](#ar-06) | Исполнительская | M | Complete area, clipped-sheet and tube evaluation parity | AR-04, AR-05 | Pending |
| [AR-07](#ar-07) | Исполнительская | M | Connect exact BSP luminous footprints to native evaluation | AR-04, AR-06 | Pending |
| [AR-08](#ar-08) | Исполнительская | M | Connect static model luminous regions without inventing occlusion | AR-07; условно до принятия карты с существенными MD3-светильниками | Pending |
| [AR-09](#ar-09) | Исполнительская | M | Implement fixture-bounded source poses and emitting-side placement | AR-05, AR-06, AR-07 | Pending |
| [AR-10](#ar-10) | Исполнительская | M | Resolve local geometry uncertainty where evidence permits | AR-09; по диагностированным блокерам | Pending |
| [AR-11](#ar-11) | Исполнительская | M | Commission source-to-receiver influence and observable sampling | AR-06, AR-07, AR-09, AR-36; только оставшиеся разрывы | Pending |
| [AR-12](#ar-12) | Управленческая / экспериментальная | M | Freeze an expanded stock pilot bank under the final modern policy | AR-02, AR-11, AR-36; расширенный pilot bank | Pending |
| [AR-13](#ar-13) | Исполнительская | M | Integrate appearance-guided RGB fitting for fixed fixtures | AR-04, AR-06, AR-11, AR-12 | Pending |
| [AR-14](#ar-14) | Исполнительская | M | Fit only justified local shape and pose parameters | AR-09, AR-13 | Pending |
| [AR-15](#ar-15) | Исполнительская | M | Produce offline visual evidence and spatial quality reports | AR-11, AR-13 | Pending |
| [AR-16](#ar-16) | Управленческая / экспериментальная | M | Accept or reject the first stock region | —; архив отложенного gate, заменён ранним AR-36 | Superseded |
| [AR-17](#ar-17) | Исследовательская | M | Design scene-wide sunlight and nonlocal illumination handling | AR-36; раньше — только по диагностическому решению | Pending |
| [AR-18](#ar-18) | Управленческая / экспериментальная | M | Choose global and background model capacity | AR-17, AR-02 | Pending |
| [AR-19](#ar-19) | Исполнительская | M | Integrate modern sun and sky hypotheses | AR-18 | Pending |
| [AR-20](#ar-20) | Исполнительская | M | Represent supported nonlocal illumination as category C | AR-18 | Pending |
| [AR-21](#ar-21) | Исполнительская | M | Unify evidence-aware sparse source selection | AR-13, AR-14, AR-18 | Pending |
| [AR-22](#ar-22) | Исполнительская | M | Add only motivated category B sources in an iterative loop | AR-19, AR-20, AR-21 | Pending |
| [AR-23](#ar-23) | Управленческая / экспериментальная | M | Pass a modern aesthetic adversarial validation bank | AR-19, AR-20, AR-21, AR-22 | Pending |
| [AR-24](#ar-24) | Исполнительская | M | Plan complete map work from original geometry | AR-36, AR-23, AR-37 | Pending |
| [AR-25](#ar-25) | Исполнительская | M | Reconcile shared sources and lighting across map regions | AR-24, AR-21, AR-22 | Pending |
| [AR-26](#ar-26) | Управленческая / экспериментальная | M | Accept the first complete stock map | AR-08 при необходимости, AR-10 при необходимости, AR-24, AR-25, AR-27, AR-28 | Pending |
| [AR-27](#ar-27) | Управленческая / экспериментальная | S | Freeze a faithful data-only output contract | AR-02, AR-04, AR-36 | Pending |
| [AR-28](#ar-28) | Исполнительская | M | Export and replay validated aesthetic reconstruction data | AR-27, AR-13, AR-20 | Pending |
| [AR-29](#ar-29) | Исполнительская | M | Make corpus processing resumable and dependency-safe | AR-24, AR-25, AR-28, AR-37 | Pending |
| [AR-30](#ar-30) | Управленческая / экспериментальная | M | Validate transfer to a representative stock-map bank | AR-26, AR-29 | Pending |
| [AR-31](#ar-31) | Управленческая / экспериментальная | M | Reach the half-corpus acceptance milestone | AR-30, AR-37; нужные repairs по выбору | Pending |
| [AR-32](#ar-32) | Управленческая / экспериментальная | M | Resolve the remaining corpus gaps without an endless research loop | AR-31 | Pending |
| [AR-33](#ar-33) | Управленческая / экспериментальная | M | Accept the complete original-stock relighting dataset | AR-32 и все добавленные обязательные ремонты | Pending |
| [AR-37](#ar-37) | Управленческая / экспериментальная | S | Choose a measured corpus-blocker branch | AR-34; параллельно, не блокирует доступный pilot | Pending |
| [AR-38](#ar-38) | Исполнительская | M | Recover original captures and resolve input section refusals | AR-37; только если выбрана эта ветвь | Deferred |
| [AR-39](#ar-39) | Исполнительская | M | Complete bounded catalogue processing on affected maps | AR-37; только если выбрана эта ветвь | Deferred |
| [AR-40](#ar-40) | Исполнительская | M | Replace the whole-map dynamic veto with scoped evidence | AR-37; только если выбрана эта ветвь | Deferred |

Номера AR-24/AR-25 обозначают задачи этого плана, а ASR24/ASR25 — уже выполненные исследования: это разные пространства имён. Архивный AR-16 не выполнять. Ветви AR-27/28 (контракт/экспорт) можно вести после раннего решения параллельно развитию global/C. AR-08 и AR-10 не обязаны задерживать простой пилот, но их отсутствие учитывается в покрытии. AR-23 проверяет именно современный aesthetic pipeline; ASR23 остаётся неизменённым историческим свидетельством. AR-32 — явная точка изменения направления для последних сложных карт, а не разрешение бесконечно расширять scope.

## 6. Самостоятельные промпты для задач

Каждый блок ниже можно передать отдельному исполнителю без этого чата и без других промптов. План и названные существующие файлы доступны из репозитория. Тип и размер продублированы внутри блока.

<a id="ar-34"></a>

### AR-34 — Rebase viability on ASR24/25 and freeze a minimal proof

Тип: **Управленческая / экспериментальная**. Размер: **S**.

```text
Task ID: AR-34
Task type: Decision / Experiment
Estimated size: S
Title: Rebase viability on ASR24/25 and freeze a minimal proof

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and the actual revision/remote before editing. Preserve unrelated, concurrent and staged changes, including ASR25. Verify and reuse newer completed work.

Product objective:
Deliver data-only Aesthetic Relighting of original stock maps: visually motivated modern sources attached to actual lamps/screens/BSP/model emission, plus justified nonlocal illumination. A means a visibly supported fixture, B an independently motivated probable unseen source, C supported illumination without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. Historical type, XYZ, power and exactLight certification are not aesthetic eligibility requirements.

Mandatory boundaries:
Preserve historical solver behavior and existing canonical/storage meanings. Do not modify BSP/lightmaps, author unmotivated lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty and unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Record modern-model/transfer/gauge assumptions, bounded approximations and assistance. Never promote a conditional lamp or processed map to historical truth or complete reconstruction.

Objective:
Recognize the existing conditional stock successes and choose the smallest independent multi-fixture experiment that can justify or reject further investment in the current aesthetic reconstruction approach.

Required inputs and inspection:
Read docs/rend2-stock-aesthetic-asr24.md, docs/rend2-stock-discovery-asr25.md, their public summary/checks/manifest files, tools/rend2/stock_aesthetic/, tools/rend2/stock_audit/, and accessible private geometry/response packets. Inspect actual committed/staged code; ASR25 may already have been committed since this prompt was written.
Use the current versions of named artifacts. A missing required decision/input is a precise blocker, not permission to invent it; complete useful independent inspection. This prompt requires no previous chat or another task prompt.

High-level action plan:
1. Verify the existing evidence and report its actual scope: mp/duel10 and mp/duel7 each have one accepted conditional mounted-point configuration. Do not claim there is no stock success or count the preserved duel10 result twice.
2. Assess spatial/visual sufficiency separately from numeric acceptance: one chart and one held-out block in duel10; a large residual and minimally sufficient grid corroboration in duel7; neither result is a complete room.
3. Select at most two new original-stock ROI scopes with up to four independently inventoried visible fixtures each. At least one scope must test two or more distinct motivated fixtures and surrounding receiver surfaces beyond a repeat of one fitted chart; preflight their observable influence. Use geometry and visible evidence before opening new fit/final values. Keep the published examples as development/regression baselines, not fresh blind tests.
4. Freeze an existing-model baseline: modern DiffuseIntegral plus explicitly conditional appearance transfer, fixed gauge and bounded nuisance. Finite-radius implementation, full fields, MD3 and whole-map export are not prerequisites for this diagnostic proof.
5. Choose geometric coverage, shadow/dark controls, color/brightness motif, multi-source interaction, source-removal and independent visual/spatial acceptance measures before fitting. Specify assistance, new fold/scene independence, work limits and failed-case accounting.
6. Write docs/rend2-relighting/viability-decision.md and a versioned viability protocol under tools/rend2/aesthetic_relighting/policies/. Bound the next coding effort to one M integration plus at most one M repair before a mandatory decision.

Definition of Done:
- The decision credits existing stock results, preserves their hashes/limitations, and identifies a specific missing multi-fixture demonstration rather than restarting first-source discovery.
- The frozen experiment has concrete original inputs, visible-fixture inventory, model assumptions, independent evaluation, artifact requirements and an exit budget.
- A viable already implemented path is reused; if inputs or required semantics are missing, the result is an explicit Blocked or targeted decision, not speculative infrastructure work.
- Deliver the complete scoped artifact, verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not rerun the entire 57-map audit, require a complete new modern evaluator before testing, silently retune the ASR24/25 thresholds, claim a remaster-quality room from one fitted chart, or invent a probability of whole-corpus completion.
Deliver a concrete evidence-backed decision or the authorized bounded experiment plus its decision. You may rewrite the roadmap, dependencies and future prompts or add tasks within the data-only aesthetic objective. Log every change, preserve prior frozen failures, and give each new task a standalone English prompt, S/M size and a measurable exit condition.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md and append a high-level result under "AR-34" in Results Log, including date/revision, actual scope and assistance, result, measured evidence and artifact paths/hashes, A/B/C/D and coverage implications, limitations, decision and concrete next action. Update the task's status honestly even when the outcome is Partial, Failed or Blocked. A completed investigation can make a valid negative decision; it cannot turn a failed product gate into Passed. Do not claim measurements that were not performed. Record approved scope/policy/prompt changes in Plan Change Log, with fresh independent final evidence when old final results informed the change.
```

<a id="ar-41"></a>

### AR-41 — Diagnose the frozen oblique named-mount refusal

Тип: **Управленческая / экспериментальная**. Размер: **S**.

```text
Task ID: AR-41
Task type: Decision / Experiment
Estimated size: S
Title: Diagnose the frozen oblique named-mount refusal

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 branch. Inspect actual HEAD, remote, git status and applicable AGENTS.md before work. Preserve unrelated, concurrent and staged changes. AR-34 inspected dff8ea77acc1fc578239af69b17253e3a12b7c9f, rend2-unified-wip, origin keyten/BetterJK; verify newer work instead of assuming this is still HEAD.

Product objective and boundaries:
Deliver data-only aesthetic relighting motivated by original visible fixtures and justified nonlocal illumination. A is visibly supported, B independently motivated unseen, C supported illumination without an identified lamp, D unresolved with original-data fallback. Historical type, XYZ, power and exactLight certification do not determine aesthetic eligibility. Preserve historical solver behavior and canonical/storage meanings. Do not change BSP/lightmaps, author unmotivated lamps, implement GPU/runtime/world shading or solve double lighting. Unknown geometry is not empty; unavailable predictions are not black; encoded RGB8 is not calibrated linear irradiance. Keep licensed geometry/XYZ/images private under ignored build/. Record all assumptions and assistance; conditional sources never become historical truth.

Required inputs:
Read docs/rend2-relighting/viability-decision.md, tools/rend2/aesthetic_relighting/policies/viability-v1.json and tools/rend2/aesthetic_relighting/results/ar34-v1/evidence.json, plus the public ASR24/25 summaries/checks/manifests. Inspect build/ar34-v1/pilot-scope.json, pilot-geometry.json, frozen-folds-geometry.json, joint-coverage.json, dummy-admission-v2.txt, dummy-held-v2.txt and dummy-admission-v2.json. Rehash the original duel10 BSP/capture/catalogue/rows and frozen executables. Read shared/rd-rend2/lightrecon/emitter_support.cpp (FromSurfaces/Check), source orientation/visibility extraction, tools/lightrecon_test/aesthetic_fit.cpp and lightrecon_aesthetic_tool.cpp. Missing required packets are a precise Blocked input, not permission to invent geometry or a replacement experiment. No previous chat or task prompt is required.

Objective:
Explain why the two original oblique housings on mp/duel10 surfaces 987/988 have available unit responses yet native SourceSupport::Check refuses every local configuration with 'Named mounting surface absent at declared contact plane'. Decide whether one bounded modern-path integration can safely unblock the frozen experiment, or send a precise negative receipt to AR-36.

High-level action plan:
1. Reproduce the decisive existing dummy-zero admission probe without opening, fitting or scoring original receiver RGB. Distinguish unit-response preparation from actual source admission; retain prior scratch failures and metadata corrections.
2. Bound inspection to these two named surfaces and their immutable original nearby geometry. Measure original triangle/normal/anchor identities, extraction completeness and contact-plane residuals using existing providers or a private read-only diagnostic. Distinguish absent geometry, genuine unsupported/contact geometry and numerical representation error. Do not present float-normal/double-roundoff mismatch as proved until measured.
3. Test the causal explanation with exact input identities and appropriate positive/negative oblique and axial controls. No guard removal, guessed epsilon, source relocation, free-point substitution, unknown-space clearing or model-capacity increase is authorized.
4. Write docs/rend2-relighting/contact-plane-diagnosis.md plus a versioned hash receipt. Choose Authorize-Bounded-AR35 only if the diagnosis specifies a safe existing-provider/modern-adapter proof or repair that preserves historical behavior and storage semantics within one M integration. State exact permitted files/behavior and acceptance controls. If a new numerical contract is required, make that explicit; absence of a justified contract is Blocked, not authorization to invent one.
5. Otherwise issue Blocked-SourceSupport or a supported Stop/Pivot decision for this frozen scope and route its receipt to AR-36 without demanding a nonexistent original-data fit. Do not search another scope or launch the large roadmap.
6. Update the plan queue, Results Log and Plan Change Log. Any material change to poses, geometry/response support or acceptance requires a new explicit freeze before original-target exposure; old failures remain immutable. Once final evidence informs a change, require fresh adequate independent final evidence.

Work limits and measurable exit:
At most two hours of investigation, two original housings and one causal diagnosis receipt. No production implementation is part of AR-41 and it adds no coding allowance. The total following budget remains one M integration (eight engineering hours maximum) and at most one M repair (four hours maximum) before the single original-target fit/repeat and mandatory AR-36 decision. V1's reserve lacks housing 988 control pairs and cannot authorize post-final repair fitting. Each diagnostic subprocess has a 300-second cap. At the limit, publish measured findings and a precise Blocked/Stop outcome; do not extend automatically.

Definition of Done:
- The exact refusal has a measured causal explanation or a precise retained unresolved-input/contract blocker.
- The receipt explicitly authorizes a bounded safe AR-35 scope or sends a negative decision to AR-36, with no hypothetical success or original-target score.
- Original fixture motivation, unknown geometry, historical/canonical behavior, old stock successes and all failures remain intact.
- Append an AR-41 Results Log entry with date/revision, actual scope/assistance, measurements, artifact hashes, A/B/C/D and coverage implications, limitations, decision and next action; update status honestly and log approved changes. Done investigation does not make an unknown product gate Passed.
```

<a id="ar-35"></a>

### AR-35 — Complete only the missing multi-fixture pilot wiring

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-35
Task type: Implementation
Estimated size: M
Title: Complete only the missing multi-fixture pilot wiring

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and the actual revision/remote before editing. Preserve unrelated, concurrent and staged changes, including ASR25. Verify and reuse newer completed work.

Product objective:
Deliver data-only Aesthetic Relighting of original stock maps: visually motivated modern sources attached to actual lamps/screens/BSP/model emission, plus justified nonlocal illumination. A means a visibly supported fixture, B an independently motivated probable unseen source, C supported illumination without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. Historical type, XYZ, power and exactLight certification are not aesthetic eligibility requirements.

Mandatory boundaries:
Preserve historical solver behavior and existing canonical/storage meanings. Do not modify BSP/lightmaps, author unmotivated lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty and unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Record modern-model/transfer/gauge assumptions, bounded approximations and assistance. Never promote a conditional lamp or processed map to historical truth or complete reconstruction.

Objective:
Deliver the narrow end-to-end stock-region path required by the frozen viability protocol: visible fixed fixtures, native response, joint RGB fit, saved parameters, replay and a reviewable spatial/visual result.

Required inputs and inspection:
Inspect docs/rend2-relighting/viability-decision.md, the frozen viability policy under tools/rend2/aesthetic_relighting/policies/, tools/rend2/stock_aesthetic/run.py and report.py, tools/rend2/stock_audit/packet.py, Aesthetic::FitAndValidate/SelectSparse, native coverage/source support, and the ASR24/25 accepted and failed witnesses.
Execution gate added by AR-34: protocol v1 is FrozenBlocked. Read docs/rend2-relighting/contact-plane-diagnosis.md and its AR-41 receipt. Proceed only if it explicitly authorizes the exact bounded modern-path integration while preserving historical behavior/storage meanings. Missing or negative authorization keeps AR-35 Blocked. Unit-response availability does not grant named-mount admission. Do not create a guessed contact tolerance or treat the supplied fixtures as free points.
Current AR-41 result at 89a40b0fc: Blocked-SourceSupport, integration_authorized false. Its receipt is tools/rend2/aesthetic_relighting/results/ar41-v1/evidence.json. A measured representation defect is not permission to repair it; the justified oblique plane-witness contract is still absent. The next task is AR-36's negative preflight decision, not AR-35 implementation.
Use the current versions of named artifacts. A missing required decision/input is a precise blocker, not permission to invent it; complete useful independent inspection. This prompt requires no previous chat or another task prompt.

High-level action plan:
1. Map each required operation to existing code and reuse it. If the path is already complete, verify it and record no implementation needed rather than creating another fitter or protocol.
2. Add only the missing cohort/adapter wiring to evaluate up to four independently supplied visible fixtures together on each selected original ROI, preserving source families, individual masks/approximations and fixed fixture-local poses.
3. Use the same native modern evaluator, occupancy assumptions and appearance/gauge in unit-response preflight, fitting and frozen replay. Distinguish blocked, unavailable, zero and positive response; keep dark/shadow controls.
4. Freeze original multi-surface receiver support and disjoint folds before target-dependent fitting. Jointly fit allowed RGB and the same bounded nuisance baseline; do not add free positions or backgrounds to force success.
5. Save actual source parameters, geometry/support references, policy/input identities, conditional limitations and per-receiver predictions. Reload these data without the optimizer to reproduce the score and source-removal response.
6. Reuse existing report/OBJ/plate generation and add only the contextual views needed to assess combined light motifs, receiver normals, shadows and unexplained regions at fixed presentation settings.
7. Run meaningful native/stock-protocol regressions and the declared replay checks. Enforce the integration budget; one additional repair requires a written causal decision.
AR-34 v1 limit: one M integration (eight engineering hours), at most one M repair (four hours) before original-target fitting, then one fit and identical repeat followed by AR-36. Use the exact frozen original receiver IDs; primary final spans three charts/eight blocks. The diagnostic reserve lacks one fixture's shadow-control pairs, so no post-final repair fit is authorized. If admission, controls or review are unavailable, preserve the blocker and send the receipt to AR-36 without inventing a score.

Definition of Done:
- The selected real-stock ROI can execute the complete existing-model path and produce replayable data plus the frozen review packet, including honest failed/unavailable configurations.
- All independently supplied fixtures and original receiver scopes remain auditable; known blockers are retained.
- No disconnected helper, hand-written fitted power or synthetic scene is substituted for the substantive stock experiment.
- The implementation is complete within its narrow scope; aesthetic acceptance is left to the separate frozen evaluation.
- Deliver the complete scoped artifact, verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not implement finite near-field physics, a new renderer, a full-map scheduler, a new storage schema or a new optimizer merely to run this proof. Do not silently convert the exploratory point/stand-off approximation into final production physics.
Implement the specified mechanism completely through the existing real path with meaningful verification. Do not substitute a loose sketch, disconnected helper, stub or report for the required integration. Reuse already complete operations. If the scoped work exceeds M or its required decision is absent, document the concrete split/blocker; do not silently expand the work or decide a new product policy.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md and append a high-level result under "AR-35" in Results Log, including date/revision, actual scope and assistance, result, measured evidence and artifact paths/hashes, A/B/C/D and coverage implications, limitations, decision and concrete next action. Update the task's status honestly even when the outcome is Partial, Failed or Blocked. A completed investigation can make a valid negative decision; it cannot turn a failed product gate into Passed. Do not claim measurements that were not performed. Do not silently change acceptance criteria or the plan direction; propose a specific decision task if a branch is needed.
```

<a id="ar-36"></a>

### AR-36 — Decide Go, Repair, Pivot or Stop on a real stock region

Тип: **Управленческая / экспериментальная**. Размер: **M**.

```text
Task ID: AR-36
Task type: Decision / Experiment
Estimated size: M
Title: Decide Go, Repair, Pivot or Stop on a real stock region

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and the actual revision/remote before editing. Preserve unrelated, concurrent and staged changes, including ASR25. Verify and reuse newer completed work.

Product objective:
Deliver data-only Aesthetic Relighting of original stock maps: visually motivated modern sources attached to actual lamps/screens/BSP/model emission, plus justified nonlocal illumination. A means a visibly supported fixture, B an independently motivated probable unseen source, C supported illumination without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. Historical type, XYZ, power and exactLight certification are not aesthetic eligibility requirements.

Mandatory boundaries:
Preserve historical solver behavior and existing canonical/storage meanings. Do not modify BSP/lightmaps, author unmotivated lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty and unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Record modern-model/transfer/gauge assumptions, bounded approximations and assistance. Never promote a conditional lamp or processed map to historical truth or complete reconstruction.

Objective:
Determine whether the existing aesthetic reconstruction path produces a coherent multi-fixture original-stock result, and explicitly authorize, redirect or stop the next work instead of following the large roadmap automatically.

Required inputs and inspection:
Read the frozen viability protocol/decision, complete stock-region fitted data and replay/review packets, original source/receiver evidence, and ASR24/25 reference results. Required policy and saved-data inputs must exist; do not infer them from this prompt or from another chat.
AR-34/41 may terminate at source admission before any original-target fit. In that branch, the frozen policy, native refusal and causal diagnosis are the substantive decision inputs: publish a precise Blocked/Stop/Pivot verdict and keep numerical/visual product gates Unknown. Do not demand or fabricate nonexistent fitted data. An unavailable spare control set cannot waive fresh final evidence after a policy change.
Current routed inputs: docs/rend2-relighting/contact-plane-diagnosis.md and tools/rend2/aesthetic_relighting/results/ar41-v1/evidence.json / manifest.json. AR-41 is Done with measured normal-quantization/plane-bound incompatibility and a negative Blocked-SourceSupport decision; no justified replacement oblique plane-witness contract or production repair is authorized. Keep AR-35 blocked unless a new explicit decision establishes a safe bounded modern-only contract and exact files/controls within the unchanged allowance. Numerical and visual pilot gates remain Unknown. Apply steps 2–4 below only if the corresponding authorized fitted data actually exist; otherwise record them unavailable and decide from the retained negative preflight. Do not run another scope or infer appearance-model failure from a pre-fit geometric refusal.
Use the current versions of named artifacts. A missing required decision/input is a precise blocker, not permission to invent it; complete useful independent inspection. This prompt requires no previous chat or another task prompt.

High-level action plan:
1. Verify that all hypotheses, geometry/assistance, nuisance capacity, validation selection and evaluation limits froze before opening the new final data. Already exposed ASR24/25 rows remain development evidence. A one-source/single-chart repeat cannot satisfy the new multi-fixture gate; record it only as regression or partial evidence.
2. Evaluate every declared ROI and its dark/shadow controls, source attachments, color motifs, relative brightness, multi-source interactions, nuisance/parameter bounds and unsupported area.
3. Review spatial/contextual visual output at a fixed declared presentation; a geometric overlay or a successful interval-MSE percentage alone cannot establish convincing room lighting.
4. Check the loaded saved data and frozen-background source-removal behavior. Do not refit after final scoring or claim unmeasured regions as reconstructed.
5. Issue one explicit outcome: Go-Narrow for justified expansion to independent regions; Repair-Wiring for a specific bounded defect; Pivot-Appearance/Background for model mismatch; Assisted-Geometry for a disclosed evidence/annotation need; Blocked-Inputs; or Stop-Current-Approach.
6. For every non-Go branch, name the causal evidence, the exact bounded research/decision/implementation deliverable, its size and a fresh exit test. Add standalone tasks/prompts when needed; do not assign a generic 'improve the solver' task.
7. Write docs/rend2-relighting/viability-verdict.md and pilot-acceptance.md with numeric/spatial/visual results, all failures, uncertainty, approved next scope and a plan revision. No automatic corpus rollout follows from this verdict.

Definition of Done:
- A substantive stock result is accepted only if it meets the previously frozen multi-fixture, geometry, appearance and review gates; otherwise the failure is retained with a useful causal decision.
- The verdict distinguishes existing one-source feasibility, new coherent-region viability, full modern-model adequacy and whole-map readiness.
- Repairs stay within the fixed short budget; exceeding it requires a new explicit decision before more infrastructure work.
- The roadmap and future prompts reflect the chosen branch, including a stop or scope reduction when evidence does not support continuation.
- Deliver the complete scoped artifact, verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not treat this decision as a promised success, rewrite final criteria after seeing results, iterate on the same final patch indefinitely, classify every failure as missing engineering, or report conditional point-proxy fit as a complete relit map.
Deliver a concrete evidence-backed decision or the authorized bounded experiment plus its decision. You may rewrite the roadmap, dependencies and future prompts or add tasks within the data-only aesthetic objective. Log every change, preserve prior frozen failures, and give each new task a standalone English prompt, S/M size and a measurable exit condition.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md and append a high-level result under "AR-36" in Results Log, including date/revision, actual scope and assistance, result, measured evidence and artifact paths/hashes, A/B/C/D and coverage implications, limitations, decision and concrete next action. Update the task's status honestly even when the outcome is Partial, Failed or Blocked. A completed investigation can make a valid negative decision; it cannot turn a failed product gate into Passed. Do not claim measurements that were not performed. Record approved scope/policy/prompt changes in Plan Change Log, with fresh independent final evidence when old final results informed the change.
```

<a id="ar-37"></a>

### AR-37 — Choose a measured corpus-blocker branch

Тип: **Управленческая / экспериментальная**. Размер: **S**.

```text
Task ID: AR-37
Task type: Decision / Experiment
Estimated size: S
Title: Choose a measured corpus-blocker branch

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and the actual revision/remote before editing. Preserve unrelated, concurrent and staged changes, including ASR25. Verify and reuse newer completed work.

Product objective:
Deliver data-only Aesthetic Relighting of original stock maps: visually motivated modern sources attached to actual lamps/screens/BSP/model emission, plus justified nonlocal illumination. A means a visibly supported fixture, B an independently motivated probable unseen source, C supported illumination without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. Historical type, XYZ, power and exactLight certification are not aesthetic eligibility requirements.

Mandatory boundaries:
Preserve historical solver behavior and existing canonical/storage meanings. Do not modify BSP/lightmaps, author unmotivated lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty and unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Record modern-model/transfer/gauge assumptions, bounded approximations and assistance. Never promote a conditional lamp or processed map to historical truth or complete reconstruction.

Objective:
Prioritize the concrete input/tool limitations measured by the original 57-map audit and authorize only repairs that unlock a specified reconstruction scope.

Required inputs and inspection:
Inspect docs/rend2-stock-discovery-asr25.md, tools/rend2/stock_audit/results/v1/{maps.json,summary.json,checks.json,protocol.json}, current actual files/native errors, the viability decision and the selected next map bank.
Use the current versions of named artifacts. A missing required decision/input is a precise blocker, not permission to invent it; complete useful independent inspection. This prompt requires no previous chat or another task prompt.

High-level action plan:
1. Recheck current file availability and measured causes: 20 missing captures, 23 whole-map dynamic vetoes, 11 catalogue work-budget maps, one polygon-budget map and two native capture-section refusals. Counts overlap; do not sum them as independent failed maps.
2. Separate missing inputs, controller restrictions and bounded work limits from failed lighting-model evidence. Preserve per-map nulls and rejected fits.
3. Choose the smallest repair branch needed by the approved next region/map bank: restore original evidence, complete bounded catalogue processing, or support locally justified dynamic-map scopes.
4. Specify target maps, expected unlocked stage/coverage, required local assumptions, regression witnesses and implementation-size limits. Defer corpus-wide restoration that does not help the immediate pilot.
5. Write docs/rend2-relighting/corpus-blocker-decision.md with the selected implementation scope and explicit activation/defer/stop decisions.
6. Revise the task queue and add smaller standalone tasks if capture-section handling, geometry-state reasoning or catalogue representation exceeds one M deliverable.

Definition of Done:
- Each selected repair has a specific original input/error, an expected measurable stage improvement and a verification/exit condition.
- Operational blockers are not misreported as evidence that aesthetic relighting is impossible.
- Available static MP proof work is not delayed by restoration of the entire corpus, and no deferred branch executes without a decision.
- Deliver the complete scoped artifact, verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not repeat the all-map audit just to rediscover known counts, treat a capture manifest entry as a present file, remove a geometry guard without equivalent scope checks, or raise all budgets blindly.
Deliver a concrete evidence-backed decision or the authorized bounded experiment plus its decision. You may rewrite the roadmap, dependencies and future prompts or add tasks within the data-only aesthetic objective. Log every change, preserve prior frozen failures, and give each new task a standalone English prompt, S/M size and a measurable exit condition.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md and append a high-level result under "AR-37" in Results Log, including date/revision, actual scope and assistance, result, measured evidence and artifact paths/hashes, A/B/C/D and coverage implications, limitations, decision and concrete next action. Update the task's status honestly even when the outcome is Partial, Failed or Blocked. A completed investigation can make a valid negative decision; it cannot turn a failed product gate into Passed. Do not claim measurements that were not performed. Record approved scope/policy/prompt changes in Plan Change Log, with fresh independent final evidence when old final results informed the change.
```

<a id="ar-38"></a>

### AR-38 — Recover original captures and resolve input section refusals

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-38
Task type: Implementation
Estimated size: M
Title: Recover original captures and resolve input section refusals

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and the actual revision/remote before editing. Preserve unrelated, concurrent and staged changes, including ASR25. Verify and reuse newer completed work.

Product objective:
Deliver data-only Aesthetic Relighting of original stock maps: visually motivated modern sources attached to actual lamps/screens/BSP/model emission, plus justified nonlocal illumination. A means a visibly supported fixture, B an independently motivated probable unseen source, C supported illumination without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. Historical type, XYZ, power and exactLight certification are not aesthetic eligibility requirements.

Mandatory boundaries:
Preserve historical solver behavior and existing canonical/storage meanings. Do not modify BSP/lightmaps, author unmotivated lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty and unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Record modern-model/transfer/gauge assumptions, bounded approximations and assistance. Never promote a conditional lamp or processed map to historical truth or complete reconstruction.

Objective:
Restore the selected missing original observation inputs and diagnose the selected native section refusals through the existing capture/export/reader workflow, preserving source identity and format semantics.

Required inputs and inspection:
Inspect docs/rend2-relighting/corpus-blocker-decision.md, ASR25 maps/checks, original assets0–3 BSP hashes, current capture manifests, tools/rend2/datasets/, native dataset export/quality tools and relevant Offline read limits/sections. Only the explicitly selected maps are in scope.
Activation: Do not start implementation unless docs/rend2-relighting/corpus-blocker-decision.md explicitly selects this branch and its target maps. An inventory blocker count or a completed decision file alone is not authorization for all repair branches. Otherwise retain Deferred and record the missing choice.
Use the current versions of named artifacts. A missing required decision/input is a precise blocker, not permission to invent it; complete useful independent inspection. This prompt requires no previous chat or another task prompt.

High-level action plan:
1. Verify original BSP/assets and the actual missing or refused capture paths. Preserve old manifests as historical receipts; do not overwrite a valid input merely because its path changed.
2. Use the existing capture-only/export workflow to restore or regenerate evidence with original map, shader/material, image and derived-section identities.
3. For t1_rail/vjun3 or other selected section refusals, diagnose actual header/layout/limit/version behavior and apply a compatible bounded reader/tool fix or regenerate with the supported exporter.
4. Do not invent observations or treat a refused section as corruption without evidence. Preserve unsupported sections and precise external blockers when recovery is unavailable.
5. Verify raw RGB bytes, geometry/receiver mapping, section completeness, hashes and native opening against original BSP data; update a current availability manifest distinct from archived receipts.
6. Demonstrate that recovered inputs reach the approved diagnostic/reconstruction stage and leave numerical acceptance unchanged for previously valid inputs.

Definition of Done:
- Selected recoverable captures exist and pass native identity/completeness/mapping checks; any irrecoverable input has a specific documented blocker.
- Section compatibility is fixed or causally resolved without changing canonical storage semantics or silently omitting data.
- Before/after corpus availability and unlocked stages are reported separately from reconstructed-lighting success.
- Deliver the complete scoped artifact, verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not download substitute assets, replace original observations with synthetic lighting, edit BSP/lightmaps, reinterpret unsupported bytes, add a second capture format, or claim a restored file is a relit map.
Implement the specified mechanism completely through the existing real path with meaningful verification. Do not substitute a loose sketch, disconnected helper, stub or report for the required integration. Reuse already complete operations. If the scoped work exceeds M or its required decision is absent, document the concrete split/blocker; do not silently expand the work or decide a new product policy.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md and append a high-level result under "AR-38" in Results Log, including date/revision, actual scope and assistance, result, measured evidence and artifact paths/hashes, A/B/C/D and coverage implications, limitations, decision and concrete next action. Update the task's status honestly even when the outcome is Partial, Failed or Blocked. A completed investigation can make a valid negative decision; it cannot turn a failed product gate into Passed. Do not claim measurements that were not performed. Do not silently change acceptance criteria or the plan direction; propose a specific decision task if a branch is needed.
```

<a id="ar-39"></a>

### AR-39 — Complete bounded catalogue processing on affected maps

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-39
Task type: Implementation
Estimated size: M
Title: Complete bounded catalogue processing on affected maps

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and the actual revision/remote before editing. Preserve unrelated, concurrent and staged changes, including ASR25. Verify and reuse newer completed work.

Product objective:
Deliver data-only Aesthetic Relighting of original stock maps: visually motivated modern sources attached to actual lamps/screens/BSP/model emission, plus justified nonlocal illumination. A means a visibly supported fixture, B an independently motivated probable unseen source, C supported illumination without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. Historical type, XYZ, power and exactLight certification are not aesthetic eligibility requirements.

Mandatory boundaries:
Preserve historical solver behavior and existing canonical/storage meanings. Do not modify BSP/lightmaps, author unmotivated lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty and unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Record modern-model/transfer/gauge assumptions, bounded approximations and assistance. Never promote a conditional lamp or processed map to historical truth or complete reconstruction.

Objective:
Make the selected original luminous catalogues complete or precisely bounded under a declared deterministic processing strategy, addressing the measured work/polygon limits without changing their geometric meaning.

Required inputs and inspection:
Inspect the selected scope in docs/rend2-relighting/corpus-blocker-decision.md, ASR25 catalogue failures, tools/rend2/bsp_fixtures/ mask clipping/connectivity/union code, original texture repetition and topology, and existing catalogue identity tests.
Activation: Do not start implementation unless docs/rend2-relighting/corpus-blocker-decision.md explicitly selects this branch and its target maps. An inventory blocker count or a completed decision file alone is not authorization for all repair branches. Otherwise retain Deferred and record the missing choice.
Use the current versions of named artifacts. A missing required decision/input is a precise blocker, not permission to invent it; complete useful independent inspection. This prompt requires no previous chat or another task prompt.

High-level action plan:
1. Profile the actual work and polygon expansion of the selected failed maps before choosing a repair; distinguish repeated UV expansion, connectivity comparisons, union topology and true output size.
2. Implement the selected bounded spatial indexing, deduplication, deterministic sharding or representation improvement, reusing the existing source geometry/mask semantics.
3. Preserve cross-shard component connectivity, dark gaps, exact source aliases, area/centroid and global fixture identities. Record every shard, work count and completeness condition.
4. Retain genuine unsupported optical/topological cases as unknown; no mask-axis reduction, dropped pieces or early output truncation may silently count as complete.
5. Compare already supported catalogues semantically/bytewise where appropriate and run adversarial repeated/clamped UV, thin-strip, disconnected and boundary-component cases.
6. Execute the approved difficult-map set and report completed catalogue coverage, remaining blockers, resource measurements and the stages actually unlocked.

Definition of Done:
- The selected processing mechanism is implemented and integrated, with independently checked area/component semantics and bounded repeatable work.
- At least the targeted causal failure class has measured before/after evidence; any remaining budget/representation refusal stays explicit and cannot publish a complete-catalogue claim.
- Catalogue completion is reported as input preparation, not accepted source or map reconstruction.
- Deliver the complete scoped artifact, verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not solve the audit by globally increasing limits without profiling, silently simplifying luminous geometry, changing historical extraction results, merging unrelated lamps, or publishing partial shards as a finished catalogue.
Implement the specified mechanism completely through the existing real path with meaningful verification. Do not substitute a loose sketch, disconnected helper, stub or report for the required integration. Reuse already complete operations. If the scoped work exceeds M or its required decision is absent, document the concrete split/blocker; do not silently expand the work or decide a new product policy.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md and append a high-level result under "AR-39" in Results Log, including date/revision, actual scope and assistance, result, measured evidence and artifact paths/hashes, A/B/C/D and coverage implications, limitations, decision and concrete next action. Update the task's status honestly even when the outcome is Partial, Failed or Blocked. A completed investigation can make a valid negative decision; it cannot turn a failed product gate into Passed. Do not claim measurements that were not performed. Do not silently change acceptance criteria or the plan direction; propose a specific decision task if a branch is needed.
```

<a id="ar-40"></a>

### AR-40 — Replace the whole-map dynamic veto with scoped evidence

Тип: **Исполнительская**. Размер: **M**.

```text
Task ID: AR-40
Task type: Implementation
Estimated size: M
Title: Replace the whole-map dynamic veto with scoped evidence

Repository and working rules:
Work in the supplied OpenJK-rend2 checkout for the keyten/Jedi-Knight-2026 project on the active Rend2 development branch (historically rend2-unified-wip). Inspect current code, git status, relevant AGENTS.md instructions and the actual revision/remote before editing. Preserve unrelated, concurrent and staged changes, including ASR25. Verify and reuse newer completed work.

Product objective:
Deliver data-only Aesthetic Relighting of original stock maps: visually motivated modern sources attached to actual lamps/screens/BSP/model emission, plus justified nonlocal illumination. A means a visibly supported fixture, B an independently motivated probable unseen source, C supported illumination without an identified lamp, and D unresolved/conflicting evidence with original-data fallback. Historical type, XYZ, power and exactLight certification are not aesthetic eligibility requirements.

Mandatory boundaries:
Preserve historical solver behavior and existing canonical/storage meanings. Do not modify BSP/lightmaps, author unmotivated lights, implement GPU/runtime consumers or world shading, or solve double lighting. Unknown geometry is not empty and unavailable predictions are not black. Encoded RGB8 is not calibrated linear irradiance. Record modern-model/transfer/gauge assumptions, bounded approximations and assistance. Never promote a conditional lamp or processed map to historical truth or complete reconstruction.

Objective:
Permit the specifically approved locally supportable regions of maps containing movers to use the existing aesthetic pipeline, while preserving unknown motion/occlusion and required-path safety.

Required inputs and inspection:
Inspect docs/rend2-relighting/corpus-blocker-decision.md and its chosen local-state proof/assumption contract, tools/rend2/stock_aesthetic/discover.py, the native grid bridge, SourceSupport, PointRefine occupancy, captured model bounds/states and ASR25 DynamicMapUnsupported cases.
Activation: Do not start implementation unless docs/rend2-relighting/corpus-blocker-decision.md explicitly selects this branch and its target maps. An inventory blocker count or a completed decision file alone is not authorization for all repair branches. Otherwise retain Deferred and record the missing choice.
Use the current versions of named artifacts. A missing required decision/input is a precise blocker, not permission to invent it; complete useful independent inspection. This prompt requires no previous chat or another task prompt.

High-level action plan:
1. Separate static-world fixture identity, local source support, receiver paths and mover/state uncertainty instead of rejecting every candidate from any map with dynamic_models metadata.
2. Implement only the selected evidence-backed local policy using existing BSP, visual geometry and occupancy providers. Unknown travel extents cannot be bounded by captured pose alone.
3. Require an actual certificate or explicitly approved conditional scene-state hypothesis for every admitted scope. Label conditional assumptions; an assumption is not an independently known-clear ray.
4. Keep unavailable required paths unavailable and reject independently known solid intersections/unknown optical geometry. Retain the old strict path and historical behavior.
5. Scope cache/provenance/coverage to the chosen state interpretation and verify moved/unknown-door, far-static-fixture, same-room occlusion and stale-state counterexamples.
6. Run the approved affected-map regions and report newly examinable support, conditional coverage, unchanged rejections and any remaining physical/input blocker.

Definition of Done:
- The global controller veto is replaced only where equivalent local evidence/explicit policy exists; required geometry and occupancy checks still decide admission.
- At least one approved local scope reaches the intended next stage, or the measured lack of the required evidence is explicitly returned to a decision instead of bypassed.
- Newly processed dynamic-map scopes are not claimed historically bake-correct, unconditionally free or completely reconstructed.
- Deliver the complete scoped artifact, verification evidence and the mandatory high-level plan-result append.

Non-goals and things not to do:
Do not remove the dynamic guard unconditionally, use a current AABB as an unknown mover's full travel envelope, ignore doors/models in visibility, mark conditional assumptions as proven empty geometry, or activate runtime lighting.
Implement the specified mechanism completely through the existing real path with meaningful verification. Do not substitute a loose sketch, disconnected helper, stub or report for the required integration. Reuse already complete operations. If the scoped work exceeds M or its required decision is absent, document the concrete split/blocker; do not silently expand the work or decide a new product policy.

Required completion and plan update:
Open docs/rend2-aesthetic-relighting-plan.md and append a high-level result under "AR-40" in Results Log, including date/revision, actual scope and assistance, result, measured evidence and artifact paths/hashes, A/B/C/D and coverage implications, limitations, decision and concrete next action. Update the task's status honestly even when the outcome is Partial, Failed or Blocked. A completed investigation can make a valid negative decision; it cannot turn a failed product gate into Passed. Do not claim measurements that were not performed. Do not silently change acceptance criteria or the plan direction; propose a specific decision task if a branch is needed.
```

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
Current evidence update: docs/rend2-stock-aesthetic-asr24.md and docs/rend2-stock-discovery-asr25.md now document substantive one-source stock results and a full inventory audit. Reuse their frozen summaries/checks and current code, including staged changes, instead of repeating a zero-result baseline audit.
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Current evidence update: two bounded one-source stock configurations are accepted under the ASR24/25 conditional policy, but neither map is fully relit. Distinguish the early viability protocol from final whole-map acceptance; original-grid corroboration is not a mandatory universal requirement for every visually motivated A fixture.
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Current evidence update: the existing modern DiffuseIntegral plus conditional forward encoding already supports useful stock fits. Use the real-stock viability decision to justify which model gaps need new implementation; do not require a full evaluator rewrite merely to replay or extend the working baseline.
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Current evidence update: ASR24/25 use catalogue-derived fixture evidence but accept fixed mounted-point proxies, not exact native clipped area evaluation. Preserve this useful regression path while proving the missing finite-footprint integration.
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Current evidence update: ASR25 identifies model-source discovery as a specific Hoth gap. Entity model-reference counts are not lamp counts; implement the existing instance/emission bridge only for locally supportable selected cases.
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Current evidence update: use the fixed ASR25 failure witnesses. A whole-map static-map guard is a separate controller limitation from an actual local Unknown support; do not conflate them or repeat corpus-wide diagnostics.
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Current evidence update: ASR24 already implemented native outward receiver-hit selection, zero-dummy-RGB unit-response coverage, disjoint folds and a frozen source-removal audit. Reuse them and diagnose only remaining multi-fixture/multi-surface sampling gaps; the old mp/duel4 zero-response study is preserved historical evidence, not a reason to recreate commissioning from scratch.
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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

### AR-12 — Freeze an expanded stock pilot bank under the final modern policy

Тип: **Управленческая / экспериментальная**. Размер: **M**.

```text
Task ID: AR-12
Task type: Decision / Experiment
Estimated size: M
Title: Freeze an expanded stock pilot bank under the final modern policy

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
Current evidence update: a minimal viability protocol and its go/repair/pivot decision must already exist as named artifacts under docs/rend2-relighting/ or tools/rend2/aesthetic_relighting/policies/. This task expands to a representative pilot bank under the chosen final modern policy; it must not postpone the first stock demonstration or rerun ASR24/25 as newly blind evidence.
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Current evidence update: ASR24/25 demonstrate that the existing fixed-fixture aesthetic RGB fit works on real observations. Preserve those native numerical reports as regressions. Implement only policy/evaluator/material integration gaps justified by the stock viability decision, not a replacement fitter.
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Current evidence update: reuse stock_aesthetic/report.py, stock_audit/packet.py, geometry plates, hypothesis files and frozen response plates. Their existing geometric/single-chart evidence is not a whole-room relighting preview; add only the spatial/contextual review needed by the selected decision.
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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

### AR-16 — Accept or reject the first stock region (Superseded; архив)

Тип: **Управленческая / экспериментальная**. Размер: **M**.

```text
Task ID: AR-16
Task type: Decision / Experiment
Estimated size: M
Task status: Superseded; archived deferred gate. Do not execute this prompt.
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
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Current evidence update: tools/rend2/stock_audit/ already accounts for all 57 BSPs and measured blockers. Import its census/provenance, then add actual scene work/accepted-area accounting; do not repeat inventory processing and label it whole-map reconstruction.
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Current evidence update: reuse the existing two-worker stock_audit controller, frozen policies and per-stage receipts. Its 57-map audit is not a resumable accepted-reconstruction pipeline, and its old capture manifest does not prove current file availability.
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Current evidence update: ASR25 already provides 210 original-observation ROIs on 35 maps and a failure packet. They are development evidence for designing this bank, not untouched final tests. Preserve the two accepted bounded source examples and all rejected scopes.
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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
Execution authorization: This is a post-viability roadmap item. Read docs/rend2-relighting/viability-verdict.md and the current approved branch before substantive work. Proceed only when that decision explicitly needs this deliverable or authorizes a bounded research/repair scope. A completed negative decision is not Go. If the verdict is absent, Stop, or does not authorize the scope, retain the work as Deferred/Blocked and perform only useful independent read-only inspection; do not launch the large roadmap automatically.
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

### PLAN-REVISION-02 — ASR24/25, ранняя жизнеспособность и измеренные blockers

- **Дата / revision:** 2026-11-10; текущий HEAD `bddf5b376`, пакет ASR25 находится в индексе. Область этой записи — пересмотр документа по запросу пользователя, а не новый запуск реконструкции.
- **Высокоуровневый результат:** ASR24 уже доказал условный ненулевой fixture-associated fit на mp/duel10; ASR25 добавил отдельный цветной пример mp/duel7 и актуальный 57-map аудит. Утверждение исходного плана об отсутствии положительного стокового результата заменено на точное описание двух bounded one-source конфигураций.
- **Свидетельства:** `docs/rend2-stock-aesthetic-asr24.md`, `docs/rend2-stock-discovery-asr25.md`, их summary/checks/manifest и per-map таблицы. При пересмотре сверены все четыре публичных артефакта ASR24 и шесть файлов manifest ASR25; хеши совпали. Native fitting, visual acceptance и корпусные расчёты заново не выполнялись.
- **Ограничения:** два принятых ROI не равны двум реконструированным картам; белый пример имеет один held-out блок, жёлтый — высокий остаток и необъяснённое пятно. Оба используют условный point-прокси/stand-off/appearance transfer. Прежние final данные после их использования для планирования считаются development/regression evidence.
- **Изменение направления:** сначала короткие AR-34/35/36 с ограничением scope и обязательным Go/Repair/Pivot/Assisted/Blocked/Stop; полная смена evaluator, поля и whole-map инфраструктура не являются условием начала. Поздний AR-16 архивирован как Superseded, а не помечен успешно выполненным.
- **Новые предметные ветви:** AR-37 выбирает, какие ограничения ASR25 реально мешают ближайшему этапу; AR-38/39/40 адресуют captures/sections, catalogue budgets и local mover safety. Они Deferred до соответствующего решения. Отсутствующий input и общий controller veto не выдаются за доказанную неудачу модели освещения.
- **A/B/C/D и покрытие:** текущие численные конфигурации остаются условными кандидатами AestheticCandidate; whole-map площадь A/B/C/D пока не принята. Все 57 BSP учтены, но доступны только 37 capture-файлов; 53 per-map Blocked не превращаются в нулевой принятый свет. Измеренные blocker counts пересекаются.
- **Следующее действие:** выполнить AR-34 и зафиксировать минимальный новый multi-fixture viability protocol. Ни AR-34–40, ни оставшиеся исходные задачи этим редактированием не исполнены; шансы полного завершения не объявлены и успех не обещан.

### AR-16 — изменение статуса без выполнения прежнего gate

**Task status: Superseded. Outcome: Unknown / not executed.** Отложенная задача первого принятия стокового участка заменена ранним обязательным решением. Её исходный промпт сохранён как архив и помечен «Do not execute». Полезные требования — независимая проверка, фиксация неуспехов, ограничения и изменение плана — перенесены в действующий ранний gate. Это организационное изменение не утверждает, что связный multi-fixture участок уже принят.

### AR-34 — Frozen proof; named-source admission blocks execution

- **Date / revision / branch:** 2026-11-10 (Australia/Sydney), inspected `dff8ea77acc1fc578239af69b17253e3a12b7c9f`, completion HEAD `a8cfaa215192dbf7693d1060ef8e8900329a3710`, `rend2-unified-wip`; actual origin `git@github.com:keyten/BetterJK.git`. ASR25 is committed, with no staged changes. Concurrent `new plan` committed only the pre-existing plan/launch documents: its plan text matches the pre-AR-34 snapshot after Git line-ending normalization and launch-note bytes remain unchanged. No commit was created by AR-34.
- **Task status: Done. Outcome: Blocked-SourceSupport. Product gate: Unknown.** This completed investigation credits exactly two existing conditional stock configurations: preserved duel10/942 and new duel7/1657, not three. It accepts no new light, map or room.
- **Actual scope / assistance:** Codex inspected current code, public receipts, licensed geometry, source image and old appearance plates; selected two original duel10 housings (987/988), one proxy per housing, and three same-material receiver charts (705/706/707). No subagent or independent human review was used. Original target RGB was discarded during geometry selection and never fitted/scored/viewed; native fitting ran only on literal dummy zeros to test admission.
- **Measured evidence:** all 4 + 6 public manifest entries, 209 ASR24 private entries, 150 ASR25 implementation files including 113 core/canonical files, six tools and seven selected input identities match. Fresh geometric sampling: 941 rows, 18 both-positive, 37 single-positive, 604 known-zero, 282 unavailable; finer trim sample 394 rows, 57/64 exclusive source responses, 199 known-zero, 74 unavailable. Joint native coefficients on the frozen 141-row subset equal individual sums exactly. Primary final is 34 rows / eight blocks / three charts with 5/8 source-positive rows and two lit/control pairs per housing.
- **Blocking result:** the stronger native admission probe refuses all three local configurations: `Named mounting surface absent at declared contact plane`. Geometry-only response is not admission. A float-normal/contact-plane precision mismatch is a hypothesis requiring measurement. The 26-row spare fold lacks housing 988 control pairs; it cannot authorize a later repair acceptance. Failed fold designs, the initial axial clip, a scratch block-ID collision and the corrected admission metadata are retained.
- **Artifacts / hashes:** [decision](rend2-relighting/viability-decision.md), [protocol v1](../tools/rend2/aesthetic_relighting/policies/viability-v1.json), [evidence](../tools/rend2/aesthetic_relighting/results/ar34-v1/evidence.json), [public hash manifest](../tools/rend2/aesthetic_relighting/results/ar34-v1/manifest.json); licensed packet `build/ar34-v1/`. Protocol SHA-256 `96da96c0ccd896bca3751f539b2d8860f4e1d0580f5d947c60c906b5a84a28d0`; evidence SHA-256 `6cd5283c1491153f40588ebfeea17aaa3b94bfe9a0b52dafb8056b739de427cf`. Existing duel10/duel7 native reports retain SHA-256 `c72d343d52d7c8b893f7232fd262f921e421c7a1bb23f2cb2ddd21759c70af70` / `b1f475579816fceb291fdd555ec0d0c0a91dd467829f81d1bfae1d6f63dd54d0`.
- **A/B/C/D / coverage:** two visibly motivated A candidates have unresolved admission and D/original-data fallback; no new accepted coverage, B lamp or C field is delivered. Known-zero direct transport is not observed darkness. Different-material overlapping floor/ledge appearance remains unavailable; neither the primary subset nor the old chart successes prove a complete room. Unknown geometry/predictions remain unknown, and RGB8 remains conditional encoded appearance.
- **Decision / next action:** AR-41 (new standalone S decision) must diagnose the exact named-mount refusal. AR-35 is Blocked until a causal receipt explicitly authorizes a safe modern-path integration. Otherwise AR-36 takes the blocked receipt directly. Freeze existing DiffuseIntegral/conditional encoding, gain 1, bounded RGB and one constant nuisance; no finite-radius/MD3/fields/whole-map prerequisite. At most one M integration plus one M repair before one original-target fit/repeat and the mandatory verdict; no post-final repair fit or second ROI is authorized by v1. ASR24/25 numerical thresholds and failures are unchanged; new final evidence is independent receiver support on a development map, not blind map transfer.

### AR-35 — Missing decision; existing multi-fixture path inspected

- **Date / revision / branch:** 2026-11-10 (Australia/Sydney), `25bd45803c8918b5059a3add56f123ac1f81f4c6`, `rend2-unified-wip`, origin `git@github.com:keyten/BetterJK.git`; clean starting worktree/index, no applicable `AGENTS.md`. Concurrent untracked `tools/rend2/stock_lights/` was preserved; no staging or commit was performed.
- **Task status: Blocked. Outcome: Blocked-MissingDecision/SourceSupport. Product gate: Unknown.** The required `docs/rend2-relighting/contact-plane-diagnosis.md` and explicit AR-41 authorization receipt are absent; unchanged viability-v1 remains `FrozenBlocked` / `integration_authorized: false`, so production integration and original-target fitting were not started.
- **Actual scope / assistance:** Codex mapped all seven required operations to current stock/native code, inspected ASR24/25 accepted and failed evidence, and ran independent integrity, geometry/admission and regression checks; no subagents or separate reviewer were used. Existing CLI enumeration, joint RGB/shared-background fit, saved-parameter all-source replay and report/OBJ helpers are reusable; remaining work is the explicit frozen multi-chart cohort, complete fixture provenance, individual removals, per-receiver diagnostics and registered multi-chart views, all gated by AR-41.
- **Measured evidence:** ASR24 4 public/209 private, ASR25 6 public, AR-34 5 public/42 selected private and 12 accepted-witness references match; all 150 ASR25 implementation files including 113 core/canonical files and six tools match. Frozen receiver IDs/blocks and counts remain disjoint and match policy; original capture/row-file identities were rehashed without parsing pilot RGB. Four native dummy/geometry probes repeat AR-34 stdout byte for byte: all three configurations unavailable at the named contact plane, 141/141 joint geometry responses available with exact sum error 0, and the 394-row individual census retains 57/64 exclusive positives, 199 known zeros and 74 unavailable rows. Eight native and ten Python regressions pass; dummy fit work is 16,422 units and CTest records 16.47 s, with no original-target quality measurement.
- **Artifacts / hashes:** [operation map and blocked result](rend2-relighting/pilot-wiring-status.md), [evidence receipt](../tools/rend2/aesthetic_relighting/results/ar35-v1/evidence.json), [public manifest](../tools/rend2/aesthetic_relighting/results/ar35-v1/manifest.json), and ignored logs/script/integrity details in `build/ar35-v1/`. Evidence SHA-256 `8894a0e2f9986d7f31ac31d53f201646e2056f9d43290d9f4b5946ac88b12e9c`; manifest SHA-256 `1166ceed04316987f88f5d735a84a4ec2c82279e290927a4d856355b0a398849`; unchanged policy SHA-256 `96da96c0ccd896bca3751f539b2d8860f4e1d0580f5d947c60c906b5a84a28d0`. A receipt-only relative-path error after completed checks was corrected using retained outputs; individual subprocess timings were lost and are not claimed.
- **A/B/C/D / limitations:** the two visible A candidates retain D/original-data fallback, with no new accepted coverage, B lamp or C asset; unavailable predictions remain unknown and known-zero direct response does not imply darkness. Point/stand-off, occupancy, conditional encoded appearance/gauge, bounded nuisance and incomplete reserve controls remain unchanged; no original pilot fit, selected-parameter replay, individual-removal result or independent visual review exists, and implementation is not Done.
- **Decision / concrete next action:** retain the existing block and complete AR-41's exact two-mount causal diagnosis/authorization, or take this negative preflight receipt directly to AR-36. No criteria, roadmap direction, integration/repair allowance or fit-cycle limit changed; no tolerance bypass, source relocation or new optimizer was introduced.

### AR-41 — Measured contact-plane cause; negative receipt to AR-36

- **Date / revision / status:** 2026-11-10 (Australia/Sydney), inspected/completion `89a40b0fc027cc5e3e1e1cce5204cc48b526c013`, `rend2-unified-wip`, origin `git@github.com:keyten/BetterJK.git`. Initial worktree/index clean; no applicable AGENTS.md found. **Investigation Done; Outcome Blocked-SourceSupport; product gate Unknown.** No commit, staging or production implementation; newer AR-35 blocked inspection preserved.
- **Actual scope / assistance:** Codex inspected only original duel10 housings 987/988 and their bounded immutable local geometry, rehashed frozen inputs and six executables, replayed four native dummy/geometry probes, and used private C++ calls to existing providers plus Python exact rational measurements. Eight owned synthetic numerical controls isolate the predicate; they are not a replacement stock experiment. No subagents, separate reviewer or new original-appearance review. Receipt finalization records about 23 minutes of investigation against the two-hour cap; every diagnostic subprocess has a 300-second cap.
- **Measured cause:** both named original triangles are present in each 11-triangle contact extraction, match original BSP geometry exactly and are opaque/error-zero. Whole-mount extraction returns 30 triangles, still both named originals. Exact plane determinants and resolved-double-normal residuals are zero. `FromSurfaces` stores the normal as float; `Check` then rejects each triangle at residual **2.384185791015625e-7 WU** against bounds **1.624448436864759e-11 / 1.6244664421924223e-11 WU** (about 14,677 times smaller). Rational arithmetic reproduces the residual exactly, ruling out a mere absolute-dot cancellation explanation. Axial/equal-component oblique positives admit; shifted-anchor/wrong-side/missing-ID controls refuse; an exactly planar unequal-component oblique control reproduces the failure. This measures the immediate refusal, not complete downstream safety.
- **Reproduction / integrity:** all four native stdout streams are byte-identical to AR-34: three configurations unavailable, 16,422 dummy fitter work units; 141/141 joint geometry responses available with exact additivity; individual 394-row census 57/64 exclusive positives, 199 both-zero and 74 either-unavailable. All 223 file-hash comparisons (217 path strings), ten ASR24/25 public entries, 150 implementation files, six native tools and original BSP/capture/catalogue/rows match. Original row-file RGB was not decompressed or scored. Existing `Offline::Load` does deserialize archive image buffers; no receiver image values were indexed/displayed/evaluated by the diagnostic. This explicit controller non-use boundary replaces any overbroad claim of zero RGB bytes in memory. No regression-suite rerun or original-quality result is claimed.
- **Artifacts / hashes:** [causal diagnosis](rend2-relighting/contact-plane-diagnosis.md), [evidence receipt](../tools/rend2/aesthetic_relighting/results/ar41-v1/evidence.json), [validation](../tools/rend2/aesthetic_relighting/results/ar41-v1/validation.json), [manifest](../tools/rend2/aesthetic_relighting/results/ar41-v1/manifest.json); private diagnostic source/executable, exact XYZ/triangles/normals, rational analysis, logs and scratch corrections remain ignored in `build/ar41-v1/`. Evidence SHA-256 `7c0c5f81b35fd132d63e0ba120ac9c93e692e25e12709cd04a0047b08d09166c`; manifest SHA-256 `226d06119ca6a6aedc7c3caebd7ba9b36efbd23bf5ef897c83ecbdd744c5631a`; unchanged viability-v1 SHA-256 `96da96c0ccd896bca3751f539b2d8860f4e1d0580f5d947c60c906b5a84a28d0`. The manifest pins diagnosis/evidence/validation and excludes this plan to avoid self-reference. Shader whole-file/material-body mismatch, compiler/harness failures, incomplete diagnostic outputs and metadata corrections remain recorded; all AR-34 failures remain immutable.
- **A/B/C/D / limitations:** two visibly motivated A candidates retain D/original-data fallback; zero newly accepted sources/rows/maps/area, no B lamp or C asset. OpticalUnknown mask 16 and alpha-tested nearby geometry remain unresolved even with complete extraction. Independent point-domain/occupancy queries are Free, but later visual/clearance/contact checks were not certified. Old conditional stock successes and failures, historical/canonical/storage meanings, fixed poses, support, gauge/encoding and bounded nuisance remain unchanged. No original-target fit, selected pilot replay/removal, final score or independent visual acceptance exists.
- **Decision / next action:** **AR-36 next, on the measured negative receipt; AR-35 remains Blocked and integration unauthorized.** A justified versioned modern oblique plane-witness contract is absent; finding float quantization does not authorize epsilon widening, a new double field, guard removal, relocation/free points or unknown-space clearing. No safe one-M existing-contract adapter repair was established. AR-36 must issue a bounded Blocked/Stop/Pivot decision without demanding a nonexistent fit; this pre-fit defect does not itself justify an appearance-model pivot. One M integration/eight hours and at most one pre-target M repair/four hours remain the total following ceiling, not currently authorized spending. Material pose/geometry-response/acceptance changes need a new explicit freeze before original exposure; after final-informed changes require fresh adequate independent final evidence. V1 reserve still lacks housing-988 controls and cannot authorize post-final repair fitting. No new scope, new task or roadmap rollout was launched.

## 8. Журнал изменений плана / Plan Change Log

| Дата | Основание / evidence | Решение | Затронутые задачи / версии | Последствия для проверки |
|---|---|---|---|---|
| 2026-11-10 | Текущий код и отчёты ASR09–23, рабочая разработка stock_aesthetic | Исходный data-only roadmap: 33 S/M-задачи; современная aesthetic цель отделена от исторической идентификации | AR-01–AR-33; численные политики ещё предстоит утвердить | Ни один исходный frozen benchmark не переписан; новые final gates требуют предварительной фиксации |
| 2026-11-10, редакция 2 | Запрос пользователя о риске повторить длинный план; ASR24/25 reports, checks и совпадающие public artifact hashes | Признать существующие stock successes; вынести multi-fixture viability в начало, ограничить ремонт и ввести явное Go/Repair/Pivot/Assisted/Blocked/Stop | Добавлены AR-34–AR-40; AR-16 Superseded; изменены приоритеты, зависимости и evidence/reuse inputs исходных промптов | Старые final результаты остаются regression/development evidence; новый protocol и независимая проверка до расширения scope |
| 2026-11-10, редакция 2 | ASR25: 20 MissingOriginalCapture, 23 DynamicMapUnsupported, 11 CatalogueWorkBudget, 1 CataloguePolygonBudget, 2 CaptureSectionUnsupported; классы пересекаются | Выделить управленческий выбор конкретной ветви и три ограниченные реализации; не повторять общий аудит и не снимать геометрические проверки | AR-37 → по решению AR-38/39/40; AR-24/29/31 используют измеренный corpus baseline | Учитывать восстановленные inputs и разблокированные stages отдельно от принятого relighting; неизвестность не становится успехом |
| 2026-11-10, AR-34 | Inspected dff8ea77a; completion a8cfaa215 commits only pre-existing plan/launch documents; current ASR24/25 hashes and private packets; duel10 byte-identical | Credit two conditional stock configurations once; freeze one new two-housing/three-chart same-material proof using existing native evaluator | AR-34 Done / Outcome Blocked; viability-v1 and evidence receipt | Old finals remain development/regression; no new original-target fitting or accepted coverage; numerical 10%/5% gates unchanged |
| 2026-11-10, AR-34 | Geometry-only unit responses available, but native dummy-target SourceSupport::Check refuses both oblique mounts | Add standalone AR-41 (S) causal diagnosis; gate AR-35 on explicit safe authorization; allow AR-36 to decide from a blocked receipt | AR-41 Pending; AR-35 Blocked; dependencies and execution clauses updated | Preserve named-source and unknown-geometry checks; no speculative evaluator/contact tolerance or automatic roadmap rollout |
| 2026-11-10, AR-34 | Geometry-only 128/64 WU hash designs insufficient; four-role stratified 64 WU design has fresh IDs/blocks; spare lacks housing 988 controls | Explicit new geometry-only fold policy and assistance; primary final 34 rows / three charts / eight blocks; grid diagnostic for new visible A scope, old three-record gate retained for old studies | Viability-v1 only; at most one M integration + one M repair before one fit/repeat and mandatory AR-36 | Bake independence remains conditional; no after-final repair fit, policy retuning or second ROI under v1; any later change informed by final requires a new decision and fresh adequate final evidence |
| 2026-11-10, AR-41 | HEAD 89a40b0fc; exact original coplanar triangles present; float-normal residual 2.384185791015625e-7 WU exceeds existing double-roundoff bound; eight native numerical controls; no justified oblique contact contract | Finish AR-41 with Blocked-SourceSupport; route measured negative receipt directly to AR-36; do not authorize AR-35 integration or a numerical repair | AR-41 Done; AR-35 Blocked (negative authorization, no longer missing receipt); AR-36 next; active route, task counts and standalone AR-35/36 prompts updated; ar41-v1 receipt added | Viability-v1 and all old results/failures unchanged; no original-target score or accepted coverage. Any material pose/support/acceptance change needs a new explicit freeze; any final-informed change requires fresh adequate independent final evidence. No extra coding allowance, post-final repair fit or second scope |

Управленческие/экспериментальные задачи добавляют сюда изменения. Следующие новые задачи получают ID начиная с AR-42, оценку S/M и полный автономный английский промпт. При замене задачи сохраняются её прежняя запись и ссылка на основание пересмотра.

