# Проверка static sun-shadow cache

Проверено 2026-10-21 в отдельном worktree `sun-shadow-cache`, база `6e6305f91`.
Основной checkout и установленная игра не изменялись. Собраны Release x64
SP/MP renderers, SP engine и `jagamex86_64.dll`; игра запускалась с собственными
`fs_homepath`, DLL, конфигами, логами и скриншотами. Installed PK3 использовались
только для чтения.

## Результат

- Финальный тонкий mode 1: **88/88** сравнений с полным рендером совпали
  побитово на `mp/ffa3`, max depth error = 0. Проверены также исчезновение и
  возвращение entities: старые динамические тени должны стираться. Артефакты:
  `build/sun-shadow-runtime-thin-final/mode1/run-0.json` и `.log`.
- На Yavin финальный предохранитель переключает unbounded каскады на baseline:
  19/19 выполненных cached/reference сравнений побитово, после переключения
  restore = 0 Mpix/frame и 3 baseline bypasses/frame. Это намеренно меньше
  сравнений: bypass-кадры не выдаются за cache hits. Артефакты:
  `build/sun-shadow-runtime-thin-yavin/mode1/run-0.json` и `.log`.
- На exact hit восстанавливается только прошлый dynamic footprint, без полного
  копирования слоя. Известные bounds: BSP, Ghoul2 culling sphere, MD3 union
  текущего/предыдущего animation frame. Unknown/deformed/moving foliage —
  полный restore на первом cached кадре, затем mode 1 возвращается к single-pass
  redraw до изменения проекции/настроек. Повторные Ghoul2 body surfaces не
  пересчитывают sphere.
- Raw foliage evidence не равно движению: `FOLIAGE_TWO_SIDED` на одежде не
  включает полный restore и не исключает обычный static BSP из кэша.
- Опциональный `glCopyImageSubData` заменяет depth FBO blit для restore;
  на старом GL остаётся fallback. PCSS и прочие потребители теней не изменены.
- При движении mode 1 использует обычный single-pass redraw, пока проекция
  не совпадёт четыре раза подряд. На miss нет обязательного static build +
  depth copy + dynamic pass. Cache default остаётся 0: проверена только одна
  GPU и ограниченный набор сцен; режим 1 включается явно.
- Mode 2 остаётся экспериментальным. Расширенный тест предыдущей промежуточной
  сборки нашёл 5 MISMATCH из 432 сравнений, в том числе вне one-texel footprint.
  Ранние 84/84 PASS не означают доказанную корректность clipmap в gameplay.
- Mode 3: историческая загрузка страниц между запусками подтверждена: в контрольном запуске
  загружено 8 окон по 25 страниц. 83/84 сравнений прошли допуск, 38/84 совпали
  побитово. В одном сравнении near cascade один тексель из 1,048,576 вышел за
  допуск (normalized depth error в этой проверке до `1.20997e-5`). Mode 3 остаётся
  экспериментальным; интеграционный тест честно возвращает ошибку для этого
  случая. После последних правок mode 3 повторно не проверялся.
  Максимальная разница во всей исторической серии mode 3 — `0.422208` на границе
  покрытия, укладывающейся в однопиксельный footprint; это не побитовое равенство.
- Явных GL_INVALID/GL_OUT_OF_MEMORY/FBO/shader compile/link ошибок в сохранённых
  логах не найдено. Это проверка логов, не полная валидация OpenGL debug context.
- В предыдущей серии просмотрен игровой screenshot; сравнение неподвижных beauty-кадров mode 1/2
  дало среднюю абсолютную разницу RGB около 0.067/0.067/0.048 из 255 и 25 пикселей
  с разницей >20 из 518,400. Разные запуски не синхронизируют анимацию персонажа,
  поэтому это только визуальная проверка, а не тест эквивалентности цвета.

Диагностика `r_sunShadowVerify` использует одинаковую стабильную проекцию для
кэша и полного рендера, сохраняет строгую D24-метрику и отдельно проверяет
симметричный footprint 3x3 с depth-допуском max(8 D24 steps, 5% world texel).
Описанный выше PASS означает этот геометрический допуск. Проверялись небольшие
и диагональные сдвиги, движение, большие телепорты и возвраты, переключение
`r_drawworld`, `r_nocurves` и `r_facePlaneCull`.

## Производительность

Intel UHD Graphics, `mp/ffa3` в SP engine, окно 960x540, shadow maps 1024,
PCSS включён, dynamic lights/cubemaps/SSR/SSGI/volumetric fog выключены.
Раннюю таблицу «GPU стал медленнее» нельзя использовать как строгий вывод:
end timestamp отправлялся только при submit следующего view, поэтому интервал
мог включать ожидание CPU-подготовки следующего вида. Теперь begin/draw/end
отправляются вместе. Кроме того, прежний запуск передавал >32 `+set` команд,
превышая SP `MAX_CONSOLE_LINES`: хвост склеивался в значение одного cvar.
Все настройки перенесены в private `openjk_sp.cfg` до renderer initialization.

После исправлений медианы суммы sun-pass timestamp в отдельных запусках:
полный redraw около 1.46–1.47 мс, mode 1 stationary около 0.43–0.55 мс,
mode 1 moving около 1.32–1.50 мс. Это **не контролируемый процент ускорения**:
прогоны отличались фоновой нагрузкой/пейсингом. В финальном stationary-окне
restore составлял около 0.640 Mpix/frame вместо обязательных 3.146 Mpix/frame;
размер зависит от положения и conservativeness bounds caster-ов.

Добавлен `r_sunShadowBenchmark N`: steady-clock интервалы полных кадров,
mean/median/p95 и FPS, без per-frame logging самим измерителем. Стенд снимает
FPS cap, vsync, verification, shadow stats и r_speeds для FPS-окон; developer
тоже выключен, чтобы ожидания GPU не писались в лог каждый кадр.

Серия 0/1/0/1 по 600 кадров на stationary/moving в
`build/sun-shadow-runtime-thin-perf` действительно завершилась, но во время
серии другой агент запустил `build/ffa3-runtime/openjk_sp.x86_64.exe` (PID 25852).
Был также включён developer logging. Эти FPS **не принимаются как A/B результат**
и не обосновывают ни FPS gain, ни FPS regression. Новый стенд опрашивает process
inventory и сохраняет `foreign_game_pids` / `performance_isolated`; посторонние
игры он никогда не приостанавливает и не завершает. Нужен повтор в согласованное
окно без других игр и тяжёлой фоновой работы.

Mode 2 всё ещё платит за ping-pong scroll и дополнительные strip views.
Mode 3 дополнительно требует синхронного readback и записи новых страниц.
Их не следует выбирать как оптимизацию FPS без отдельного профилирования.

### Повторный A/B финальной сборки

`build/sun-shadow-runtime-thin-perf-clean`: 0/1/1/0, по 600 complete frame
intervals на каждое окно, uncapped, developer 0. Во всех четырёх прогонах
`foreign_game_pids=[]`, GL/FBO/shader ошибок в логах нет. Это отсутствие
других OpenJK-процессов, **не** гарантия изоляции всей системы.

| Режим | Stationary FPS, два прогона | Среднее время полного кадра | Медиана sun GPU, два прогона |
|---|---:|---:|---:|
| 0 | 94.18 / 93.75 | 10.6423 мс | 1.388 / 1.298 мс |
| 1 | 98.88 / 104.05 | 9.8621 мс | 0.4715 / 0.4525 мс |

По совокупному времени 1200 stationary кадров на режим: 93.96 → 101.40 FPS,
около +7.9%. Это предварительный локальный результат на `mp/ffa3`, не обещание
такого выигрыша в gameplay или на другой GPU. Sun-pass GPU стоимость в этих
окнах уменьшилась, а не просто обменялась на CPU-экономию.

Moving-окна не дают надёжного сравнительного FPS: mode 0 показал 81.07 и
25.19 FPS (во втором окне p95 119.6 мс), mode 1 — 91.34 и 90.78 FPS.
Причина больших пауз второго baseline-прогона здесь не установлена;
не следует приписывать их отсутствию кэша или заявлять по ним speedup.
Маршрут задаётся input/wait-командами, не фиксированным timedemo, и не
обеспечивает идентичную траекторию при разном FPS. Для moving gameplay нужен
отдельный детерминированный benchmark. В режиме 1 при непрерывной смене
проекции используется baseline draw без обязательных depth copies.

## Исправления, найденные при запуске

- Scissor предыдущего вида обрезал depth-blit; копии теперь выполняются без него.
- Узкая raster-проекция полос меняла clipping/interpolation. Полосы используют
  полный viewport/projection, узкий culling frustum и отдельный scissor,
  сохраняемый также при повторном viewport restore.
- Повторное преобразование light origin давало зависимое от XY округление Z.
  В матрицу записываются исходные scalar light coordinates.
- Screenshot PBO мог перехватить синхронный depth readback и сохранить нулевую
  страницу. Pack/unpack PBO state теперь сохраняется и восстанавливается.
- При upload страницы явно выбирается texture unit: cached texture binding
  сам по себе не переключает active unit.
- Края окна не имели полных дисковых страниц. Страницы строятся отдельно,
  максимум по одной на каскад за сцену, с 16-texel guard band.
- Draw-world/curves/culling/curve-LOD настройки включены в инвалидацию;
  Force Sight поверхности остаются dynamic.
- Исправлены границы view timestamp и лимит startup-команд тестового стенда.
- Verifier возвращает actual depth после reference render: следующая проверка
  тестирует настоящий partial restore, а не слой, оставшийся от reference.

## Воспроизведение и артефакты

```powershell
python tools/rend2/test_sunshadow_cache_runtime.py --installation C:/path/to/game --binaries build/sun-shadow-cache/Release
python tools/rend2/test_sunshadow_cache_runtime.py --installation C:/path/to/game --modes 0 1 0 1 --no-verify --benchmark-frames 600
```

Финальный mode 1: `build/sun-shadow-runtime-thin-final/mode1/run-0.json` и `.log`.
Расширенная промежуточная проверка mode 2: `build/sun-shadow-runtime-thin-check/mode2/run-0.json`.
Исторический mode 3: `build/sun-shadow-runtime-pages-v4/mode3/run-0.json` и `.log`;
его `disk_windows_loaded` равен 8. Конфиги, исходные TGA и страницы находятся
в соответствующих private `home/OpenJK` каталогах. Test harness расположен в
`tools/rend2/test_sunshadow_cache_runtime.py`.

Проверены `mp/ffa3`, `yavin1` и один GPU. MP renderer компилировался, но MP engine
в этой серии не запускался. Другие карты, разрешения, NVIDIA/AMD, совместная
работа с облаками/volumetrics и длительные игровые сессии не проверялись.
