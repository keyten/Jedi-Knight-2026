# План 1. Проверка в игре и инструменты для уже сделанных GL 4.3 портов

Относится к [rend2-gl43-review.md](rend2-gl43-review.md), пункты 1 и 2 (сделаны, но в игре не запускались).
Объём: небольшой (1–2 дня). Делать **первым**: остальные планы опираются на те же замеры и инструменты.

## Зачем

Оба порта меняют поведение, а проверены только на синтетике в SDL-тесте:

- **Forward+ на compute** (`r_forwardPlusCompute`) строит другие списки: из CPU-списка выкидываются источники,
  чья сфера не задевает кластер. Ожидается, что картинка не меняется вообще, но это надо доказать на реальной
  сцене и замерить выигрыш.
- **Контакты воды на compute** (`RB_WaterIntersectionCompute`) убирают `glReadPixels`, но меняют способ
  пробуждения спящих тел: теперь через асинхронное чтение с fence, с задержкой 1–2 кадра. Надо проверить, что
  пена появляется, тела просыпаются и не «засыпают» при постоянном контакте.

Без замеров нельзя ни честно описать выигрыш, ни решить, стоит ли браться за планы 2–5.

## Почему картинка Forward+ должна совпасть бит в бит

Источник за пределами своего радиуса даёт ровно 0. В `lightall.glsl` `CalcLightAttenuation` возвращает
`clamp(0.5 * r²/d² − 0.5, 0, 1)`, а это 0 при `d ≥ r`. В физическом режиме за затухание отвечает
`PBRPointAttenuation`; нужно проверить, что она тоже ровно 0 на радиусе. Spot-источник ограничен сферой вокруг
конуса (`R_SpotBoundingSphere`).

Значит, выкинутые GPU-сборкой источники ничего не добавляли. Порядок оставшихся тот же, а прибавление `+0.0`
сумму не меняет. Отличия возможны только в кластерах, упёршихся в лимит `r_forwardPlusMaxLightsPerCluster`:
там GPU может уместить менее важный источник, который CPU отбросил.

## Шаги

### 1. GPU-таймеры на новые диспатчи

Сейчас время самих диспатчей не видно: оно попадает в соседние таймеры.

- `shared/rd-rend2/tr_forwardplus.cpp`, `RB_ForwardPlusBuildViewGPU`: обернуть `qglDispatchCompute` в
  `RB_ScreenBeginTimer("Forward+ cluster build")` / `RB_ScreenEndTimer`, только для `VPT_MAIN`. Таймеры
  ограничены `MAX_GPU_TIMERS`, на каждый вид их ставить не нужно.
- `shared/rd-rend2/tr_watersurface.cpp`, `RB_WaterIntersectionCompute`: один таймер `"Water depth contacts"`
  вокруг цикла по телам.
- Таймеры работают только при `r_speeds 100`, вне замеров они ничего не стоят.

### 2. Команды-сверки (cheat, только для отладки)

Блокирующие чтения здесь допустимы: команда выполняется один кадр по запросу.

**`r_forwardPlusVerify`** (tr_forwardplus.cpp):
- Флаг `s_fp.verifyPending` ставится командой и обрабатывается в `RB_UpdateForwardPlus` для главного вида.
- Вынести проходы 1–2 из `RB_ForwardPlusBuildView` (подсчёт, префиксная сумма, заполнение) в функцию
  `R_ForwardPlusBinCPU(ranges, …, outOffsets, outCounts, outIndices)` без загрузки в буферы. CPU-путь вызывает её
  и загружает результат, сверка вызывает её рядом с GPU-сборкой.
- После диспатча: `qglMemoryBarrier(GL_BUFFER_UPDATE_BARRIER_BIT)`, затем `qglGetBufferSubData` по grid-диапазону
  вида, по счётчику и по используемой части индексов.
- Проверки для каждого кластера:
  - GPU-список — подпоследовательность CPU-списка, без учёта лимита;
  - число элементов не больше лимита;
  - смещения чётные и не пересекаются;
  - каждый индекс меньше `s_fp.numLights`.
- Вывести сумму ссылок CPU→GPU, число переполненных кластеров и первые 5 нарушений с координатами кластера.

**`r_waterIntersectionVerify`** (tr_watersurface.cpp):
- В кадре проверки после `RB_WaterIntersectionCompute` выполнить старый `RB_WaterIntersectionInject`, но в
  отдельный выходной буфер, не трогая состояние тел. Для этого добавить в Inject параметр
  `std::vector<float> *verifyOut` (`nullptr` — обычное поведение). При `verifyOut` функция пишет только туда и не
  трогает `sim.*`.
- Прочитать `sim.gpuIntersection` через `glGetTexImage(GL_RED, GL_FLOAT)`.
- Сравнить по ячейкам с допуском 3e-3 (точность R16F), вывести по каждому телу: число ячеек с контактом,
  максимальную разницу и число расхождений на пороге 0.01.

### 3. Сценарии в игре

Запуск по рецепту скрытого окна: отдельная тестовая установка и сценарий через `+exec`
(см. память *rend2-game-launch-hidden*). Конфиги положить в `tools/rend2/gl43_validation/`.

**Forward+:**
1. `devmap t2_trip` (или другая карта с помещениями), `r_forwardPlus 1`, `r_dlightMode 2`,
   `r_forwardPlusSpawnTestLights 128`.
2. Скриншоты при `r_forwardPlusCompute 0` и `1`, с `r_forwardPlusDebug 0` и `5` (5 показывает переполненные
   кластеры). Сравнить скриптом: без переполнения разница должна быть ровно 0.
3. `r_forwardPlusDebug 4` (тепловая карта числа источников на кластер) в обоих режимах: при compute должно быть
   ≤ CPU.
4. `r_forwardPlusVerify` — ноль нарушений.
5. `r_forwardPlusBenchmark` при `r_forwardPlusCompute 0` и `1`. Заполнить таблицу в
   [rend2-forward-plus.md](rend2-forward-plus.md): время CPU-сборки, GPU-время главного вида, время кадра.

**Вода:**
1. Карта с камнями или колоннами в воде (`yavin1`, `t3_hevil`), `r_waterSurface 1`, `r_waterInteraction 1`,
   `r_waterIntersectionFoam 1`, `vid_restart`.
2. `r_waterInteractionInfo`: в выводе должно быть «GL 4.3 depth contacts», ненулевые диспатчи.
3. `r_waterIntersectionFoamDebug 1`: оранжевая маска у колонн, скриншоты при `r_gl43 0` и `1` с
   `vid_restart` и замороженным временем волн (`r_waterWaveTime <число>`).
4. **Пробуждение тела.** Отойти от воды, дождаться в `r_waterInteractionInfo` состояния «sleeping» для тела.
   Войти в воду: тело должно стать «active», счётчик «wakes from readbacks» — вырасти.
5. **Постоянный контакт.** Стоять неподвижно в воде 30 с: тело не должно засыпать, пена не должна пропадать.
6. `r_waterIntersectionVerify` — без расхождений.
7. **Время кадра.** Поле `msec` при `com_speeds 1` и таймер «Water surface» при `r_speeds 100`, `r_gl43 0` против
   `1`. На `r_gl43 0` время кадра должно расти с разрешением из-за чтения глубины; на `1` — нет. Замерить при 1080p
   и 1440p.

**Регресс legacy:** те же сцены при `r_gl43 0` — без изменений относительно коммита до портов.

**NVIDIA:** повторить сценарии с NVIDIA-адаптером (настройки Optimus для `openjk_sp.x86_64.exe`).

### 4. Починить устаревшие тесты

Они падают и на HEAD, но мешают считать набор тестов зелёным:

- `tools/rend2/test_water_whitewater.py`: ищет строку
  `u_WaterFoamFlow.xy * u_WaterFoamParams.w + radialFlow`, а в шейдере теперь
  `velocity * u_WaterFoamParams.w + radialFlow`. Обновить контракт.
- `tools/rend2/test_water_intersection_foam.py`: строки `intersectionMask * u_Water[35].z * u_Water[19].w`,
  `interactionField.b * u_Water[19].w + intersectionFoam`, `u_Water[35].w > 0.5` больше не существуют в
  `watersurface.glsl`. Найти их текущие эквиваленты (поиск по `WaterIntersectionFoam`) и обновить. Добавить
  проверку новых строк: `RB_WaterIntersectionCompute`, `u_WaterIntersectionMap`.
- `tools/rend2/test_watersurface_gl.py`: `np.trapezoid` нет в numpy < 2.0 (Python 3.8). Заменить на
  `getattr(np, 'trapezoid', np.trapz)`.
- Новый `tools/rend2/run_gpu_tests.py`: запускает все `test_*_gl.py`, `test_gl43_compute.py`,
  `test_volumetric_compute.py` и контрактные тесты, печатает сводку. Режим `--nvidia` выставляет
  `SHIM_MCCOMPAT=0x800000001`.

### 5. Документация

Внести замеры в [rend2-gl43-review.md](rend2-gl43-review.md) (раздел Validation),
[rend2-forward-plus.md](rend2-forward-plus.md) (таблица бенчмарка) и
[rend2-water-intersection-foam.md](rend2-water-intersection-foam.md).

## Готово, когда

- обе сверки дают 0 нарушений на Intel и NVIDIA;
- скриншоты Forward+ совпадают бит в бит без переполнения;
- пена и пробуждение тел работают;
- замеры занесены в документы;
- набор тестов зелёный.
