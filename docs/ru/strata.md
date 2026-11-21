# Strata — виртуальная геометрия AIVERSE

Состояние (2026-11-20): **интегрировано в main; экспериментальная функция, включаемая
явно; реализованы CPU-планирование, стриминг, переносимый индексированный рендеринг и
закрытые флагами выбор на GPU Vulkan и маршрутизация indirect-отрисовки. Полная
приёмка для production не завершена.**

## Текущая реализация и проверка

| Подсистема | Реализованное поведение | Проверка и оставшиеся ограничения |
| --- | --- | --- |
| Сборщик, швы источника, планирование LOD | Адаптация жёсткой непрозрачной геометрии источника, сертификация иерархии и швов источника, CPU-выбор для каждого вида и гистерезис | CPU-фикстуры проходят; сертификаты не доказывают визуальное качество LOD с потерями |
| Резидентность и стриминг с диска | Страницы индексов в рамках бюджета, откат к корням, асинхронное чтение с диска и общая координация нескольких миров | Тесты жизненного цикла на CPU/записывающем RHI, повреждённого архива и бюджета проходят, в том числе при повторных запусках; облёт под давлением резидентности на реальном оборудовании не выполнен |
| Переносимый рендеринг | Фронты, выбранные на CPU, отправляются через существующий Renderer3D и исходные вершинные буферы | Реальные Metal-фикстуры растра без потерь, глубины солнечной тени и отражения проходят; визуальная приёмка на оборудовании для Vulkan/GLES/GL/WebGL не выполнена |
| Выбор на GPU и временное состояние | Отсечение вычислительными шейдерами Metal/Vulkan и ограниченные временные истории на GPU | Реальные Metal-фикстуры выбора и трёхкадровой временной истории проходят; для Vulkan нужен отдельный шлюз на реальном устройстве |
| Indirect-рендеринг на GPU Vulkan | Явно закрытые флагами пакеты для каждого материала/экземпляра/прохода, потребляемые Renderer3D | Контракты на записывающем RHI и компиляция GLSL/SPIR-V проходят; реальная приёмка вычислений/indirect/временной истории и растра/глубины/отражения на Vulkan не сертифицирована |
| Готовность к production | Экспериментальные флаги и ручные шлюзы паритета по умолчанию выключены | Spot-тени, ROI швов при смешанных LOD с потерями, давление/«хлопки» на оборудовании, нативный arm64 и приёмка производительности текущим QA-прогоном не подтверждены |

Последний [отчёт системного QA](qa/strata-system-2026-11-20.md) фиксирует
проверенный SHA, реальное устройство, доказательства и сбои. Этот прогон на Mac
использовал двоичные файлы x86_64 под Rosetta на Apple M3 Ultra, а не нативные
arm64. Успешное выполнение Metal-фикстур нельзя обобщать на другие бэкенды или сцены.
[Матрица визуальной приёмки](strata-stage18-visual-acceptance.md) остаётся
контрольным списком выпуска; она не означает, что все сценарии пройдены.

Исполняемый файл теста Vulkan поддерживает `--compile-only` (устройство не нужно),
необязательный режим выполнения по умолчанию (код пропуска CTest 77 при отсутствии
устройства) и `--require-device` (отсутствие устройства/вычислений считается сбоем).
Целевой CI на Lavapipe использует строгий режим. Одна лишь компиляция шейдеров не
является паритетом на GPU.

Продолжайте с [передачи дел QA этапа 21](agent-handoffs/strata-stage21-system-qa-2026-11-20.md)
и последующих исправлений. [Исходный журнал функции](agent-handoffs/strata-virtual-geometry.md)
и пронумерованные этапы ниже — исторические контрольные точки реализации;
их датированные утверждения «не реализовано/не протестировано» описывают те этапы,
а не текущую сводку выше.

## Область действия и владение
Strata — независимая от жанра система **представления** для жёсткой непрозрачной геометрии.
Она не должна владеть мирами ECS, симуляцией физики/рельефа, игровой логикой или материалами.
Она не заменяет `GeometryScene`, существующий `GeometryCluster`, AutoLOD,
`GlobalGeometryPool`, батчинг Metal или существующий RHI.
Strata — необязательное представление геометрии, которое потребляет общий рендерер.
Она готовит выборки и данные индексов/indirect; Renderer3D остаётся единственным
владельцем отрисовки, с исходным владением геометрией/материалами и откатом на Legacy.

## Контракт сборщика и CPU-ассета
- `engine/render/StrataGeometry.h`: `ClusterNode`, `SelectionInput`,
  `SelectionOutput`, `validate()`, эталонный CPU-`select()`.
- `engine/render/StrataBuilder.h`: `BuildInput`, `BuildOptions`,
  `Meshlet`, `ClusterBounds`, `Asset`, `build()`.
- `engine/render/StrataModelAdapter.h`: `ModelAsset`,
  `ModelPrimitiveAsset`, `ModelPrimitiveSkip`, `buildModelAsset()`.
  Это адаптер только для чтения, включаемый явно, для CPU-ассетов `Model`, а не хук рендерера.
- Подключайте `aiverse_strata_builder` или `aiverse_strata_model_adapter` в CMake
  проекта при `AIVERSE_BUILD_STRATA=ON` (по умолчанию ON). Оба переиспользуют
  закреплённую зависимость `aiverse_meshoptimizer_lod`. Интеграция во время выполнения
  использует отдельные цели `aiverse_strata_view`, `aiverse_strata_portable`,
  `aiverse_strata_gpu_scene`, `aiverse_strata_gpu_culling` и `aiverse_strata_render_routing`.
- `build()` принимает **один раздел материала за вызов**; позиции вершин
  начинаются с трёх float и могут быть чередующимися. Исходные индексы
  и позиции заимствуются только на время вызова. При сбое выход остаётся
  неизменным. Результат — ассет во владении CPU, независимый от gfx::Device.
- `Meshlet` — это **лист** с компактными локальными индексами треугольников uint8 и
  отображением на исходные глобальные индексы вершин. По умолчанию: не более 64 вершин,
  не более 124 треугольников. Исходные глобальные индексы сохраняются для индексированного отката.
- `ClusterNode` в текущей схеме — **отрисовываемая группа**; не гарантируется, что
  внутренние узлы помещаются в один аппаратный мешлет. `firstIndex` и
  `indexCount` ссылаются на плоский массив индексов ассета. Дочерние узлы
  идут подряд и перечислены после родителей (переупорядочивание в ширину).
  `Asset::bounds` соответствует `Asset::nodes` один к одному.
- Родительские кластеры используют `meshopt_simplify` (входы только из позиций) или
  `meshopt_simplifyWithAttributes` (необязательные 1..32 взвешенных float-атрибута),
  с `meshopt_SimplifyLockBorder | meshopt_SimplifyErrorAbsolute`.
  Геометрическая ошибка родителя консервативно накапливается из ошибок его
  детей. Группа с сокращением менее 10% остаётся необъединённой; допускается
  несколько корней. Максимум уровней, целевое соотношение и относительная ошибка настраиваются.
  Ошибка в `ClusterNode` задана в единицах объекта; вызывающая сторона должна проецировать её
  в пиксели с правильным проходом и преобразованием.
- `select()` требует, чтобы **все корни были резидентны**. Отсутствующий дочерний узел не даёт
  уточнить родителя; при давлении бюджета сохраняется родитель.
  Вызывающая сторона передаёт корректную спроецированную ошибку для каждого узла. Реализация —
  эталонный селектор на CPU, а не отсечение или резидентность на GPU.
- `buildModelAsset()` проецирует из `Model` 15 компонент атрибутов на вершину
  (нормаль xyz, UV0, касательная xyzw, RGBA вершины, упакованный UV1 карты освещения), с
  взвешенным упрощением с учётом швов. Он не изменяет исходный Model или LOD0.
  Он пропускает изменяемые узлы, скиннинг/морфы, уже загруженные, меши с существующими
  авторскими/сгенерированными LOD, варианты материалов и материалы с альфой/пользовательским шейдером.
  Он возвращает явные причины пропуска и при некорректной подходящей геометрии
  завершается сбоем транзакционно. Эти ассеты потребляет staging в GeometryScene; сам адаптер
  не отправляет отрисовку и не забирает владение у Renderer3D.
- Это по-прежнему экспериментально; ограниченный набор проверенных сценариев перечислен выше.
  Не направляйте сюда смещённые поверхности автоматически.
  Импортёры должны сохранять существующие меши и точный LOD0 как запасной вариант.

## Этап 4 — необязательный CPU-staging в GeometryScene

`GeometryScene` остаётся источником истины для экземпляров сцены, владения
моделями, существующего HLOD, видимости и отправки отрисовки. Новые API:

```cpp
render::GeometryScene scene;
scene.setStrataStagingEnabled(true);  // Before load/loadPrepared, default OFF
scene.loadPrepared(device, sceneJson, std::move(models));
// Scene draws through the existing Renderer3D exactly as before.
const auto& counters = scene.stats(); // strataSources, strataNodes,
// strataMeshlets, strataIndexBytes, strataSkippedSources,
// strataSkippedPrimitives, strataFailures
const auto* cpuAsset = scene.strataAsset("models/stone.glb");
// cpuAsset is nullptr when unavailable or the key is not staged.
scene.shutdown(device); // invalidates cpuAsset and all staging handles
```

Staging компилируется только при `AIVERSE_BUILD_STRATA=ON` и бездействует, пока
перед загрузкой сцены не вызван `setStrataStagingEnabled(true)`.
`strataStagingSupported()` сообщает о доступности на этапе компиляции. Метод
возвращает false в сборках, где Strata отключена. Staging сцены настраивается
через **нативный API**, а не через JSON сцены, поэтому сохранённые сцены не затрагиваются.
Нет дополнительных вызовов отрисовки и нет резидентных на GPU буферов Strata.

CPU-объект `SceneStaging` находится в
`engine/render/StrataModelAdapter.h` и может использоваться инструментами без
создания сцены или рендерера. Он хранит не более одной копии на уникальный ключ источника.
Он сохраняет исходный `Model`, сообщает диагностику пропусков/сбоев и
имеет настраиваемые ограничения по умолчанию в 250k вершин и 750k индексов на источник.
Это **ограничения подготовки на один источник**, а не глобальные лимиты Strata/VRAM.
Все данные staging освобождаются в `GeometryScene::shutdown` и создаются заново
при последующих загрузках. Прокси источников и сгенерированные HLOD-прокси намеренно
исключены. Модели, не подходящие для Strata, продолжают идти через существующий рендерер
без изменений. Неудачная операция staging никогда не должна приводить к сбою legacy-загрузки сцены.

Счётчик `strataIndexBytes` измеряет только вспомогательное плоское хранилище индексов
Strata (а не всё использование кучи CPU Strata); не выдавайте его за резидентную VRAM.
Все счётчики диагностические и не входят в публичную схему сохранения/сети.

**Пока не активно:** буферы сцены на GPU, отсечение мешлетов, резидентность на GPU,
indirect-отрисовка, кодируемая на GPU, программная растеризация, выбор Strata для каждого прохода,
реальная отправка рендеринга Strata или интеграция с рельефом/растительностью.

## Этап 5 — общая основа батчинга на GPU (закреплённая интеграция)

Настоящий двухродительский merge-коммит `0913b82088b63913e1313557de16d85e7c79b89f`
интегрирует закреплённую голову `perf/incremental-static-model-cache`
`1043a9de747fe89cb42de899c15c6d2f4db8e0e1` в функцию Strata.
Ветка/PR #34 батчинга по-прежнему принадлежит отдельно своему агенту.

**Переиспользуемые существующие системы** (не создавайте их заново в Strata):
- `GlobalGeometryPool`, проверки пригодности и ревизии геометрии: общие исходные
  буферы GPU и безопасная инвалидация. Будущие резидентные страницы Strata должны
  согласовывать время жизни аллокатора и дескрипторы буферов с этим пулом.
- `StaticModelCache`, `StaticModelCachePolicy.h` и постоянные пакеты
  рендеринга: существующее переиспользование преобразований/примитивов на отрисовку; никогда не добавляйте
  второй кэш статических преобразований в Strata.
- `StaticCrossModelBatchPolicy` и пути CPU MDI/GPU ICB:
  `Renderer3D` остаётся единственным владельцем группировки по материалам/indirect-
  отправок. Будущий отсекатель Strata передаёт выбранные диапазоны кластеров
  в тот же путь отправки рендеринга, причём выбор источника взаимоисключающий.
- `EcsStaticModelRender.h`: общий адаптер ECS-к-представлению. Компоненты ECS Strata
  не нужны для отрисовки статических экземпляров.

**Текущее поведение не изменилось:** staging Strata в `GeometryScene`
выключен, пока его явно не включат; экспериментальные флаги батчинга CPU/GPU сохраняют
свои значения по умолчанию. Ни отсечение Strata на GPU, ни отрисовка выбранных кластеров,
ни стриминг, ни путь резидентности не активированы и не проверены. PR #35 — это
стековый черновик поверх работы по батчингу, пока PR #34 не смёржен или не заменён.
Агент батчинга может пушить новые коммиты после закреплённого снимка; явно
просматривайте и мёржите эти изменения, прежде чем полагаться на более поздние API.

Не путайте уже существующий путь `GeometryCluster`/статического пулинга
с новой иерархией `ClusterNode` Strata. Staging Strata сейчас хранит
вспомогательные CPU-данные; он НЕ заменяет пулированные страницы GPU и не должен
дважды отправлять примитивы сцены.

## Этап 6 — контракт выбора Strata для каждого прохода

`engine/render/StrataView.h/.cpp` вводит эталонный селектор для каждого прохода,
работающий только на CPU. Выбранная геометрия использует **локальные для Asset диапазоны индексов**; диапазоны
не являются командами отрисовки GPU и НЕ ДОЛЖНЫ передаваться в
`gfx::Device::drawIndexedIndirect` без предварительного размещения вспомогательного
потока индексов через общую резидентность/аллокатор геометрии и правильной
группировки по материалам/экземплярам.

```cpp
#include "engine/render/StrataView.h"
render::strata::ViewConfig pass{};
pass.objectToClip = cameraViewProj * instanceWorld * bindNodeWorld;
pass.width = renderTargetWidth;
pass.height = renderTargetHeight;
pass.clipDepthZeroToOne = device.info().clipDepthZeroToOne;
pass.errorThresholdPixels = 1.0f;
pass.maxSelectedNodes = 65536;
render::strata::ViewPlan selected;
std::string error;
bool ok = scene.planStrataView(
    "models/stone.glb", /* mesh */ 0, /* primitive */ 0,
    pass, selected, &error);
// Existing Renderer3D remains the sole draw issuer regardless of 'ok'.
```

`planStrataView` один раз разрешает уже подготовленный источник и вызывает
независимый `strata::planView(asset, pass, result, error)`.
Вызывающая сторона должна передать правильную матрицу mesh-to-clip **для каждого узла и
прохода**. API не переиспользует молча основной вид для теней,
отражений, порталов или стерео. `strata::planView` проверяет границы,
диапазоны индексов, матрицу и вьюпорт; проецирует существующую геометрическую
ошибку каждого кластера через `geometryErrorPixels`, запускает селектор с откатом
к резидентному родителю, затем консервативно отбрасывает выбранные узлы за экраном
через `geometryBoxVisible`. Ошибки оставляют предыдущий вывод неизменным.
Полученные диапазоны указывают ID узла, firstIndex, indexCount и группу
материала в индексном потоке соответствующего `strata::Asset`; статистика включает
проверенные/отброшенные узлы, треугольники, промахи резидентности и давление бюджета.

Планировщик упакован как `aiverse_strata_view` и использует существующие
вспомогательные функции геометрии `aiverse_models`. Цель CTest —
`aiverse_strata_view_tests` / `strata_view`. Ни выполнение тестов, ни
проверка на Metal/Vulkan не заявлены. Реализация остаётся
эталонным CPU-путём для сравнения с будущим отсечением вычислительными шейдерами;
**буфера сцены на GPU, загрузки команд в RHI или пути отрисовки Strata** пока нет.

## Этап 7A — экспериментальные метаданные кластеров на GPU (не резидентность геометрии)

`engine/render/StrataGpuScene.h/.cpp` предоставляет версионированный буфер хранения
с фиксированной раскладкой для подготовленных на CPU записей кластеров, отображений примитивов и
индексов корней иерархии. Он необязателен, по умолчанию выключен и требует, чтобы
исходный `Model` был загружен в **существующий**
`GlobalGeometryPool`. Он не создаёт страницы VB/IB геометрии и не владеет ими.

CPU-упаковщик `strata::packGpuMetadata(ModelAsset, bytes, error)` чистый,
транзакционный и проверяет уникальность ключей примитивов, все иерархии кластеров,
границы, материалы, локальные для источника диапазоны индексов и смещения u32. Раскладка версии 1:

| Запись метаданных GPU | Размер | Назначение |
| --- | ---: | --- |
| `GpuMetadataHeader` | 32 B | Версия, количества, байтовые смещения |
| `GpuPrimitive` | 32 B | ID меша/примитива Model, глобальные диапазоны узлов/корней |
| `GpuNode` | 64 B | AABB, глобальный ID дочернего узла, диапазон индексов **локальный для источника**, ошибка/флаги |
| ID корней | 4 B каждый | Глобальные ID корней, дополнены до общей длины, кратной 16 байтам |

Компактные массивы хранятся в одном буфере RHI `BufferType::Storage`
на каждый подходящий источник (не на экземпляр). Ограничения по умолчанию: 1 MiB на источник,
8 MiB метаданных на сцену. Экспериментальная загрузка ограничена устройствами Metal
и Vulkan, сообщающими о поддержке вычислений и о соответствующем существующем
`Model::geometryPoolPage`, общих VB/IB и непустых пулированных экстентах.
Неподходящие источники не прерывают загрузку сцены и продолжают обычный рендеринг.
При получении метаданных проверяются `Model::geometryRevision`, идентичность источника и
GPU-дескрипторы источника; устаревшие записи источника никогда не переиспользуются молча.

Нативная интеграция явная:
```cpp
render::GeometryScene scene;
scene.setStrataStagingEnabled(true);
scene.setStrataGpuMetadataEnabled(true); // both OFF by default
scene.loadPrepared(dev, sceneJson, std::move(models));
auto gpuBuffer = scene.strataGpuMetadataBuffer("models/stone.glb");
auto stats = scene.stats();
// strataGpuMetadataSources, strataGpuMetadataDeclined,
// strataGpuMetadataFailures, strataGpuMetadataBytes
scene.shutdown(dev); // call before destroying gfx::Device
```

Буфер метаданных предназначен только для чтения будущим вычислительным кодом; это **не**
список отрисовки сцены на GPU, и ни один вычислительный шейдер его пока не потребляет. `GpuNode::resident`
— снимок CPU-флага кластера, а НЕ доказательство резидентности геометрии
Strata на GPU. Важнее всего, что `GpuNode::sourceFirstIndex` ссылается на
вспомогательный CPU-массив `strata::Asset::indices`, а не на индексный поток `Model::ib` или
какое-либо пулированное размещение на GPU. Пока общий пул не получит безопасных, версионированных
**размещений вспомогательных индексных потоков Strata**, **не выпускайте** индексированные или
indirect-отрисовки по этим смещениям метаданных.

Существующая подготовка `GeometryCluster` может сделать `Model::geometry` ненулевым
и намеренно исключить эту модель из текущего жёсткого общего пула
геометрии. Такой источник откажется от экспериментального staging метаданных на GPU. Разрешите
политику допуска/владения пула явно; не обходите его защитные
проверки. Этот этап оставляет `GlobalGeometryPool.cpp`, активный батчинг `Renderer3D`,
все проходы рендеринга, ECS и форматы сохранённых сцен без изменений.

`aiverse_strata_gpu_scene` собирает слой метаданных и сайдкаров общей страницы; CTest
`strata_gpu_scene` проверяет двоичную раскладку на CPU, глобальные смещения
для нескольких примитивов, флаги источника, некорректные диапазоны и транзакционный сбой.
**Нативная компиляция и выполнение GPU/CTest не проверены**.

## Этап 7B — резидентность вспомогательных индексов Strata в общей странице

**Состояние: код закоммичен; нативная/GPU-проверка НЕ ВЫПОЛНЯЛАСЬ.** Все данные остаются
экспериментальными, а путём отрисовки остаётся существующий `Renderer3D`.

`GlobalGeometryPool.h/.cpp` теперь предлагает следующий явно включаемый API, привязанный к источнику.
Он не вводит второй аллокатор геометрии на GPU:

```cpp
render::GeometryPoolIndexSpan lease;
if (render::uploadAuxiliaryGeometryIndices(
        device, sourceModel, strataIndices.data(), strataIndices.size(), lease)) {
    // lease.buffer == sourceModel.ib == sourceModel.vb (same page).
    // lease.firstIndex is an absolute uint32 GPU index element offset.
    // lease.indexCount counts only uploaded indices in the sidecar stream.
    // Indices remain relative to sourceModel.gpuBaseVertex.
    assert(render::auxiliaryGeometryIndicesValidFor(lease, sourceModel));
}
// Explicitly retire before destroying device / source GPU objects:
render::releaseAuxiliaryGeometryIndices(device, lease);
```

Функция принимает только модели, **уже загруженные в существующий
`GlobalGeometryPool`**, корректные общие дескрипторы VB/IB, полную проверенную
запись в общий буфер и индексы относительно источника, укладывающиеся в зарезервированный
срез вершин. Она никогда не создаёт новую страницу для индексов Strata. Если оставшийся
на странице зазор между вершинами и индексами слишком мал, вызывающая сторона сохраняет существующий
legacy-рендерер. Если запись в RHI не удалась, в аллокаторе ничего не фиксируется;
все прежние байтовые диапазоны моделей и сайдкаров остаются зарезервированными.

Размещения только индексов используют тот же фронт `GeometryPoolPage::indexBegin`,
что и обычные размещения индексов моделей. Аренда сайдкара удерживает свою страницу GPU
через `shared_ptr`, предотвращая уничтожение страницы, даже если исходный
`Model` первым освободит свою аренду. Ревизия источника, базовая вершина,
первый индекс, общая страница и GPU-дескрипторы буферов должны совпадать, чтобы привязкой
можно было пользоваться. `geometryPoolStats` теперь сообщает отдельные *фактические*
`liveModels` и счётчики живых/выведенных/переиспользуемых вспомогательных индексов.

Освобождённые вспомогательные диапазоны можно переиспользовать, только если включён существующий
`AIVERSE_GEOMETRY_POOL_RECYCLE=1` **и** бэкенд
сообщает ненулевой, исправный `completedFrameSerial()`, не меньший записанного
при освобождении `frameSerial()`. Иначе хранилище консервативно остаётся
в карантине/неиспользуемым, пока не будет уничтожена вся страница. Это сделано намеренно:
ни одному CPU-аллокатору не разрешено перезаписывать данные, которые GPU ещё читает. Нет
никаких дополнительных специфичных для Strata барьеров или фоновых потоков.

`GpuMetadataScene::stage()` теперь склеивает `strata::Asset::indices` каждого
подходящего примитива и загружает этот поток в общую страницу его исходной модели
(по умолчанию не более 1 MiB на источник и 8 MiB на сцену). После загрузки
он публикует буфер метаданных кластеров **версии 2**:
`GpuPrimitive::gpuFirstIndex` содержит *абсолютную базу индексов сайдкара на GPU
для этого примитива*; `GpuNode::sourceFirstIndex` остаётся локальным для
примитива, поэтому будущее смещение отрисовки — их сумма. Исходный чистый CPU-
`packGpuMetadata()` намеренно выпускает **версию 1** с
`gpuFirstIndex=invalidNode`; этот формат **не пригоден для отрисовки на GPU**.

```cpp
scene.setStrataStagingEnabled(true);
scene.setStrataGpuMetadataEnabled(true); // both default OFF
scene.loadPrepared(device, json, std::move(models));
GeometryScene::StrataIndexBinding binding;
if (scene.strataGpuIndexBinding("models/stone.glb", binding)) {
    // binding.buffer: same pool as the source Model.
    // binding.firstIndex: base of the entire Strata sidecar.
    // binding.baseVertex: original pooled source vertex base.
    // No Strata draw/dispatch is issued by this API.
}
```

Размещения метаданных и сайдкара разделяют одно время жизни источника и
освобождаются **до того**, как `GeometryScene` освободит свои исходные модели. Геометрия источника
остаётся неизменяемой; существующие материалы, HLOD, батчинг и тени остаются
авторитетными. Пока нет ни отсечения вычислительными шейдерами на GPU, ни Hi-Z, ни генерации indirect-команд,
ни прохода рендеринга Strata, ни стриминга текстур, ни реально измеренного выигрыша в FPS.

`tests/render/strata_gpu_scene.cpp` расширяет фикстуру CPU-упаковки
записывающим RHI и проверяет резервирование в той же странице, байтовую идентичность индексов,
защиту соседних моделей, откат при сбое загрузки, инвалидацию по ревизии,
время жизни источника и сайдкара и поведение при освобождении. Эти сценарии **не
заявлены как проходящие, пока не запущены**. Следующий этап должен проверить отложенное переиспользование
при реальных порядковых номерах бэкенда, собрать все целевые CTest, добавить паритет отрисовки OFF/ON,
а затем реализовать единственного владельца отправки рендеринга, закрытого проверкой возможностей.

## Этап 8 — диагностическое отсечение кластеров на GPU Metal

**Экспериментально, без интеграции в рендеринг и без заявлений о визуальном паритете.**
Библиотека `aiverse_strata_gpu_culling` добавляет вычислительное ядро Metal, которое обходит
иерархию метаданных Strata на GPU версии 2 для каждого статического экземпляра источника
и применяет те же политики спроецированной ошибки и консервативного отсечения по пирамиде, что и
эталонный CPU-`strata::planView`.

Входы — подготовленный буфер хранения метаданных v2, ограниченный список
записей `GpuSceneInstance` на источник (матрицы object-to-world, включающие привязочное
преобразование каждого узла Model, индекс примитива на GPU, флаг включения, диагностический токен)
и `GpuPassConfig` с отдельной матрицей **view-projection**. Не передавайте
на GPU заранее перемноженную матрицу objectToClip и не умножайте экземпляр
дважды. Используйте `strata::cpuReferenceView(pass, instance)`, чтобы получить соответствующий
CPU-`ViewConfig` для каждого экземпляра.

```cpp
render::GeometryScene scene;
scene.setStrataStagingEnabled(true);
scene.setStrataGpuMetadataEnabled(true);
scene.setStrataGpuCullingEnabled(true); // ALL opt-in, default OFF
scene.loadPrepared(device, sceneJson, std::move(models));

// Within the graphics frame and OUTSIDE any render/compute encoder:
render::strata::GpuPassConfig pass;
pass.viewProjection = mainCameraViewProj;
pass.width = viewportWidth;
pass.height = viewportHeight;
pass.clipDepthZeroToOne = device.info().clipDepthZeroToOne;
pass.errorThresholdPixels = 1.f;
pass.maxOutputClusters = 65536;
render::strata::GpuSelectionSubmission result;
bool encoded = scene.dispatchStrataGpuView(
    device, "models/stone.glb", pass, device.frameSerial(), result);
// result.ranges / result.counters are GPU buffers; NOT a draw command list.
```

`GeometryScene::collectStrataGpuInstances(sourceKey, output)` строит
статический вход сцены из её **существующих** преобразований экземпляров и узлов;
он не копирует состояние ECS во второй мир. Токен — локальный
диагностический идентификатор, а не постоянный ID Entity. Более 4096 подготовленных
экземпляров примитивов консервативно отклоняются. У источника должны быть корректные
метаданные Strata v2, обеспеченные той же арендой индексов `GlobalGeometryPool`.

`GpuClusterCulling::dispatch()` использует Metal `gfx::Device::createCompute()`
и слоты 0/1/2/3 для метаданных, экземпляров, выбранных кластеров и атомарных
счётчиков, с байтами uniform прохода в слоте 8. Каждый поток GPU обходит иерархию
кластеров одного экземпляра примитива, уточняя её только тогда, когда все дочерние
кластеры резидентны. Локальный стек обхода на 128 элементов и ограниченный
вывод помечают некорректную/чрезмерную работу; **результаты overflow/invalid никогда не должны
использоваться для отрисовки**. Шейдер выпускает записи `GpuSelectedCluster` с
токеном экземпляра, абсолютным смещением индекса в пуле GPU, количеством, ID материала,
глобальным ID узла и ID примитива. Порядок атомарного уплотнения не определён,
поэтому будущие сравнения GPU/CPU должны сортировать по стабильным ключам экземпляра/узла.

Этот этап НЕ выдаёт в обычном цикле рендеринга ни `drawIndexed`, ни indexed indirect-
команд, сформированных на GPU, ни GPU ICB, ни чтения данных. Существующий
`Renderer3D` остаётся единственным владельцем отрисовки и продолжает рисовать всю исходную
геометрию, даже когда диагностический запуск успешен. Не добавлено ни Hi-Z, ни истории
окклюзии, ни mesh-шейдинга, ни стриминга, ни вычислительного пути Vulkan.

Безопасность кадров: три слота кадров, до 64 независимых отправок на
слот кадра, свежие буферы хранения на каждую отправку в том же кадре. Переиспользуемый
слот отклоняется, если бэкенд не подтвердил завершение его старого кадра
(`completedFrameSerial >= previousFrameSerial`); отсутствующий/неизвестный
порядковый номер завершения приводит к безопасному отказу, а не к перезаписи данных в работе.
Вызывайте `GpuClusterCulling::shutdown(device)` или
`GeometryScene::shutdown(device)` (если владеет сцена) до уничтожения
устройства. Шлюз возможностей только для Metal отклоняет другие бэкенды; legacy-
рендерер не затрагивается.

Целевые тесты:
`aiverse_strata_gpu_culling_tests` (`strata_gpu_culling`) использует проверки
записывающего RHI для композиции преобразований эталона CPU, отключённых бэкендов,
настройки запуска вычислений Metal, независимых отправок в том же кадре, вывода слотов
кадров из оборота и нулевого числа вызовов отрисовки. Это **не** компилирует шейдер через
Metal и не подтверждает, что выборки GPU равны выборкам CPU. Целевой
workflow `.github/workflows/strata-focused.yml` настраивает и запускает все
пять CPU/mock-целей контрактов Strata на Linux и macOS. Считайте поставленные в очередь,
отменённые или недоступные Actions **НЕ ПРОВЕРЕННЫМИ**.

Прежде чем включать отрисовку: нативная компиляция + целевые тесты, реальная компиляция
шейдера Metal, асинхронное чтение счётчиков/результатов GPU в тестовом стенде,
сравнение с CPU-`planView` на случайных проекциях, переходы LOD/отката,
тени, меши/материалы и несколько экземпляров. Затем доказать
исключительное владение отрисовкой и сравнить пиксели legacy и Strata на реальном GPU,
прежде чем даже рассматривать использование уплотнённых диапазонов в indirect-пути.

## Этап 8.2 — асинхронное чтение, CPU-оракул и запуск реального паритета на Metal

Владелец запустил **предыдущий код этапа 8** на Mac Studio 2026-11-19.
`aiverse_strata_gpu_culling_tests` собрался, и все пять целевых CTest
(`strata_builder`, `strata_model_adapter`, `strata_view`,
`strata_gpu_scene`, `strata_gpu_culling`) **прошли (5/5, 1.19 s)**.
Это доказывает прежние нативные C++-тесты, но **не** реальное выполнение шейдера Metal
и не новый код этапа 8.2.

Этап 8.2 добавляет ограниченный асинхронный диагностический API:

```cpp
render::strata::GpuSelectionSubmission selected;
if (scene.dispatchStrataGpuView(device, sourceKey, pass,
                                device.frameSerial(), selected)) {
    scene.requestStrataGpuReadback(device, selected,
        [&](render::strata::GpuSelectionReadback readback) {
            // Device owner thread at a subsequent beginFrame.
            // Incomplete/overflow/invalid results may NEVER be drawn.
            if (!readback.valid()) return;
            // Verify against CPU planView before considering rendering.
        });
}
```

`GpuClusterCulling::requestReadback` отправляет два независимых запроса RHI
`Device::readBufferAsync` (счётчики GPU и выбранные диапазоны).
Существующий бэкенд Metal ограничивает каждый запрос 64 KiB, поэтому запросы
с `GpuSelectionSubmission::capacity > 2048` отклоняются, а не
публикуют внешне полный префикс. Обратные вызовы выполняются позже в
**потоке-владельце устройства**, а не синхронно с запуском на GPU.
Результат — `GpuSelectionReadback`, содержащий счётчики и статус
(`Valid`, `Overflow`, `Invalid`, `ReadbackFailed`). Он никогда не размещает
новую геометрию на GPU и не отправляет команды отрисовки. Не храните в обратных вызовах чтения
указатели на выгруженную `GeometryScene`; управляйте временем жизни наблюдателя
явно. Если планирование чтения отклонено, обратный вызов сообщает о сбое.

`strata::compareGpuSelection(source, instances, pass, pooledFirstIndex,
readback, error)` — оракул CPU/GPU. Он независимо проецирует каждый
экземпляр через `cpuReferenceView()` и `planView()`, переводит каждый локальный
диапазон в абсолютные адреса индексов/узлов общего пула, сортирует записи CPU/GPU
по их стабильным полям и требует точного равенства. Он отклоняет
некорректные экземпляры, дублирующиеся/нулевые включённые токены, переполнение вывода и
статус GPU invalid. Порядок атомарных операций GPU явно не имеет значения.
pooledFirstIndex должен быть исходной базой привязки сайдкара v2 для того же
источника/ревизии. Это сравнение **диагностическое** и никогда не является основанием
для отправки отрисовки в production.

Добавлен отдельный исполняемый файл для **реального GPU Metal**:
`tests/render/strata_metal_parity.cpp`, закрытый опцией CMake
`AIVERSE_STRATA_METAL_GPU_TESTS=ON` (по умолчанию OFF, только macOS).
Он использует реальные `platform::run` + CAMetalLayer, фактическую
загрузку источника в `GlobalGeometryPool`, метаданные Strata v2, диагностические вычисления
и асинхронное чтение с GPU. Он выполняет пять случаев: детальный LOD с
двумя экземплярами (четыре листа), грубый LOD (два родителя), отсечение по пирамиде,
отключённый экземпляр и намеренное переполнение вывода. Он выводит в консоль
результат каждого сравнения GPU/CPU и завершается с ненулевым кодом при
сбое. Он локально для фикстуры включает только старый **допуск общего пула на CPU**
(`AIVERSE_METAL_CPU_MDI=1`); он не отправляет
ICB или отрисовку Strata.

Команды для Mac Studio:

```sh
cd ~/Projects/AIVERSE-strata-stage8
git status --short
git fetch origin feature/strata-virtual-geometry
git switch --detach origin/feature/strata-virtual-geometry
cmake -S . -B build/strata \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_STRATA=ON \
  -DAIVERSE_STRATA_METAL_GPU_TESTS=ON
cmake --build build/strata \
  --target aiverse_strata_gpu_culling_tests aiverse_strata_metal_parity \
  --parallel 8
ctest --test-dir build/strata \
  -R '^strata_(builder|model_adapter|view|gpu_scene|gpu_culling)
- **One scene/instance truth**: `GeometryScene` owns static render instances
  and HLOD; do not create separate Strata transforms or introduce gameplay
  entities solely for rendering.
- **One grouping authority**: indirect draw grouping/material binding stays
  in the existing renderer, with a future Strata adapter supplying visible
  cluster ranges. Never double-submit the same primitives.
- **GPU capability negotiation**: GLES3/WebGL2 should retain traditional
  indexing paths. Metal/Vulkan/D3D future paths must gate compute, indirect
  count, storage resources and synchronization independently.
- **Multi-world**: geometry assets can be shared by handle across worlds,
  while scene-resident instance tables and render budgets are per world/view.
- **Dynamic terrain**: TerraWeave owns chunk geometry and versions; Strata
  consumes immutable snapshots and schedules deferred release. PHYSIS owns
  simulation/events, not GPU buffers.
- **Pass separation**: main-camera, shadows, reflections and portals may
  require distinct culling/error metrics and shadow caster inclusion.

## Known shortcomings / next steps

1. Compile and execute focused tests on a native project checkout:
   ```sh
   cmake -S . -B build/strata -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON
   cmake --build build/strata --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests aiverse_strata_view_tests aiverse_strata_gpu_scene_tests aiverse_strata_gpu_culling_tests -j 8
   ctest --test-dir build/strata -R '^strata_(builder|model_adapter|view|gpu_scene) --output-on-failure
   ```
2. Добавить сценовую фикстуру на записывающем устройстве, сравнивающую staging Strata OFF/ON,
   дедупликацию экземпляров, паритет отрисовки материалов/LOD/теней и выгрузку/повторную загрузку.
3. Проверить качество силуэтов и швов для разных глубин LOD и разрывов
   атрибутов; одни лишь упрощение родителей и зафиксированные границы не
   гарантируют переходов без трещин.
4. Существующий `GlobalGeometryPool` теперь резервирует вспомогательные диапазоны индексов
   Strata без создания новой страницы, а метаданные версии 2 отображают базы
   примитивов на их фактические смещения в пуле GPU. Проверьте на Mac Metal и Vulkan:
   записи в общий буфер, повторную загрузку/инвалидацию по ревизии, вывод из оборота данных в работе,
   окружающие жёсткие модели и все исходные пути отката, прежде чем включать
   отправку. Staging по умолчанию остаётся OFF.
5. Этап 9 добавляет явную исключительную предварительную проверку маршрута/команд кандидата.
   Выполните запуск реального паритета Metal этапа 8.2 и шестой тест маршрута,
   затем варианты со случайными камерами/LOD/материалами, прежде чем принимать вывод GPU;
   прежние mock/CPU-тесты не устанавливают паритет растра.
6. Добавлять взаимоисключающую отправку выбранных диапазонов в Renderer3D только после
   паритета выбора GPU/CPU И паритета снимков экрана. Сохранять legacy-откат на всех
   неподдерживаемых устройствах; затем заняться Hi-Z, стримингом и независимыми CPU/GPU-
   бенчмарками.

## Политика проверки

О компиляторе, CTest, снимках экрана, таймингах GPU и бенчмарках можно сообщать только
после **реального выполнения** с точными логами или URL CI. Запуски в очереди/отменённые
не являются успешными результатами.
 --output-on-failure
   ```
2. Add a recording-device scene fixture comparing Strata staging OFF/ON,
   instance deduplication, material/LOD/shadow draw parity and unload/reload.
3. Validate silhouette and seam quality for different LOD depths and attribute
   discontinuities; parent simplification and locked borders alone do not
   guarantee crack-free transitions.
4. Extend **existing** `GlobalGeometryPool` to safely allocate an auxiliary
   Strata index stream on an existing pooled page, with explicit scene/
   model revision checks, upload failure rollback, frame-fenced retirement
   and shared-buffer synchronization. Do not add a second geometry VB/IB pool.
   All current Strata metadata offsets remain source-local, not drawable.
5. Build a capability-gated GPU scene/cluster selection path using the
   CPU planner as a correctness oracle. Submit visible ranges through one
   existing `Renderer3D` batching/MDI owner, or fall back to normal models.
6. Benchmark CPU and GPU time separately, validate view-dependent shadows,
   reflection views and multi-world lifetime, then pursue streaming.

## Verification policy

Compiler, CTest, screenshots, GPU timing and benchmarks must be reported only
after **actual execution** with exact logs or CI URLs. Queued/cancelled runs
are not passing results.
 \
  --output-on-failure
build/strata/aiverse_strata_metal_parity
```

**Различие в проверке:** предыдущие пять CPU/mock-тестов CTest были
подтверждены владельцем как PASS. Новые изменения этапа 8.2, реальная компиляция шейдера MSL,
запуск на GPU, асинхронное чтение, паритет выбора Metal GPU и CPU и
паритет изображений на момент этой записи документации **НЕ ВЫПОЛНЯЛИСЬ**. Реальный
запуск намеренно не является автономным CTest; он создаёт окно macOS.
Не делайте вывода об ускорении или полном рендеринге из корректной диагностической выборки.

Оставшийся обязательный шлюз перед включением отправки отрисовки:
нативные тесты этапа 8.2, настоящая компиляция шейдера Metal, обработка несовпадений/переполнения,
пороги камеры и качество геометрии, явный паритет GPU/CPU
на случайных преобразованиях и материалах, тени/отражения для каждого прохода
и интеграция в Renderer3D с одним владельцем без двойной отправки. Вычисления на Vulkan,
Hi-Z и стриминг остаются будущей работой.

## Этап 9 — исключительная предварительная проверка маршрутизации рендерера (без отрисовки на GPU)

После этапа 8.2 PR #34 по батчингу был смёржен в `main` в
`58e29ab052458fffcdebebffe27022b53adb9367`. Strata синхронизирует
этот `main` через двухродительский merge `a77cd830e8dee751658028786066111bc8e65f6a`,
сохраняя независимые изменения батчинга и передачу дел.
Renderer3D, StaticModelCache, GlobalGeometryPool и CPU MDI/GPU ICB по-прежнему
принадлежат существующей реализации батчинга.

`engine/render/StrataRenderRouting.h/.cpp` определяет явную **предварительную проверку
кандидата и решение об исключительном маршруте источника** (а не живой путь отправки).

```cpp
using namespace aiverse::render;
strata::RenderCandidate candidate;
strata::ViewConfig view; // pass object-to-clip, viewport & error budget
// Scene must have been staged with both CPU and pooled-GPU Strata enabled.
const bool prepared = scene.prepareStrataRenderCandidate(
    "models/stone.glb", 0, 0, strata::RenderPass::MainOpaque,
    view, candidate);

// Renderer3D owns final material/pipeline validation and the real uniform slot.
strata::RenderRouteGate gate;
gate.requested = true;
// DO NOT set proof flags speculatively: real Metal CPU/GPU and raster parity
// must be established first, for the applicable pass/material.
gate.sourceAndMaterialSafe = prepared;
gate.sourceResident = prepared;
gate.cpuGpuSelectionParityVerified = false; // unverified in production
gate.rasterParityVerified = false; // unverified in production
gate.rendererOwnsSubmission = false;
auto route = strata::chooseExclusiveRenderRoute(gate);
// route == Legacy; original Renderer3D draws stay authoritative.
```

CPU-предпроверка проверяет точную исходную модель, примитив и материал,
проверяет **свежую** аренду сайдкара общего пула `GpuMetadataScene::indexBinding`,
живые GPU-дескрипторы источника/модели, ревизии геометрии и материала,
отсутствие анимации/скиннинга/морфов/смещения вершин/пользовательского шейдера, отсутствие альфа-
смешивания, существующего LOD модели, владения пакетом теней, пакетом материалов или
вариантом. Она вызывает CPU-`planView` с правильной проекцией прохода и
переводит выбранные локальные для примитива индексы в абсолютные смещения индексов
общего GPU. Для полностью находящихся за экраном примитивов она может вернуть пустого
кандидата; это корректное отсечение для конкретного прохода, **а не** указание
дополнительно нарисовать исходный примитив.

`encodeRendererCommands(liveModel, liveResidency, candidate, slot, gate,
commands)` переводит диапазоны геометрии в общий 20-байтный ABI
`gfx::DrawIndexedIndirectCommand` **только если** каждый шлюз маршрута
явно удовлетворён. Он независимо повторно проверяет живые ревизии материала/геометрии,
идентичность указателя источника, буферы GPU и *текущую* аренду сайдкара
(включая границы). Реальный слот uniform `firstInstance` должен
предоставлять Renderer3D; Strata никогда не размещает вторую таблицу преобразований,
конвейер или источник группировки материалов. При сбое вывод остаётся
нетронутым. Это по-прежнему диагностическое построение команд, а НЕ их запуск.

**Важно:** простая установка всех булевых полей `RenderRouteGate` не
составляет настоящего паритета GPU или растра. Production-рендерер сейчас
не вызывает этот кодировщик и не подавляет legacy-отрисовку. Не подключайте
ветку только для Strata, пока не пройдут запуск реального Metal этапа 8.2 и полный визуальный
паритет OFF/ON, включая солнечные тени, отражения, переходы материалов/LOD
и несколько экземпляров. Никогда не отправляйте кандидата Strata
вдобавок к соответствующему `ModelDraw`.

Новая статическая цель `aiverse_strata_render_routing` и целевой CTest
`strata_render_routing` покрывают шлюз, перевод пулированных индексов,
выбор родителя/листа для каждого прохода, полностью отсечённые примитивы, изменение живого
источника, отозванные/некорректные аренды GPU и отсутствие реальной растровой/
indirect-отрисовки. Тесты не считаются проходящими, пока не выполнены.
Workflow `.github/workflows/strata-focused.yml` собирает этот шестой
набор контрактов на Linux/macOS.

## Этап 10 — реальный пробный прогон ModelDraw в Renderer3D (без изменений рендеринга)

**В Renderer3D теперь есть диагностический вызов предпросмотра, а не путь отрисовки Strata.**
Одна лишь предварительная проверка маршрутизации источника этапа 9 не знала о фактическом
оттенке, позе, преобразованиях узлов отдельной отрисовки и независимых проходах рендерера.
Новый `StrataRendererPreview.h/.cpp` добавляет не имеющий состояния, тестируемый
вычислитель `strata::previewModelDraw`, который принимает существующий
`ModelDraw`, `ModelAsset` источника и резидентность сайдкара на GPU,
а также `PreviewContext` с view-projection основной камеры/солнца и
размерами цели. Он никогда не создаёт буферы команд, не вызывает
`drawIndexed`, не меняет ModelDraw и не кэширует владение преобразованиями.

`Renderer3D::previewStrataModel(draw, stagedAsset, gpuScene, sourceKey)`
запускает тот же вычислитель с **реальными** текущими матрицами рендерера,
качеством солнечных теней, размером вьюпорта и соглашением о глубине.
`Renderer3D::strataPreviewStats()` предоставляет счётчики на каждый `beginScene`:
modelDraws, attempted, ready, rejected, selectedClusters, fullyCulled
и skippedUnsafeDraws. Кандидат «ready» означает лишь, что эталонная CPU-
геометрия и пулированные индексы прошли предпроверку, — он
**не** означает, что паритет выбора GPU/CPU или изображений пройден.

`GeometryScene` владеет единственным текущим вызовом интеграции:
```cpp
scene.setStrataStagingEnabled(true);
scene.setStrataGpuMetadataEnabled(true);
scene.setStrataRenderPreviewEnabled(true); // additional opt-in, default OFF
scene.loadPrepared(device, json, std::move(models));
// In an existing renderer frame:
scene.draw(renderer);
// Every source still goes through original Renderer3D::drawModel().
auto diagnostics = renderer.strataPreviewStats();
```

Предпросматриваются только исходные модели (не HLOD и не замены прокси поверхностей),
и только когда источник уже резидентен на GPU в
исходном `GlobalGeometryPool`, а его CPU-данные Strata подготовлены.
Не вводятся ни новые данные ECS, ни кэш материалов рендерера, ни пул GPU, ни альтернативный путь
отрисовки. Временный sourceKey и пулированные данные заимствуются
только синхронно на время этой оценки и никогда не сохраняются в пакетах
Renderer3D. Предпросмотр отказывается от анимированной позы/запечённого скиннинга, переопределений материалов и
узлов, альфы/затухания, вариантов модели, проявления (reveal), проходов воды и
несовместимых живых шейдеров/материалов/геометрии. Каждый подходящий узел источника
оценивается отдельно с правильным привязочным преобразованием узла и
проекцией прохода. Выбор для солнечной тени не зависит от основной
камеры, а пустая успешная выборка считается полностью отсечённым
предпросмотром, а не приглашением отрисовать второй источник.

**Без дублирующихся вызовов отрисовки:** после необязательного предпросмотра
`GeometryScene::drawInstance` ВСЕГДА вызывает исходный
`renderer.drawModel(d)` ровно один раз. Замены HLOD/прокси никогда
не попадают в пробный прогон. `RenderRouteGate` этапа 9 остаётся контрактом доказательства
и не подключён к живому пути отправки, даже если вызывающая сторона
вручную включит флаг предпросмотра.

`tests/render/strata_render_routing.cpp` дополнительно проверяет
исходный ModelDraw с независимыми видами основной камеры/тени и записывающим
RHI, чтобы убедиться в правильных счётчиках предпроверки, ограничениях opaque/alpha
и **нулевом числе отправок отрисовки**. Шесть целевых CTest Strata по-прежнему
нужно запускать на нативной сборке; этот ассистент этап 10 не выполнял.
Ранее полученный пользователем результат 5/5 CTest PASS этапа 8 на Mac Studio остаётся
подтверждённым **более старым** снимком кода, а не проверкой этапа 10.

## Этап 10: проверенный выбор на Metal (Mac Studio владельца, 2026-11-19)

Нативный целевой набор на macOS: **6/6 CTest PASS**, 0 сбоев, включая
`strata_gpu_culling` и `strata_render_routing`.
Владелец также скомпилировал и запустил реальный исполняемый файл `aiverse_strata_metal_parity`
на Mac Studio. Реальный GPU создал вычислительный конвейер Metal Strata,
запустил его, асинхронно скопировал видимые кластеры и
сравнил их с CPU-оракулом. Все пять предоставленных случаев фикстуры прошли:
детальный LOD (выбрано 4, осмотрено 6), грубый LOD (2/2), отсечение по пирамиде
(0/6), отключённый экземпляр (2/3), а переполнение ограниченного вывода корректно
отклонено. Итоговый вывод: `[strata-metal] cases=5/5 status=PASSED`.

SHA проверенного checkout не был включён в предоставленный вывод терминала.
Эти результаты **пока не** устанавливают паритет снимков экрана legacy- и
Strata-геометрии на растре; они не включают отрисовку Strata в Renderer3D.
Production по-прежнему рисует ровно одну legacy-модель один раз, а предпросмотр
этапа 10 остаётся включаемым явно. Следующий обязательный шлюз — реальный паритет растра/изображений
на Metal с исходной геометрией и проходами солнечной тени/отражений, без
дублирующейся отправки примитивов и с консервативным откатом.

Отдельный полный прогон CTest `AIVERSE-static-scene` показал 125/127 PASS;
два его сбоя регистрации навыков не связаны с этой GPU-фикстурой
Strata, и их не следует смешивать с 6/6 целевыми тестами Strata.

## Этап 11 — фикстура A/B реального растра Metal, отдельные кадры (не production)

`tests/render/strata_metal_raster_parity.cpp` — это **включаемый явно
зонд паритета растра Metal на macOS**, собираемый целью
`aiverse_strata_metal_raster_parity` при
`AIVERSE_STRATA_METAL_GPU_TESTS=ON`. Цель намеренно не является
автономным CTest, потому что открывает реальный CAMetalLayer.

Фикстура создаёт один простой жёсткий непрозрачный квад с UV-развёрткой и
подготавливает источник Strata v2, родитель которого использует те же исходные два
треугольника, что и оба резидентных потомка вместе. Она загружает источник
только один раз через существующий `GlobalGeometryPool`, затем размещает
вспомогательные индексы Strata **внутри того же пулированного буфера GPU**.
Две модели-псевдонима только для теста заимствуют именно этот вершинный/индексный буфер GPU
с `gpuBaseVertex` из исходного источника и `gpuFirstIndex`
из подготовленного сайдкара; ни одна не загружается и не освобождается отдельно.
Примитивы псевдонимов ссылаются на выбранные CPU-`prepareRenderCandidate`
диапазоны, а не на второе размещение вершин.

С помощью production-`Renderer3D::drawModel` каждый тестовый кадр
отправляет не более ОДНОГО из вариантов: ничего, источник Legacy, детальные потомки Strata
или грубый родитель Strata. Пол служит приёмником теней.
Обычный рендерер обрабатывает свои основной проход и проход солнечной тени; ни одна
модель не рисуется через второй конкурирующий растровый конвейер.
Отдельные кадры захватываются существующим в Metal
`gfx::Device::captureNextFrame`. Тест требует различия изображений empty и Legacy
(защита от чёрных/пустых изображений), а затем побайтового совпадения снимков
Legacy и fine, Legacy и coarse. Эта **точная**
фикстура намеренно использует неупрощённую геометрию родителя; это не
допуск на трещины/швы или перцептивную ошибку для грубых LOD реального мира.

По умолчанию запуск записывает в рабочий каталог четыре двоичных снимка PPM
(`strata-raster-empty.ppm`,
`strata-raster-legacy.ppm`,
`strata-raster-strata-fine.ppm`,
`strata-raster-strata-coarse.ppm`). Задайте
`AIVERSE_STRATA_RASTER_CAPTURE_PREFIX=/path/to/prefix`, чтобы выбрать
место. Он проверяет одну отправку ModelDraw на каждый кадр кандидата,
ненулевые счётчики отправок основного прохода и солнечной тени и
нулевое число изменённых каналов в сравниваемых снимках экрана.
Он ПОКА НЕ читает текстуру карты теней напрямую, не проверяет
плоское отражение воды и не использует автоматически буфер выбранных диапазонов
вычислений на GPU как авторитетный вход отрисовки. Это остаётся
отдельными приёмочными тестами.

Ручная проверка на Mac Studio:
```sh
cd ~/Projects/AIVERSE-strata-validation
git status --short
git fetch origin feature/strata-virtual-geometry
# If clean (or after preserving local modifications):
git switch --detach origin/feature/strata-virtual-geometry
cmake -S . -B build/strata -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_STRATA=ON -DAIVERSE_STRATA_METAL_GPU_TESTS=ON
cmake --build build/strata --target \
  aiverse_strata_metal_parity aiverse_strata_metal_raster_parity \
  --parallel 8
ctest --test-dir build/strata \
  -R '^strata_(builder|model_adapter|view|gpu_scene|gpu_culling|render_routing)
- **One scene/instance truth**: `GeometryScene` owns static render instances
  and HLOD; do not create separate Strata transforms or introduce gameplay
  entities solely for rendering.
- **One grouping authority**: indirect draw grouping/material binding stays
  in the existing renderer, with a future Strata adapter supplying visible
  cluster ranges. Never double-submit the same primitives.
- **GPU capability negotiation**: GLES3/WebGL2 should retain traditional
  indexing paths. Metal/Vulkan/D3D future paths must gate compute, indirect
  count, storage resources and synchronization independently.
- **Multi-world**: geometry assets can be shared by handle across worlds,
  while scene-resident instance tables and render budgets are per world/view.
- **Dynamic terrain**: TerraWeave owns chunk geometry and versions; Strata
  consumes immutable snapshots and schedules deferred release. PHYSIS owns
  simulation/events, not GPU buffers.
- **Pass separation**: main-camera, shadows, reflections and portals may
  require distinct culling/error metrics and shadow caster inclusion.

## Known shortcomings / next steps

1. Compile and execute focused tests on a native project checkout:
   ```sh
   cmake -S . -B build/strata -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON
   cmake --build build/strata --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests aiverse_strata_view_tests aiverse_strata_gpu_scene_tests aiverse_strata_gpu_culling_tests -j 8
   ctest --test-dir build/strata -R '^strata_(builder|model_adapter|view|gpu_scene) --output-on-failure
   ```
2. Добавить сценовую фикстуру на записывающем устройстве, сравнивающую staging Strata OFF/ON,
   дедупликацию экземпляров, паритет отрисовки материалов/LOD/теней и выгрузку/повторную загрузку.
3. Проверить качество силуэтов и швов для разных глубин LOD и разрывов
   атрибутов; одни лишь упрощение родителей и зафиксированные границы не
   гарантируют переходов без трещин.
4. Существующий `GlobalGeometryPool` теперь резервирует вспомогательные диапазоны индексов
   Strata без создания новой страницы, а метаданные версии 2 отображают базы
   примитивов на их фактические смещения в пуле GPU. Проверьте на Mac Metal и Vulkan:
   записи в общий буфер, повторную загрузку/инвалидацию по ревизии, вывод из оборота данных в работе,
   окружающие жёсткие модели и все исходные пути отката, прежде чем включать
   отправку. Staging по умолчанию остаётся OFF.
5. Экспериментальное отсечение вычислительными шейдерами Metal теперь выпускает диагностические
   диапазоны, выбранные на GPU, но не рендерит их. Выполните нативную компиляцию Metal, чтение с GPU
   и паритет с отсортированным CPU-`planView`, включая переполнение стека/вывода и
   соглашение о глубине от нуля до единицы, прежде чем подключать что-либо к рендереру.
6. Добавлять взаимоисключающую отправку выбранных диапазонов в Renderer3D только после
   паритета выбора GPU/CPU И паритета снимков экрана. Сохранять legacy-откат на всех
   неподдерживаемых устройствах; затем заняться Hi-Z, стримингом и независимыми CPU/GPU-
   бенчмарками.

## Политика проверки

О компиляторе, CTest, снимках экрана, таймингах GPU и бенчмарках можно сообщать только
после **реального выполнения** с точными логами или URL CI. Запуски в очереди/отменённые
не являются успешными результатами.
 --output-on-failure
   ```
2. Add a recording-device scene fixture comparing Strata staging OFF/ON,
   instance deduplication, material/LOD/shadow draw parity and unload/reload.
3. Validate silhouette and seam quality for different LOD depths and attribute
   discontinuities; parent simplification and locked borders alone do not
   guarantee crack-free transitions.
4. Extend **existing** `GlobalGeometryPool` to safely allocate an auxiliary
   Strata index stream on an existing pooled page, with explicit scene/
   model revision checks, upload failure rollback, frame-fenced retirement
   and shared-buffer synchronization. Do not add a second geometry VB/IB pool.
   All current Strata metadata offsets remain source-local, not drawable.
5. Build a capability-gated GPU scene/cluster selection path using the
   CPU planner as a correctness oracle. Submit visible ranges through one
   existing `Renderer3D` batching/MDI owner, or fall back to normal models.
6. Benchmark CPU and GPU time separately, validate view-dependent shadows,
   reflection views and multi-world lifetime, then pursue streaming.

## Verification policy

Compiler, CTest, screenshots, GPU timing and benchmarks must be reported only
after **actual execution** with exact logs or CI URLs. Queued/cancelled runs
are not passing results.
 \
  --output-on-failure
./build/strata/aiverse_strata_metal_parity
./build/strata/aiverse_strata_metal_raster_parity
```

Ожидаемый, **но ПОКА НЕ наблюдавшийся** результат нового растрового зонда:
`[strata-raster] PASS: real Renderer3D legacy vs fine/coarse source-page pixel parity`,
за которым следует `[strata-raster] status=PASSED`.
Выход с ошибкой или любое различие изображений означает, что паритет растра остаётся непроверенным.
Предыдущие **реальные** результаты паритета вычислений на GPU Metal 5/5 и целевых CPU-
CTest 6/6 записаны отдельно, и их нельзя выдавать за
доказательство прохождения паритета растра этапа 11.

`.github/workflows/strata-focused.yml` теперь использует исправленное
регулярное выражение CTest на шесть тестов и дополнительно **компилирует** обе нативные Metal-
фикстуры в macOS CI. GUI-тесты там **не выполняются**,
поэтому одна лишь зелёная задача компиляции не доказывает паритета растра на GPU.
Держите PR функции в статусе Draft, а production-рендерер — без изменений, пока
точный визуальный паритет и паритет теней/отражений не будут продемонстрированы на оборудовании.

## Этап 11.1 — исправление синхронизации захвата растра Metal (2026-11-19)

Владелец скомпилировал реальную фикстуру Metal `aiverse_strata_metal_raster_parity`
и запустил её на Mac Studio. Предыдущий паритет вычислений кластеров на GPU
**остаётся PASS (5/5)**, но растровая фикстура сообщила:

```text
[strata-raster] FAIL: Legacy and empty screenshots identical; test drew no model
[strata-raster] status=FAILED frames=45
[metal-assets] ... pipelines=16 pipelineFailures=0
```

Это **не** доказывает ошибку в геометрии вершин/индексов. Изучение
`GfxMetal.mm` выявило дефект планирования теста: когда текущий
drawable имеет `framebufferOnly`, `captureNextFrame` устанавливает
`captureDelay_=1`, и запрошенное изображение копируется в *более позднем*
кадре. Фикстура запрашивала захваты на кадрах 8, 19, 30 и
41, ровно перед переходами на 9, 20, 31 и 42. Поэтому
запрос «Empty» мог захватить следующую за ним геометрию Legacy, запрос
«Legacy» — следующую геометрию Fine/Strata и т. д. Одинаковые
снимки «Empty/Legacy» поэтому могут быть ложной атрибуцией,
даже когда корректная геометрия была отрисована. Не ослабляйте независимое
требование пустого эталона, чтобы скрыть этот сбой теста.

Исправление `bcc471e93c9a4a35f7fda457d4fa15ccbda13fe9` в
`tests/render/strata_metal_raster_parity.cpp`:

- Явно использовать `Device::setScreenReadable(true)` при инициализации
  фикстуры, до начала отрисовки платформой, чтобы текстуры drawable
  можно было захватывать в нужном кадре. Сбрасывать флаг
  перед завершением.
- Запрашивать каждое изображение в середине его устойчивого тестового режима:
  **кадр 4 Empty, 15 Legacy, 26 Strata Fine, 37 Strata Coarse**.
  Эти слоты остаются безопасными, даже если поддерживаемый RHI добавит до двух
  кадров задержки захвата.
- Выводить запрошенный режим/кадр и фактический кадр обратного вызова, со
  счётчиками основного прохода/прохода теней. Если Empty и Legacy остаются одинаковыми,
  выводить счётчики захваченного режима перед сбоем, чтобы отличить невидимость
  модели от ошибок фазы снимка экрана.
- Сохранить строгие побайтовые проверки снимков GPU `Empty != Legacy`, `Legacy == Fine` и
  `Legacy == Coarse`. Не
  изменять production-рендерер, конвейеры теней, сборщик ассетов,
  глобальный пул источников GPU или индексированный сайдкар.

**Проверка:** это исправление закоммичено в GitHub, но
**ассистент его ПОКА НЕ компилировал и не запускал**. Результат растра этапа 11
владельца до исправления — FAIL, а не PASS. Подтверждённые владельцем
шесть целевых CPU/mock-тестов и пять сценариев паритета реальных вычислений Metal
независимо остаются PASS на своём ранее проверенном снимке.
Реальный паритет снимков основного прохода/теней остаётся непроверенным до
успешного запуска на Mac после исправления; одна лишь сборка CMake или CI в очереди не являются
доказательством визуальной корректности.

Из существующего рабочего дерева, убедившись, что локальные изменения
сохранены:

```sh
cd ~/Projects/AIVERSE-strata-validation
git status --short
git fetch origin feature/strata-virtual-geometry
git switch --detach origin/feature/strata-virtual-geometry
cmake --build build/strata --target aiverse_strata_metal_raster_parity --parallel 8
./build/strata/aiverse_strata_metal_raster_parity
```

Если исправленный эталон всё равно не проходит, изучите новые
индексы кадров `capture requested` / `capture received`,
счётчики `shadow/opaque` и четыре сохранённых файла PPM. Подтвердите
фактическую видимость/отсечение модели и корректные базовые смещения пулированного источника,
прежде чем менять геометрию Strata или ослаблять паритет снимков экрана.
Будущему реальному паритету растра всё ещё нужны отдельные тесты чтения глубины
карты теней, вида отражения воды, нескольких материалов и бюджета ошибки упрощённых
LOD перед маршрутизацией отрисовки в production.

## Архитектурные ограничения
- **Одна истина о сцене и экземплярах**: `GeometryScene` владеет статическими
  экземплярами рендера и HLOD; не создавайте отдельные трансформации Strata и не
  вводите игровые сущности только ради рендеринга.
- **Один владелец группировки**: группировка косвенных отрисовок и привязка
  материалов остаются в существующем рендерере, а будущий адаптер Strata
  поставляет видимые диапазоны кластеров. Никогда не отправляйте одни и те же
  примитивы дважды.
- **Согласование возможностей GPU**: GLES3/WebGL2 должны сохранить традиционные
  пути индексации. Будущие пути Metal/Vulkan/D3D должны независимо проверять
  compute, indirect count, storage-ресурсы и синхронизацию.
- **Несколько миров**: геометрические ассеты можно разделять между мирами по
  хэндлу, а таблицы экземпляров сцены и бюджеты рендеринга — свои у каждого мира/вида.
- **Динамический рельеф**: TerraWeave владеет геометрией чанков и их версиями; Strata
  потребляет неизменяемые снимки и планирует отложенное освобождение. PHYSIS владеет
  симуляцией и событиями, но не GPU-буферами.
- **Разделение проходов**: основная камера, тени, отражения и порталы могут
  требовать разных метрик отсечения/ошибки и разного включения теневых кастеров.

## Известные недостатки / следующие шаги

1. Скомпилировать и выполнить целевые тесты на нативном checkout проекта:
   ```sh
   cmake -S . -B build/strata -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_STRATA=ON
   cmake --build build/strata --target aiverse_strata_builder_tests aiverse_strata_model_adapter_tests aiverse_strata_view_tests aiverse_strata_gpu_scene_tests aiverse_strata_gpu_culling_tests -j 8
   ctest --test-dir build/strata -R '^strata_(builder|model_adapter|view|gpu_scene) --output-on-failure
   ```
2. Добавить сценовый фикстур с записывающим устройством, сравнивающий staging Strata
   OFF/ON, дедупликацию экземпляров, паритет отрисовок материалов/LOD/теней и выгрузку/перезагрузку.
3. Проверить качество силуэтов и швов при разной глубине LOD и разрывах
   атрибутов; упрощение родителей и зафиксированные границы сами по себе не
   гарантируют переходов без трещин.
4. Существующий `GlobalGeometryPool` теперь резервирует вспомогательные индексные
   диапазоны Strata без создания новой страницы, а метаданные версии 2 отображают
   базы примитивов на их фактические смещения в пуле GPU. Проверить на Mac Metal и Vulkan:
   записи в разделяемые буферы, повторную загрузку/инвалидацию ревизий, вывод из
   обращения ресурсов в полёте, соседние жёсткие модели и все исходные пути отката до
   включения отправки. Staging по умолчанию остаётся OFF.
5. Экспериментальное отсечение Metal compute теперь выдаёт диагностические
   диапазоны, выбранные на GPU, но не рисует их. Выполнить нативную компиляцию Metal,
   чтение с GPU и паритет с отсортированным CPU `planView`, включая переполнение
   стека/вывода и соглашение о глубине от нуля до единицы, до любого подключения к рендереру.
6. Добавить взаимоисключающую отправку выбранных диапазонов в Renderer3D только после
   паритета выбора GPU/CPU И паритета скриншотов. Сохранить устаревший путь отката на всех
   неподдерживаемых устройствах; затем заняться Hi-Z, стримингом и независимыми
   бенчмарками CPU/GPU.

## Политика проверки

О компиляторе, CTest, скриншотах, таймингах GPU и бенчмарках можно сообщать только
после **фактического выполнения** с точными логами или URL CI. Запуски в очереди или
отменённые не считаются успешными результатами.
 --output-on-failure
   ```
2. Добавить сценовый фикстур с записывающим устройством, сравнивающий staging Strata
   OFF/ON, дедупликацию экземпляров, паритет отрисовок материалов/LOD/теней и выгрузку/перезагрузку.
3. Проверить качество силуэтов и швов при разной глубине LOD и разрывах
   атрибутов; упрощение родителей и зафиксированные границы сами по себе не
   гарантируют переходов без трещин.
4. Расширить **существующий** `GlobalGeometryPool`, чтобы безопасно выделять вспомогательный
   индексный поток Strata на существующей странице пула, с явными проверками ревизий
   сцены/модели, откатом при ошибке загрузки, выводом из обращения по фенсам кадров
   и синхронизацией разделяемых буферов. Не добавляйте второй пул геометрии VB/IB.
   Все текущие смещения метаданных Strata остаются локальными для источника, не пригодными для отрисовки.
5. Построить путь выбора сцены/кластеров на GPU с проверкой возможностей, используя
   CPU-планировщик как эталон корректности. Отправлять видимые диапазоны через одного
   существующего владельца батчинга/MDI `Renderer3D` или откатываться на обычные модели.
6. Измерить время CPU и GPU раздельно, проверить зависящие от вида тени,
   виды отражений и время жизни при нескольких мирах, затем заняться стримингом.

## Политика проверки

О компиляторе, CTest, скриншотах, таймингах GPU и бенчмарках можно сообщать только
после **фактического выполнения** с точными логами или URL CI. Запуски в очереди или
отменённые не считаются успешными результатами.


## Этап 11.1 — подтверждённый владельцем паритет растеризации на реальном Metal: PASS (2026-11-19)

**Фактический нативный вывод с Mac Studio** после исправления планирования
скриншотов Metal `bcc471e93c9a4a35f7fda457d4fa15ccbda13fe9` подтверждает:

- `aiverse_strata_metal_raster_parity` успешно собран и выполнен.
- Запросы/колбэки захвата: Empty кадр 4→6; Legacy 15→17;
  Strata Fine 26→28; Strata Coarse 37→39. Все четыре колбэка захвата
  вернули **1,920,000 байт**, что соответствует 800 × 600 × 4.
- Счётчики отрисовок профиля рендерера на запрошенный кадр:
  Empty shadow=0 opaque=0; Legacy shadow=1 opaque=1;
  Fine shadow=2 opaque=2; Coarse shadow=1 opaque=1.
- `Legacy/Fine changed_channels=0`;
  `Legacy/Coarse changed_channels=0`.
- Три сообщённые проверки PASS: фактический паритет пикселей растеризации
  страницы источника в `Renderer3D`, отправки main+sun-shadow и
  один ModelDraw на кадр сравнения без второго пула геометрии GPU.
- Итог `[strata-raster] status=PASSED frames=44`.
- Диагностика конвейеров Metal:
  `[metal-assets] libraries=0 sourceMisses=11 pipelines=16 pipelineFailures=0 archive=0 strict=0`.

Ошибка прежнего фикстура `Legacy and empty screenshots identical`
исправлена без ослабления проверки непустого базового изображения и критериев
паритета пикселей. Паритет выбора кластеров на реальном Metal уже был
независимо подтверждён владельцем: `cases=5/5 status=PASSED`;
целевые нативные CTest на CPU/записывающем RHI прошли 6/6 в ранее
предоставленном логе. Прогон этапа 11.1 **не** перезапускал все шесть CTest.

**Это доказывает лишь то, что беспотерные индексы родителей/листьев дают идентичные
итоговые RGBA-изображения основной камеры через существующий рендерер на этом фикстуре,
со счётчиками отправок основного прохода и прохода теней.** Это НЕ демонстрирует
прямое равенство текстуры глубины карты теней, паритет отражений,
эквивалентность произвольных материалов, визуальное качество намеренно упрощённых LOD,
швы смешанных LOD без трещин, косвенный рендеринг с выбором на GPU,
автоматический стриминг или паритет между бэкендами. Продакшн-маршрутизация
отрисовок Strata остаётся OFF; main не изменён; PR #35 остаётся Draft.

Далее: добавить независимый паритет чтения текстуры глубины теней и фикстуры
отражений/нескольких видов, затем случаи с разными материалами, упрощением и
несколькими экземплярами. Продолжать соблюдать одного владельца `GlobalGeometryPool` и
одного владельца отрисовки `Renderer3D`; только после этого рассматривать опциональное
эксклюзивное продакшн-переключение Legacy/Strata.

## Этап 12 — нативный паритет сырой глубины теней Metal и двух независимых видов (2026-11-19)

**Реализация закоммичена; нативная компиляция и выполнение ЕЩЁ НЕ
ПРОВЕРЕНЫ**. Это новый опциональный самостоятельный фикстур, который не
изменяет ранее подтверждённый владельцем фикстур растеризации этапа 11, не
переключает владение продакшн-рендерером на Strata и не вводит второй
пул геометрии GPU.

### Диагностический GPU API и время жизни

- `engine/gfx/Gfx.h`: `gfx::Device::readDepthTextureAsync(TextureHandle,
  DepthReadFn)` необязателен (по умолчанию false на других бэкендах RHI);
  возвращает плотно упакованные нативные пиксели `float32` в
  `std::vector<uint8_t>` вместе с шириной и высотой или пустые данные при
  ошибке GPU. Колбэк срабатывает в потоке-владельце устройства при одном из
  последующих `beginFrame`. Вызывающий код должен запланировать его после
  записывающего прохода и держать текстуру живой до завершения команды.
- `engine/gfx/metal/GfxMetal.mm`: ограниченный, не останавливающий GPU блит Metal
  из приватной текстуры `Depth32Float` в разделяемый буфер. Принимает только
  2D-текстуру глубины без мультисэмплинга; отклоняет открытые энкодеры,
  недействительные хэндлы, неподдерживаемый формат, размеры больше 4096², более
  восьми незавершённых чтений или передачи больше 64 MiB.
  Использует строки блита Metal с выравниванием 256 байт, переупаковывает в плотный
  float32, проверяет статус завершения GPU и доставляет колбэки в
  потоке-владельце устройства; без глобального синхронизирующего ожидания GPU.
  В обычном рендеринге этот API не используется.
- `engine/render/Renderer3D.h/.cpp`:
  `requestSunShadowDepthReadback(DepthReadFn)` **не раскрывает хэндл
  собственной текстуры теней** и делегирует RHI только при наличии активной
  карты солнечных теней. Вызывать после `Renderer3D::render`
  и после завершения ещё открытого основного прохода.

### Исполняемый файл этапа 12

`tests/render/strata_metal_depth_multiview_parity.cpp` собирается как
`aiverse_strata_metal_depth_multiview_parity` на macOS при
`AIVERSE_STRATA_METAL_GPU_TESTS=ON`. Он намеренно не
зарегистрирован как headless CTest, поскольку открывает реальный CAMetalLayer.

Для каждой из ДВУХ независимых камер (вид 0: pitch 0.2, yaw 0,
distance 5; вид 1: pitch 0.55, yaw 0.7, distance 6) рендерятся четыре
отдельных *взаимоисключающих* кадра устойчивого режима:
Empty (без ModelDraw), Legacy, Strata Fine (два потомка) и
Strata Coarse (беспотерный родитель). Ни один кадр не отправляет
одновременно устаревший источник и Strata; все отрисовки идут через обычный
`Renderer3D::drawModel`. Псевдоним Strata использует исходный VB
`GlobalGeometryPool` и вспомогательный индексный sidecar.
Карта солнца намеренно 512×512 для ограниченного чтения, тогда как
реальное экранное изображение остаётся 800×600.

Для каждого вида/режима фикстур захватывает:
- Фактический основной RGBA-кадр через `captureNextFrame` после включения
  читаемости экрана до получения кадра, со скриншотами,
  запрашиваемыми внутри устойчивого режима, чтобы обойти известную ошибку
  задержки захвата; и
- Реальные пиксели `Depth32Float` текущей текстуры солнечных теней
  через новый блит GPU **после закрытия основного прохода**, в том же
  кадре. Данные не выводятся из счётчиков отрисовок GPU.

Приёмка требует: все восемь захватов RGBA и восемь захватов глубины,
непустой Legacy относительно Empty для ОБОИХ форматов, ноль изменённых каналов RGBA
между Legacy/Fine/Coarse в каждом виде, **ноль изменённых байт в
сырой глубине солнечных теней float32** между Legacy/Fine/Coarse в каждом
виде, конечные значения глубины в [0,1], хотя бы один отсчёт, отличный
от глубины очистки, ненулевые счётчики отрисовок теней/opaque для случаев с моделью,
ровно один ModelDraw кандидата на кадр и различающиеся
RGBA-изображения Legacy для двух видов камеры.
Этот строгий эталон уместен только потому, что иерархические LOD фикстура
намеренно **геометрически беспотерны**.
Он НЕ доказывает точность упрощённых LOD.

По умолчанию раннер сохраняет восемь цветных захватов `strata-depth-view{0|1}-MODE.ppm`
и восемь
сырых снимков глубины `strata-depth-view{0|1}-MODE-shadow.f32` — упакованные 32-битные
float в little-endian — в рабочий каталог.
Задайте `AIVERSE_STRATA_DEPTH_CAPTURE_PREFIX=/path/prefix`, чтобы изменить это.
При 512² значений float32 каждый файл глубины занимает 1 MiB.

Ручная сборка и запуск на Mac Studio:

```sh
cd ~/Projects/AIVERSE-strata-validation
git status --short
git fetch origin feature/strata-virtual-geometry
git switch --detach origin/feature/strata-virtual-geometry
cmake -S . -B build/strata \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_STRATA=ON \
  -DAIVERSE_STRATA_METAL_GPU_TESTS=ON
cmake --build build/strata --target \
  aiverse_strata_metal_depth_multiview_parity --parallel 8
./build/strata/aiverse_strata_metal_depth_multiview_parity
```

Переключение коммитов требует чистого рабочего дерева или сохранённых локальных правок.
Ожидаемые, **ещё не наблюдавшиеся**, итоговые сводки при успехе:

```text
[strata-depth] view=0 color_fine=0 color_coarse=0 depth_fine_bytes=0 depth_coarse_bytes=0
[strata-depth] view=1 color_fine=0 color_coarse=0 depth_fine_bytes=0 depth_coarse_bytes=0
[strata-depth] PASS: exact sun-shadow Depth32Float Legacy/Fine/Coarse parity
[strata-depth] status=PASSED views=2 modes=4 ...
```

Задание macOS в `.github/workflows/strata-focused.yml` теперь
**компилирует** исполняемый файл этапа 12, но не выполняет GUI-фикстуры
GPU. Пока не предоставлен реальный вывод с Mac Studio владельца, не
заявляйте PASS этапа 12, реальный паритет глубины теней, паритет независимых видов
или безопасность продакшн-рендеринга Strata. Прежний строгий паритет основной
растеризации этапа 11 на реальном Metal остаётся **фактически PASS**
(0 изменённых каналов для Fine и Coarse), а прежний эталон
compute на реальном Metal остаётся **5/5 PASS** в своём отдельном фикстуре.

**Следующие рубежи:** чтение цели планарного отражения и выбор для каждой камеры отражения,
паритет при нескольких материалах и границах UV, выполнение отрисовки
выбранных на GPU диапазонов кластеров с эксклюзивным устаревшим откатом, качество
LOD с потерями/трещины на рёбрах, жизненный цикл нескольких миров и производительность GPU
до рассмотрения продакшн-значения по умолчанию.


## Аппаратная проверка этапа 12 — глубина/несколько видов на реальном Metal: PASS (2026-11-19)

Вывод терминала с Mac Studio, предоставленный владельцем, подтверждает **успешную нативную
компиляцию и выполнение** ручного фикстура этапа 12
`aiverse_strata_metal_depth_multiview_parity`.

- Прежний фикстур выбора на реальном Metal GPU compute был перезапущен и
  подтвердил `[strata-metal] cases=5/5 status=PASSED`.
- Прежний фикстур растеризации этапа 11 был перезапущен и подтвердил как
  `Legacy/Fine changed_channels=0`, так и
  `Legacy/Coarse changed_channels=0`, с
  `[strata-raster] status=PASSED frames=44`.
- **Этап 12** захватил все **8 RGBA** изображений основной камеры
  (800×600, по 1,920,000 байт) и **8 карт солнечных теней Depth32Float**
  (512×512, по 262,144 отсчёта float), покрывая
  Empty/Legacy/Strata Fine/Strata Coarse для двух разных камер.
  Каждый колбэк GPU завершился, включая асинхронные чтения
  глубины и отложенные цветные скриншоты.
- Точный паритет, камера 0:
  `color_fine=0 color_coarse=0 depth_fine_bytes=0 depth_coarse_bytes=0`.
- Точный паритет, камера 1:
  `color_fine=0 color_coarse=0 depth_fine_bytes=0 depth_coarse_bytes=0`.
- Сырые карты были непустыми и содержали непостоянный вклад модели:
  `nonclear_shadow_samples=73179` (вид 0),
  `nonclear_shadow_samples=34765` (вид 1).
- Сообщения PASS подтвердили независимые виды камер, точный
  паритет основного цвета, точный **паритет реальной текстуры глубины GPU** и
  эксклюзивную отправку модели:
  `[strata-depth] status=PASSED views=2 modes=4 frames=89`.
- Metal сообщил
  `[metal-assets] libraries=0 sourceMisses=11 pipelines=16 pipelineFailures=0 archive=0 strict=0`
  для прогона этапа 12.

Статус `NOT RUN` в прежней документации описывает
*состояние до проверки* и заменён этим фактическим выводом
с Mac Studio. Владелец не вывел `git rev-parse HEAD` в логе;
поэтому точный SHA **проверенного** коммита остаётся неподтверждённым
и его нельзя угадывать.

**Область доказательства:** фикстур использует намеренно **беспотерную**
геометрию кластеров родитель/лист. Он проверяет два реальных основных вида
и **сырые байты глубины** солнечных теней, а не только счётчики
отрисовок теней. Он НЕ устанавливает паритет целей рендеринга отражений,
разные материалы и UV-швы, намеренно упрощённые LOD с потерями,
предотвращение трещин между чанками, реальную отправку отрисовок с выбором на GPU,
продакшн-маршрутизацию Strata/Legacy, стриминг памяти,
паритет между бэкендами с Vulkan или производительность. Это явные
следующие рубежи. Все продакшн-пути рендеринга Strata остаются по умолчанию OFF;
исходный `Renderer3D` остаётся авторитетным, и `main`
этой проверкой не изменяется.


## Этап 13 — A/B реального RTT планарного отражения Metal с двумя текстурированными материалами (2026-11-19)

**Реализация закоммичена; реальная компиляция и выполнение этапа 13 на Mac Studio
ЕЩЁ НЕ ВЫПОЛНЕНЫ.** Этот этап сохраняет без изменений все
ранее проверенные фикстуры Metal (выбор 5/5, растеризация этапа 11,
глубина солнечных теней для двух видов этапа 12) и оставляет
PR в состоянии Draft. Он не вводит продакшн-маршрут отрисовки Strata,
не создаёт второй пул геометрии GPU и не отправляет исходную геометрию дважды.

### Диагностическое чтение цвета offscreen

- \`gfx::Device::readColorTextureAsync(TextureHandle,ColorReadFn)\`
  — опциональный диагностический хук RHI, по умолчанию возвращающий false на
  неподдерживаемых бэкендах. Он захватывает **фактическую offscreen-цель
  рендеринга**; он не должен возвращать вместо неё обычный скриншот экрана.
  Колбэк возвращает плотно упакованные байты RGBA8 сверху вниз, ширину
  и высоту в потоке-владельце устройства в последующем кадре,
  не блокируя GPU.
- Реализация Metal принимает только текстуры цели рендеринга \`RGBA8\`
  без MSAA, основанные на
  \`MTLPixelFormatBGRA8Unorm\`; она отклоняет недействительные изображения и изображения, не являющиеся RT,
  активные графические/compute-энкодеры, чтения >4096² или >64 MiB
  или более восьми незавершённых запросов. Она выполняет блит GPU **реальной
  приватной текстуры отражения** во временный разделяемый MTLBuffer,
  соблюдая выравнивание строк Metal в 256 байт. По завершении BGRA
  переупаковывается в RGBA8, а результат доставляется в поток-владелец.
  Пустой результат означает ошибку команды GPU.
  **Чтение отражения HDR/RGBA16F намеренно не поддерживается.**
- \`LightSystem::requestPlanarReflectionColorReadback\` даёт
  узкий доступ только на чтение к существующей текстуре
  \`reflColor_\` рендерера лишь после того, как фактический проход
  \`beginReflection/endReflection\` этого кадра успешно выполнен, и только
  для LDR. \`Renderer3D::requestPlanarReflectionColorReadback\`
  также требует, чтобы выбранная плоскость отражения была действительной;
  он не раскрывает владение сырой текстурой.
- Новый отдельный **диагностический** \`strata::RenderPass::PlanarReflection\`
  позволяет выполнять предварительные проверки выбора с фактической
  view-projection отражённой камеры. Он не включает никаких продакшн-команд
  Strata. Исходная геометрия по-прежнему использует единственное исходное
  выделение \`GlobalGeometryPool\` и индексный sidecar.

### Ручной фикстур Metal этапа 13

\`tests/render/strata_metal_reflection_material_parity.cpp\`,
цель \`aiverse_strata_metal_reflection_material_parity\`,
собирается на macOS только при \`AIVERSE_STRATA_METAL_GPU_TESTS=ON\`.
Фикстур использует два физических соседних непрозрачных меш-примитива
с общей границей x=0, но **продублированными вершинами шва,
независимыми UV-координатами и разными PBR-материалами**. Каждый
материал сэмплирует свою GPU-текстуру шахматки 4×4 с независимым
UV-преобразованием и имеет отдельно подготовленную беспотерную иерархию
родитель/лист (один родитель с обоими треугольниками, два треугольника-потомка).
Вся геометрия GPU загружается **один раз** для исходной Model
через существующий \`GlobalGeometryPool\`; Fine/Coarse —
тестовые псевдонимы с заимствованными исходными GPU-буферами и смещениями
индексов sidecar, а не дублирующие выделения моделей на GPU.

Полупрозрачная водная поверхность с существующим
флагом \`kDrawWaterSurface\`, specular 0.98 и высококачественным
\`LightSystem\` запускает \`Renderer3D::renderReflection()\`,
реальный RTT отражения половинного разрешения. Остальные пути рендеринга
не изменены. Четыре взаимоисключающих режима кадра
Empty/Legacy/Fine/Coarse рендерятся одним и тем же обычным
\`Renderer3D::drawModel\` из двух разных положений камеры.
\`prepareRenderCandidate\` использует отражённую
view-projection отражения для каждого материала. Отсечение по пирамиде
видимости в этом предварительном фикстуре отключено, чтобы изолировать строгий
выбор смещений индексов/LOD при нескольких материалах;
**существующий продакшн-вид отражения Renderer3D по-прежнему выполняет
отсечение по видимости**. Ни один кадр никогда не рисует Legacy и Strata вместе.

Фикстур запрашивает чтения GPU внутри каждого устойчивого режима,
покрывая 2 вида × 4 режима × ТРИ выхода:

- Фактический скриншот основной камеры 800×600 RGBA8
  (\`strata-reflect-view{0|1}-MODE.ppm\`).
- Фактическая текстура солнечных теней 512×512 Depth32Float
  (\`strata-reflect-view{0|1}-MODE-shadow.f32\`).
- **Фактическая цель рендеринга планарного отражения 400×300 RGBA8**
  (\`strata-reflect-view{0|1}-MODE-reflection.ppm\`).
  Это не вырезка из основного экрана.

Строгая приёмка требует всех 24 результатов GPU; **фактического**
прохода отражения; отличия основного кадра, глубины И отражения Legacy
от пустого базового изображения (в каждом виде); точно ноль изменённых байт
между Legacy/Fine/Coarse во всех трёх выходах и обеих
камерах; различающихся основных изображений и изображений отражения Legacy между
видами; не менее двух отрисовок источника opaque/материалов и четырёх
отрисовок мелких кластеров; и одного ModelDraw на кадр кандидата.
Равенство с нулевой разницей допустимо, потому что эта иерархия
намеренно беспотерна. Это НЕ метрика ошибки для
намеренно упрощённых LOD и не утверждение о поддержке
отражений воды на каждом бэкенде RHI.

Ручная проверка владельцем на Mac Studio:

\`\`\`sh
cd ~/Projects/AIVERSE-strata-validation
git status --short
git fetch origin feature/strata-virtual-geometry
git switch --detach origin/feature/strata-virtual-geometry
cmake -S . -B build/strata -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_STRATA=ON \
  -DAIVERSE_STRATA_METAL_GPU_TESTS=ON
cmake --build build/strata --target \
  aiverse_strata_metal_reflection_material_parity --parallel 8
./build/strata/aiverse_strata_metal_reflection_material_parity
\`\`\`

Сохраните локальную незакоммиченную работу перед переключением ветки.
Ожидаемая итоговая сводка при успехе (ЕЩЁ НЕ НАБЛЮДАЛАСЬ):

\`\`\`text
[strata-reflect] view=0 main_fine=0 main_coarse=0 shadow_fine=0 shadow_coarse=0 reflection_fine=0 reflection_coarse=0
[strata-reflect] view=1 main_fine=0 main_coarse=0 shadow_fine=0 shadow_coarse=0 reflection_fine=0 reflection_coarse=0
[strata-reflect] PASS: actual planar reflection RTT nonblank and byte-identical across Legacy/Fine/Coarse
[strata-reflect] status=PASSED views=2 modes=4 frames=...
\`\`\`

Целевой workflow macOS на GitHub **компилирует** новую цель, но
не выполняет интерактивные GUI-тесты GPU. То, что CI в очереди, в ожидании
или просто зелёный на этапе компиляции, не доказывает нативный паритет этапа 13.

**Ещё не выполнено:** фактическая сборка/запуск этапа 13 на Mac Studio; HDR
планарные отражения, качество намеренно упрощённых LOD мешлетов с потерями
и тесты швов между кластерами, живая отправка растеризации/ICB с выбором на GPU
с эксклюзивным устаревшим откатом, тесты стриминга/времени жизни, паритет
отражений Vulkan и контролируемые бенчмарки производительности реального GPU.


## Аппаратная проверка этапа 13.1 — RTT планарного отражения на реальном Metal: PASS (2026-11-19)

Владелец предоставил **фактический лог сборки и выполнения на GPU Mac Studio**
для `aiverse_strata_metal_reflection_material_parity`. Он был
успешно скомпилирован и слинкован, а затем завершился с

```text
[strata-reflect] view=0 main_fine=0 main_coarse=0 shadow_fine=0 shadow_coarse=0 reflection_fine=0 reflection_coarse=0
[strata-reflect] view=1 main_fine=0 main_coarse=0 shadow_fine=0 shadow_coarse=0 reflection_fine=0 reflection_coarse=0
[strata-reflect] PASS: actual planar reflection RTT nonblank and byte-identical across Legacy/Fine/Coarse
[strata-reflect] PASS: two independent cameras, two opaque UV-textured materials
[strata-reflect] PASS: sun-shadow and main RGBA strict pixel parity, one pooled source and one ModelDraw
[strata-reflect] status=PASSED views=2 modes=4 frames=89
[metal-assets] libraries=0 sourceMisses=11 pipelines=16 pipelineFailures=0 archive=0 strict=0
```

Восемь RGBA-снимков основного вида (800×600, по 1,920,000 байт),
восемь фактических чтений глубины солнечных теней Metal (512×512, по 262,144
отсчёта float) и восемь **независимых чтений offscreen RTT
планарного отражения** (400×300, по 120,000 пикселей) завершились
для двух независимых камер и четырёх отдельных режимов
Empty/Legacy/Strata Fine/Strata Coarse. Захваты
доставлялись асинхронно (колбэки экрана через 2–3 кадра после запросов),
а колбэки глубины и RTT приходили в том же или следующем кадре. Проверки
непустого базового изображения отражения и проверки отправок для каждого
материала в фикстуре прошли.

Разница между источником и Strata для каждой камеры везде **нулевая**
для основного цвета, сырых байтов глубины теней и цвета RTT
отражения при намеренно *беспотерной* иерархии родитель/лист.
Это реальное свидетельство паритета рендерера, а не суррогатный скриншот
экрана и не имитированный RHI.

Владелец запустил новый фикстур в чистом изолированном рабочем дереве
`AIVERSE-strata-stage13`, после того как прежнее рабочее дерево
`AIVERSE-strata-validation` оказалось грязным.
Лог пользователя **не** содержит выведенного `git rev-parse HEAD`
для этого прогона: точный SHA проверенного исходного кода независимо
не наблюдался; не выдумывайте его.

**Чего это не доказывает:** вид упрощённых LOD с потерями,
границы LOD без трещин, отправку команд косвенной растеризации с выбором на GPU,
паритет отражений Vulkan или HDR, продакшн-подавление устаревшего пути/откат Strata,
время кадра GPU или стриминг/повторное использование при нескольких мирах. Этап 13 теперь
подтверждён владельцем как **PASS в строгих рамках фикстура**, что заменяет
прежнюю плановую пометку «ещё не запускался». PR #35 остаётся Draft,
продакшн-рендерер не изменён.

### Риск интеграции

На момент этой проверки ветка всё ещё расходилась с
параллельной работой в `main`. Изолированный PASS этапа 13
**не** сертифицирует объединённое дерево main+Strata. Перед слиянием
чего-либо сохраните локальные рабочие деревья, согласуйте последние изменения
main в отдельной интеграционной проверке и выполните
целевые CTest и все нативные исполняемые файлы паритета Metal на
этом объединённом снимке исходного кода.


## Этап 14 — реальный переносимый путь растеризации Strata на Vulkan и GLES3 (2026-11-19)

В отличие от прежней диагностики GPU compute только для Metal, у Strata
теперь есть **опциональный реальный маршрут растеризации** для существующего
\`Renderer3D\` на Vulkan, GLES3/WebGL2, Metal и desktop GL.
Маршрут использует иерархические LOD кластеров, **выбранные на CPU**.
Он не требует GPU compute, буферов shader-storage или поддержки ICB.
Все специфичные для бэкенда затенение, форматы индексов, материалы,
солнечные тени, отражения воды, отправка отрисовок и владение конвейерами
остаются в **существующем бэкенде Renderer3D**.

### Публичный API

\`\`\`cpp
scene.setStrataPortableRenderingEnabled(true); // before load
scene.load(device, sceneJson, readFile);
renderer.beginScene(camera, width, height);
scene.draw(renderer);
renderer.render(mainPass); // submits one source OR selected Strata
device.endPass();
\`\`\`

По умолчанию OFF: ровно прежний путь Legacy. Опция
автоматически запрашивает \`SceneStaging\` и не зависит от жанра.
При загрузке она использует \`strata::PortableSource\` для каждой подходящей жёсткой
модели и собирает псевдонимы для каждого экземпляра со стабильным временем жизни
на протяжении \`Renderer3D::render\`.

\`engine/render/StrataPortableRender.h/.cpp\` также предоставляет
\`PortableSource::stage(device, uploadedSource, sourceAsset)\`,
\`prepareModel(drawWorld, passViews, forceFinestForUnspecifiedPass,
outputAlias)\`, \`validFor\` и явный \`shutdown(device)\` для
нативной игры или редактора. \`outputAlias\` заимствует все
вершинные буферы и текстуры GPU: **никогда не вызывайте \`uploadModel\` или
\`releaseModel\` для этого псевдонима**. Держите его живым, пока рендерер
не обработает все отправки проходов. При ошибке выход
не изменяется: рисуйте исходный источник.

### Поведение бэкендов

| Бэкенд | Путь видимой геометрии | Индексы | LOD через GPU compute |
| --- | --- | --- | --- |
| Metal | существующие PBR/main/shadow/reflection Metal в Renderer3D | существующий pooled sidecar GlobalGeometryPool, где возможно, иначе IB только с индексами | опциональный диагностический GPU compute (отдельный, не авторитетный) |
| Vulkan | существующие индексированные отрисовки Vulkan / CPU MDI в Renderer3D | тот же sidecar пула источника, где Vulkan поддерживает разделяемый индексированный MDI, иначе один неизменяемый IB только с индексами | **выбор на CPU**; compute dispatch в Vulkan RHI сейчас не реализован |
| GLES3/WebGL2 | существующий GLES3 \`glDrawElements\`, эмуляция baseVertex в GL RHI | неизменяемый IB только с индексами, заимствующий VB источника | выбор на CPU, без требования compute/SSBO |
| GL33 | существующие индексированные отрисовки GL | в пуле, если поддерживается, иначе неизменяемый IB только с индексами | выбор на CPU |

**Ни второго вершинного буфера Strata, ни второго пула геометрии, ни
отдельного рендерера, ни дублирующей отрисовки источник + Strata.**
Вершинный буфер источника остаётся единственным выделением вершин на GPU.
Отдельный sidecar только с индексами на GLES3 намеренный: существующий
GlobalGeometryPool был ограничен MDI, а ES3.0/WebGL2 не
поддерживают необходимые возможности GPU-indirect. Индексный буфер
повторно использует индексы вершин и base vertex исходного источника.

### LOD для нескольких проходов и откат

\`PortableSource::prepareModel\` строит консервативный
фронтир по всем трансформациям узлов источника и всем переданным
view-projection проходов камеры/теней. Он объединяет пути, выбирая
самый мелкий требуемый кластер (вместо того чтобы рисовать
родителя И потомка поверх одной геометрии). Он делегирует
фактическую видимость/отсечение для каждого прохода \`Renderer3D\` и
не отбрасывает треугольники просто потому, что их нет
в основной камере. Поскольку вид планарного отражения
выбирается существующим рендерером позже, переносимый
маршрут выбирает **самый мелкий резидентный фронтир**, когда
отражение может использовать ту же модель. Это может рисовать больше
кластеров, но исключает неправильно отсутствующую геометрию.

Принимаются только жёсткие, непрозрачные, без скиннинга и морфинга
модели с полным покрытием примитивов источника Strata. Неподдерживаемые
shader graph, прозрачные или анимированные материалы, устаревшие
ревизии геометрии/материалов источника, недействительные резидентные иерархии, выход
индексов за границы, ошибки загрузки источника и бюджета вызывают **откат на Legacy**
для этого экземпляра. Исходные отрисовки HLOD/прокси поверхностей остаются
Legacy. \`GeometryScene::Stats\` сообщает подготовленные/отклонённые
переносимые источники, фактические переносимые отрисовки/откаты и
количество выбранных кластеров.

### Полная подготовка на CPU перед staging во время импорта

Обычный \`uploadModel()\` может генерировать LOD, касательные,
батчи теней/материалов и даже выполнять опциональное уплотнение
жёсткой геометрии, изменяя индексы источника после раннего staging.
Переносимая геометрия теперь явно выполняет
\`prepareModelUpload(source)\` **до построения ассетов Strata**,
чтобы CPU-модель, загрузка источника на GPU и индексный поток sidecar
имели одну и ту же итоговую раскладку вершин/индексов.
Переносимый \`SceneStaging\` подготавливает только корневые меши, на которые
действительно ссылаются живые узлы модели, и может подготовить
подготовленный корень LOD0, даже если существуют авторские/сгенерированные альтернативные
LOD-меши. Эти альтернативные меши остаются нетронутыми для
отката на Legacy; у псевдонима нет второй встроенной
цепочки LOD, что предотвращает два независимых выбора LOD.
Это опциональное поведение не изменяет прежнее
значение по умолчанию для staging, используемого только для диагностики.

### Статус проверки

**Код закоммичен; переносимый рантайм/визуальный паритет Vulkan, GLES3 и нового Metal
ЕЩЁ НЕ собирались и не тестировались.**
Владелец явно попросил **никаких промежуточных тестов**:
завершить функциональность, затем выполнить одну финальную проверку
сборки/ошибок и один нативный проход проверки на Mac Studio.
Существующие результаты этапов 8–13 относятся к прежним фикстурам,
а не к новому переносимому маршруту отрисовки.

Новые тестовые цели, подготовленные к финальному прогону:
\`aiverse_strata_portable_tests\` (контракт индексов+LOD/аренды бэкендов
GLES3/Vulkan/Metal/GL на записывающем устройстве) и
\`aiverse_strata_metal_portable_parity\` (тот же путь растеризации
Renderer3D на реальном нативном Metal). Исходный код и цель
в \`tests/render/strata_portable.cpp\`,
\`tests/render/strata_metal_portable_parity.cpp\`,
\`CMakeLists.txt\`. Агент не выполнял никаких тестов.
Целевой CI собирает переносимый тест, но не запускает
его новый случай до финальной проверки.

**Оставшаяся оптимизация Vulkan:** нативное отсечение через Vulkan GPU
compute, уплотнение выбранных диапазонов в SSBO и отправка
indirect-команд, сформированных на GPU, не входят в текущий Vulkan RHI;
завершённый откат растеризации с выбором на CPU остаётся пригодным и
ориентированным прежде всего на корректность. Паритет Vulkan GPU compute требует
дополнительного compute API в Vulkan RHI, шейдера GLSL/SPIR-V и аппаратных доказательств.


## Этап 14.1 — стабильная пространственная иерархия LOD и безопасный откат на нескольких бэкендах

Strata теперь группирует **родительские кластеры LOD по центрам 3D AABB,
упорядоченным по Мортону, на каждом уровне иерархии**. Это уменьшает случайное
слияние далеко отстоящих групп мешлетов, когда порядок импорта модели не
пространственный. Листовые треугольники LOD0, ID вершин UV/атрибутов и исходная
раскладка вершинного буфера остаются нетронутыми. Родители по-прежнему используют
упрощение meshoptimizer с зафиксированными границами и учётом атрибутов, а также
консервативную накопленную ошибку.

`PortableSource` привязан к устройству: подготовленный индексный буфер может быть
уничтожен только своим исходным живым устройством RHI; подготовка отказывается работать
с неисправными устройствами. Модели источников с заранее вычисленными обычными `GeometryData`
(окклюдеры/устаревшие кластеры) теперь допускаются. Исходная `Model`
сохраняет эти метаданные для атомарного отката на Legacy, тогда как
псевдоним Strata намеренно не имеет независимых `GeometryData` и
второй встроенной цепочки LOD, поэтому рендерер не может дважды выбрать
иерархию кластеров. Vulkan и GLES3 используют один и тот же путь с выбором на CPU;
GLES3 может использовать исходный VB с одним выделенным индексным буфером.
Sidecar в пуле Vulkan/Metal остаётся оппортунистическим, никогда не обязательным.

Staging sidecar теперь проверяет у каждого узла иерархии конечные упорядоченные
границы, вложенность в границы родителя, неубывающую ошибку LOD родителя и
сумму количества индексов листьев относительно `indexCount` исходного примитива
перед загрузкой буфера на GPU. Это не даёт очевидно
неполным/повреждённым потоковым ассетам порождать отсутствующие треугольники.
Это не может проверить точные идентичности треугольников после освобождения
исходных индексных данных на CPU.

Новый регрессионный **код тестов** покрывает далеко разнесённые перемежающиеся
острова мешей, искажённые границы родителей/покрытие листьев, потерю устройства и
shutdown с чужого устройства, а также уже существующие устаревшие кластеризованные источники
на Vulkan/GLES3. **Ни один из этих тестов и ни одна нативная сборка не были
выполнены на этапе 14.1.** Ранее подтверждённый владельцем строгий паритет пикселей
отражения/теней/основного вида этапа 13 на Metal относится только к
более старому тестовому сценарию, а не к новому продакшн-маршруту с выбором на CPU.

Точные коммиты этапа 14.1, изменённые файлы, ограничения и следующий план
проверки см. в `docs/agent-handoffs/strata-virtual-geometry.md`.
`main` не влит в эту незавершённую функциональную ветку;
сохраните переключатель, выключенный по умолчанию, и не считайте это аппаратной
сертификацией Vulkan/GLES3.


## Этап 15 — опциональная бюджетированная резидентность индексов на GPU (реализация, ещё не проверена)

Для источников, которые не должны держать всю свою иерархию индексов Strata
(родители+листья) резидентной в памяти GPU, переносимый путь Metal/Vulkan/GLES3
теперь поддерживает компактный кэш резидентности **только индексов**. Исходный
вершинный буфер Model всегда повторно используется и хранится существующим рендерером.

```cpp
render::GeometryScene scene;
scene.setStrataPortableRenderingEnabled(true);
scene.setStrataStreamingIndexBudgetBytes(256 * 1024); // bytes PER source
scene.load(device, json, fileReader);
// Renderer3D::beginScene(...)
scene.draw(renderer); // preflight all instances, then submit one draw/source
// renderer.render(...)
```

Положительный бюджет выбирает потоковый путь; ноль сохраняет полностью
резидентный индексный sidecar этапа 14 в пуле/выделенный. Должен быть задан до
загрузки сцены. Каждая CPU-модель сохраняет неизменяемую полную иерархию Strata.
Уплотняется/стримится только поток индексов на GPU; вершинные данные мешей,
текстуры, анимации, владение материалами и трансформации узлов/мира
не изменяются.

Корневые кластеры закреплены. Запросы объединяются по основной камере,
солнечным теням, нескольким экземплярам сцены и неизвестным будущим видам
планарного отражения. Полные группы братьев могут быть повышены, когда позволяет бюджет; при
нехватке ранее резидентная родительская геометрия остаётся пригодной для отрисовки. Каждая
перестройка на GPU загружает **новое неизменяемое** поколение индексного буфера перед
его атомарной публикацией. Недействительные ассеты, неудачные выделения,
неполные доступные потомки или устаревшие хэндлы GPU источника не могут
порождать частичные отправки. Тот же Renderer3D по-прежнему выполняет все
отрисовки растеризации, теней и отражений, с доступным откатом на Legacy.

`StrataIndexResidency` также можно использовать напрямую из нативного кода:
`initialize(device, modelAsset, budgetBytes)`,
`update(demands)`, `span(primitive,node)`, `stats()` и
`shutdown(device)`. `PortableSource` дополнительно поддерживает
`collectResidencyDemands` и `updateResidency`. Положительный бюджет
сейчас ограничен максимум 64 MiB на источник; размер закреплённых корней —
минимум. `GeometryScene::Stats` показывает живые байты потоковых индексов,
количество источников, ошибки подготовки и количество вытеснений.

**Важные ограничения:** это бюджет *активного выделения* индексов GPU на источник,
а не совокупный бюджет сцены/нескольких миров; старые GPU-буферы в полёте могут
временно превышать активный бюджет, пока не завершится их освобождение в RHI.
Система пока не стримит индексы с диска/из сети, не уплотняет
исходные CPU-ассеты, не реализует разреженные страницы GPU, не выполняет отсечение
мешлетов через Vulkan compute и не гарантирует переходы кластеров *с потерями* без трещин.
Повторная переупаковка буфера ориентирована на корректность, а не на оптимизированную
загрузку маленьких страниц. Тесты написаны, но **не выполнены**; у этапа 15
нет проверенных визуальных результатов или результатов производительности на реальных Metal/Vulkan/GLES3.

Подробная передача дел на уровне коммитов:
`docs/agent-handoffs/strata-virtual-geometry.md` (этап 15).


### Этап 15.1 — совокупный бюджет сцены, детерминированный допуск корней

```cpp
render::GeometryScene scene;
scene.setStrataPortableRenderingEnabled(true);
scene.setStrataStreamingIndexBudgetBytes(256 * 1024);  // per-source cap
scene.setStrataStreamingSceneBudgetBytes(8 * 1024 * 1024); // TOTAL scene active index cap
scene.load(device, sceneJson, readFile);
```

Бюджет сцены необязателен и по умолчанию равен нулю (без совокупного ограничения).
Если задан сам по себе, он тоже включает стриминг; у каждого совместимого источника
максимум — его полные индексы Strata или 64 MiB, в зависимости от того, что
меньше. Байты корневых индексов должны быть допущены для всего источника, прежде чем
STRATA сможет его рисовать. При нехватке памяти первыми допускаются источники с меньшими
корневыми выделениями (детерминированное разрешение ничьих по ключу);
недопущенные источники продолжают идти через Legacy. Оставшиеся байты
распределяются между допущенными источниками в соответствии с их доступным
запасом. `strataStreamingBudgetDeclined` считает эти откаты.

Это гарантирует, что **активное хранилище индексов STRATA на GeometryScene**
не может превысить заданный совокупный предел, не считая исходных
вершинных/индексных буферов источника, других систем рендеринга и копий RHI
выведенных из обращения, но ещё находящихся в полёте поколений индексов Strata. Это не
глобальный менеджер памяти GPU для нескольких миров, и он пока не избегает
переупаковки всего буфера. Чистая политика распределения покрыта
написанными, но **невыполненными** регрессионными фикстурами. Ссылки на коммиты
см. в разделе об этапе 15.1 в передаче дел агента.


## Этап 15.2 — инкрементальные страницы индексов GPU на Metal и Vulkan

Прежний кэш этапа 15 заменяет весь компактный неизменяемый индексный
буфер при каждом изменении набора резидентных узлов. Этап 15.2 добавляет
**опциональный инкрементальный режим** на Metal/Vulkan, который владеет одним RHI-буфером
фиксированной ёмкости, совместимым с `Storage`/INDEX, и загружает только вновь запрошенные
поддиапазоны индексов узлов кластеров в неиспользуемые или освобождённые по фенсу GPU слоты.

```cpp
render::GeometryScene scene;
scene.setStrataPortableRenderingEnabled(true);
scene.setStrataStreamingIndexBudgetBytes(256 * 1024);
scene.setStrataStreamingSceneBudgetBytes(8 * 1024 * 1024);
scene.setStrataIncrementalIndexStreamingEnabled(true);  // NEW
scene.setStrataStreamingUploadBudgetBytes(128 * 1024); // NEW, bytes/source/update
scene.load(device, sceneJson, readFile);
```

Обе опции задаются **до** `load/loadPrepared`. Новый
режим по умолчанию OFF. Если задан только бюджет сцены, стриминг всё равно
включён. GLES3/WebGL2 и GL33 автоматически сохраняют существующий безопасный
откат с неизменяемыми поколениями; ни compute WebGL2, ни расширение base-vertex
не требуются.

Выделение индексов идёт по узлам кластеров Strata, и все потомки
родителя повышаются одной полной группой, чтобы избежать частично отсутствующих
треугольников. Запросы от всех экземпляров источника, основного вида, теней и
отражения объединяются перед любой новой отправкой отрисовки в Renderer3D.
Родитель остаётся пригодным, пока группа ждёт своего бюджета пропускной
способности загрузки или завершённого фенса GPU. Стабильные смещения индексов на GPU и
стабильный хэндл буфера RHI избегают переупаковки всего буфера при обычном
добавлении страниц Metal/Vulkan; вытесненные страницы находятся в карантине, пока
`completedFrameSerial()` не подтвердит завершение кадра вывода из обращения.
Менеджер атомарно публикует новую таблицу резидентности только после
успешного выполнения каждой проверяемой записи страницы. Если запись не удалась, старые видимые
страницы остаются адресуемыми. Исходные вершинный и индексный буферы Legacy
по-прежнему независимо принадлежат `Model`.

`IndexResidencyStats` добавляет `incremental`, `arenaBytes`,
`uploadedBytes`, `uploadCalls`, `uploadBudgetDeferrals`
и `fenceReuseDeferrals`. `GeometryScene::Stats` их агрегирует.
**Важно:** `arenaBytes` — полное фиксированное выделение Storage,
а не только видимые `activeBytes`. Vulkan может поддерживать дополнительные безопасные
версии на GPU и теневую копию на CPU, поэтому инкрементальные загрузки поддиапазонов через API не
гарантируют, что оборудование копирует только эти байты. Этот выпуск
пока НЕ предоставляет стриминг с SSD, привязку разреженных страниц GPU,
глобальные бюджеты между мирами, уплотнение страниц или выбор LOD
через Vulkan compute. Лимит загрузки меньше одной требуемой группы братьев
будет бесконечно откладывать уточнение этой группы, пока его не перенастроят.

Исходный код и регрессионные тесты этапа 15.2 закоммичены, но **никакой
компиляции, нативных CTest или паритета на реальном устройстве для этапа 15.2
пока не выполнялось**. Точные изменённые файлы, коммиты, случаи отказов и
продолжение см. в записи об этапе 15.2 в
`docs/agent-handoffs/strata-virtual-geometry.md`.


## Этап 15.3 — асинхронный стриминг индексов GPU с диска (2026-11-20)

Этап 15.3 добавляет отчётливую границу данных между офлайном и онлайном: страницы
иерархии STRATA теперь можно **записывать в индексированные архивы на диске**,
затем читать ограниченными фоновыми рабочими потоками ввода-вывода и загружать целыми
группами братьев в арену индексов GPU этапа 15.2. Поток рендеринга
никогда не ждёт чтения страницы потомка; он рисует резидентного родителя,
пока весь набор потомков не пройдёт проверку контрольной суммы и проверяемую загрузку
на GPU.

### Офлайн-подготовка контента

```cpp
render::strata::SceneStaging staging;
staging.stage(sourceKey, cpuModel); // CPU source, before normal GPU upload
std::vector<std::string> errors;
const bool ok=staging.writeDiskArchives("Content/Strata", &errors);
```

Это создаёт `strata-<key-hash>.strataidx` для каждого подготовленного источника. Архив
содержит версионированный заголовок, отпечаток геометрии источника, каталог
по узлам и отдельные полезные нагрузки индексов источника. Ключи файлов хэшируются,
а не интерпретируются как пути, что исключает обход каталогов через имена моделей.
**Хэши FNV-1a обнаруживают случайное повреждение; они НЕ
аутентифицируют враждебные пакеты.** Подпись сборок/архивов относится к
системе упаковки контента. Импортированный исходный `ModelAsset`
пока остаётся в памяти CPU для планирования и контрольных проверок.

### Интеграция в рантайм

```cpp
render::GeometryScene scene;
scene.setStrataPortableRenderingEnabled(true);
scene.setStrataStreamingSceneBudgetBytes(8 * 1024 * 1024);
scene.setStrataIncrementalIndexStreamingEnabled(true);
scene.setStrataStreamingUploadBudgetBytes(128 * 1024); // bytes / source / update
scene.setStrataStreamingFrameUploadBudgetBytes(256 * 1024); // total / RHI frame
scene.setStrataDiskPageDirectory("Content/Strata");
scene.load(device, sceneJson, readFile);
```

Все сеттеры задаются до загрузки сцены. Архивы потребляет только
инкрементальный стриминг индексов GPU на Metal и Vulkan. GLES3/WebGL2
и GL33 продолжают использовать свой обычный переносимый откат с неизменяемым
sidecar и выбором на CPU, независимо от поддержки асинхронного диска.

Загрузка сцены проверяет точную идентичность архива/источника. Корневые страницы
загружаются синхронно один раз при инициализации (ошибка => этот источник
остаётся на Legacy). Остальные страницы по мере необходимости загружает нативный рабочий поток.
Запросы и проверенные готовые байты на CPU ограничены для каждого источника; старые
запросы в очереди/готовые отбрасываются, когда меняются требования камеры/теней.
Иерархия на CPU выбирает идеальные LOD, но загрузка на GPU ждёт,
пока **все братья** не будут готовы, а затем атомарно публикует новые
диапазоны узлов после проверяемых записей в устройство. Повреждённый или отсутствующий лист
не может стереть существующего родителя; любая ошибка загрузки в RHI сохраняет
прежний видимый фронтир.

Новый бюджет передачи на GPU для всей сцены учитывает успешные проверяемые
записи, даже если следующий брат завершился ошибкой, и при каждом предварительном проходе
меняет по кругу, какой источник первым получает доступ к оставшейся квоте. Он
сбрасывается на каждом новом серийном номере кадра RHI; если серийный номер кадра не поддерживается,
лимит применяется вместо этого к каждому предварительному проходу отрисовки. Это регулятор
инкрементальной загрузки Metal/Vulkan, а не измерение физической памяти GPU,
не ограничитель байт/с диска и не обещание снижения внутренней пропускной
способности копирования буферов в Vulkan.

**Код этапа 15.3 закоммичен, но ещё не собирался и не запускался нативно**.
Для формата архива, рабочего потока, интеграции в сцену и регулятора загрузки на GPU
написаны тесты на записывающем RHI для финального прохода проверки.
Полное освобождение индексов на CPU, общие глобальные рабочие потоки ввода-вывода, бюджеты
памяти между мирами, выбор, управляемый GPU в Vulkan, и LOD с потерями без трещин
остаются будущими этапами. Все точные коммиты и тестовые случаи см. в
`docs/agent-handoffs/strata-virtual-geometry.md`, этап 15.3.


## Этап 15.4 — общий координатор стриминга для нескольких миров (2026-11-20)

У STRATA теперь есть **не зависящий от жанра общий для процесса нативный пул дискового ввода-вывода**
и **резервирование бюджета стриминга индексов на уровне устройства**, общее для
нескольких одновременно загруженных `GeometryScene` / ECS-миров.
Прежние бюджеты на сцену и на источник остаются дополнительными верхними
границами. Путь игры/рендеринга по умолчанию не меняется, пока пользователь
не включит переносимый + инкрементальный дисковый маршрут Strata.

### Конфигурация (до загрузки любой сцены)

```cpp
// Configures one shared native background pool. Call once before the
// first archive request. Defaults: 2 threads and 64 MiB CPU ready cache.
aiverse::render::strata::configureSharedDiskIo(
    2, 64u * 1024u * 1024u);

render::GeometryScene worldA;
worldA.setStrataPortableRenderingEnabled(true);
worldA.setStrataStreamingIndexBudgetBytes(256u * 1024u);
worldA.setStrataStreamingSceneBudgetBytes(8u * 1024u * 1024u);
worldA.setStrataGlobalIndexBudgetBytes(128u * 1024u * 1024u);
worldA.setStrataIncrementalIndexStreamingEnabled(true);
worldA.setStrataStreamingUploadBudgetBytes(128u * 1024u);
worldA.setStrataStreamingFrameUploadBudgetBytes(256u * 1024u);
worldA.setStrataGlobalFrameUploadBudgetBytes(512u * 1024u);
worldA.setStrataDiskPageDirectory("Content/Strata");
worldA.setStrataDropArchivedCpuIndicesEnabled(true);
worldA.load(device, jsonA, readFile);

// worldB may be an entirely different ECS world/scene, but must use
// the SAME device-wide nonzero limits if loaded simultaneously.
// Its source allocations count toward worldA's global GPU index cap.
```

Общий для процесса исполнитель ввода-вывода по умолчанию использует до 2 рабочих потоков
(настраивается в диапазоне 1–8 *до* запуска любого рабочего потока), ограниченную глобальную
очередь заданий и ограниченный суммарный объём памяти **кэша проверенных готовых страниц**. Каждый
архив одновременно планирует не более одного общего задания; новый поток
не создаётся на каждый источник/мир. Отмена и выгрузка сцены очищают
страницы в очереди и готовые и освобождают их резервирования в общем кэше CPU.
Задачи рабочих потоков захватывают состояние архива со счётчиком ссылок, а не указатели на
сцену или исходный ModelAsset. Отменённый архив не может публиковать
в уничтоженную сцену.

Резервирование индексов на уровне устройства делается **до того**, как допущенный источник
выделит свой индексный буфер/арену этапа 15. Если источники второго мира
превышают общий предел, их бюджет индексов уменьшается там, где корни
ещё помещаются; иначе эти источники рендерятся исходной геометрией
Legacy. Хэндлы GPU и владение страницами остаются за исходным
`gfx::Device` и рендерером. Токены возвращаются только после уничтожения
sidecar; у разных объектов устройств независимые реестры.
Дополнительный предел проверяемой передачи страниц на GPU на уровне устройства применяется к
каждой сцене, разделяющей тот же серийный номер кадра RHI. Нулевой серийный номер запрещает
новые инкрементальные повышения при заданном пределе между мирами.
Загрузки корней во время загрузки сцены не подпадают под этот покадровый регулятор.

### Необязательная экономия памяти индексов на CPU

Когда архив сопоставлен с полным импортированным
`ModelAsset`, а его закреплённые корневые страницы индексов проверены и
загружены на GPU, включение
`setStrataDropArchivedCpuIndicesEnabled(true)` освобождает
**дублирующие подготовленные векторы индексов STRATA**. `Asset::logicalIndexCount()`
сохраняет неизменяемые границы проверки, и `planView` продолжает
работать без освобождённого вектора `indices`. Исходные
выделения вершин/индексов `Model` и откат на Legacy остаются нетронутыми.
Запросы диагностики/метаданных GPU/предпросмотра сохраняют CPU-копию индексов STRATA,
чтобы не рисковать недопустимым чтением. Это опциональное снижение памяти CPU
в установившемся режиме, **а не** полное удаление геометрии источника/модели и не
снижение пикового потребления памяти во время импорта.

### Ограничения и проверка

Общий предел GPU — это логический бюджет **зарезервированной арены индексов STRATA**,
а не точный предел физической VRAM или копий в полёте. Vulkan может сохранять
дополнительные теневые копии на CPU и версии на GPU; учёт общего ввода-вывода не включает
короткоживущие буферы чтения файлов и копии, отдаваемые вызывающему коду. Это
пока не дедуплицирует страницы, общие для разных ключей источников, не
предоставляет пределов между устройствами/физическими GPU, не выполняет косвенное
отсечение mesh-шейдерами и не решает проблему трещин LOD с потерями. GLES3/WebGL2 сохраняет существующий
маршрут неизменяемых буферов с выбором на CPU без поддержки нативных дисковых рабочих потоков.

**Код и новые регрессионные фикстуры закоммичены, но этап 15.4
не компилировался и не запускался нативно.** Цель
`aiverse_strata_streaming_coordinator_tests` покрывает аренды GPU для двух миров,
покадровые счётчики передачи между мирами и 24 экземпляра архивов,
разделяющих два рабочих потока. Тесты ModelAdapter/View/Disk дополнительно
проверяют освобождение подготовленных индексов на CPU после проверки. Полная
передача дел этапа 15.4 по файлам/коммитам/тестам:
`docs/agent-handoffs/strata-virtual-geometry.md`.


## Этап 16.1–16.2 — compute кластеров на GPU в Vulkan и кандидаты indexed-indirect

Этап 16 добавляет **опциональный compute-бэкенд Vulkan** в общий
интерфейс `gfx::Device` AIVERSE, используя отдельную компиляцию GLSL 450
через glslang/SPIR-V. Vulkan теперь поддерживает создание compute-конвейеров,
дескрипторы storage-буферов, `dispatchChecked`, барьеры для последующего
ввода indirect/вершин и асинхронное диагностическое чтение storage.
Storage-буферы остаются пригодными для ввода вершин **и** индексов из пула;
indirect-буферы, сформированные на GPU, имеют одновременно использование STORAGE и INDIRECT.

`GpuClusterCulling` повторно использует те же метаданные иерархии STRATA v2
и эталон паритета CPU, что и Metal, но на Vulkan выбирает compute-ядро
GLSL. Выбранный список ограничен 2048 записями кластеров на
диагностическую отправку; счётчики статуса GPU сигнализируют об искажённых метаданных
или переполнении вывода. Для формирования записей кластеров
или последующих indirect-данных на GPU чтение на CPU не требуется.

### Indirect-аргументы, сформированные compute в Vulkan

Отправка Vulkan теперь записывает собственные 20-байтовые
записи `gfx::DrawIndexedIndirectCommand` в массив RHI
`BufferType::Indirect`. Три compute-прохода на GPU
**clear → select → safety** выполняются вне графических проходов рендеринга.
Неиспользуемые слоты обнуляются (без отрисовки); неудачный выбор обнуляет все
команды. Необязательный, отдельно проверяемый путь для одного материала может
записать в slot0 одну команду отката **исходного полного примитива**,
чтобы переполнение на GPU не стирало молча весь источник.
`GpuSelectionSubmission::gpuIndirectPrepared()` указывает, что
буфер аргументов существует, **а не** что его материал/трансформации
одобрены для продакшн-растеризации.

`StrataVulkanIndirectContract.h/.cpp` предоставляет строгую предварительную проверку
источника/отправки только на чтение для одного жёсткого непрозрачного материала, одного
примитива/узла/экземпляра, VB/IB источника, разделяющих его выделение в пуле,
совпадающих ревизий модели и одного вида основной камеры (без дополнительных
теней/отражений). Неподдерживаемые/анимированные/прозрачные/многоматериальные
источники остаются на существующих маршрутах с выбором на CPU или Legacy.

### Статус, ограничения и проверка

| Пункт | Реализация | Аппаратное подтверждение |
| --- | --- | --- |
| Vulkan compute / GLSL450 → SPIR-V | Код написан | NOT RUN |
| Выбор кластеров на GPU в Vulkan | Код написан (диагностический) | NOT RUN |
| Indirect clear/select/safety на GPU в Vulkan | Код написан | NOT RUN |
| Асинхронное чтение диапазонов/indirect в Vulkan | Код написан | NOT RUN |
| Проверки пригодности источника/материала в Vulkan | Код написан | NOT RUN |
| Реальная эксклюзивная подмена отрисовки GPU в Renderer3D | НЕ СДЕЛАНО | N/A |
| Растеризация на GPU для нескольких материалов / теней / отражений | НЕ СДЕЛАНО | N/A |
| Переносимый откат на CPU для GLES3 | Сохранён | НЕ ПЕРЕПРОВЕРЕН |

Движок рендеринга **ещё не переключён на продакшн-путь отрисовки,
управляемый GPU в Vulkan**. Проверяемый API `Device::drawIndexedGpuIndirect`
доступен рендереру, но интеграция под владением Renderer3D
должна привязывать ровно то же состояние модели/материала/освещения/экземпляра и
сначала доказать паритет пикселей на GPU. На этом шаге для этапа 16 не запускались
ни тест драйвера Vulkan, ни тест компиляции шейдеров, ни offscreen-чтение, ни нативная сборка.
Новые тестовые фикстуры:
`strata_vulkan_kernel` (необязательный реальный GPU + SPIR-V),
`strata_gpu_culling` (записывающий бэкенд) и
`strata_render_routing` (строгая проверка материала/состояния).
Коммиты кода, явные ограничения и следующую передачу дел агенту см.
в разделах об этапах 16.1 и 16.2 в
`docs/agent-handoffs/strata-virtual-geometry.md`.


## Этап 16.3 — маршрут GPU indirect в Renderer3D на Vulkan для одного примитива (экспериментальный)

Теперь написан строго ограниченный **продакшн-путь Renderer3D** для
команд кластеров STRATA, сформированных на GPU в Vulkan. В отличие от
пакета этапа 16.2, предназначенного только для диагностики, `GeometryScene` может прикрепить одобренный
`GpuIndexedSelectionPacket` к своему **существующему** `ModelDraw` (один
на источник). Затем `Renderer3D::renderModels(opaque)` привязывает
исходный шейдерный конвейер модели, текстуры PBR-материала, освещение и
трансформацию экземпляра U0 через `drawModelBatch(...,submit=false)`.
Только после финальной проверки действительности кадра/ресурса/материала/мира
он вызывает команду indexed indirect RHI в Vulkan. При успехе обычная
исходная отрисовка **пропускается**; при любой ошибке тот же элемент
идёт по исходному пути отрисовки CPU/Legacy. Ни дублирующий владелец рендеринга,
ни второй распределитель вершин не создаются.

### Явное включение и приёмка паритета

```cpp
scene.setStrataStagingEnabled(true);
scene.setStrataGpuMetadataEnabled(true);
scene.setStrataGpuCullingEnabled(true);
scene.setStrataVulkanGpuIndirectEnabled(true);

// IMPORTANT: leave both FALSE until actual CPU/GPU selection parity
// and Renderer3D Vulkan pixel parity are verified for this exact build.
scene.setStrataVulkanGpuParityGates(false,false);
scene.load(device,sceneJson,readFile);
```

Маршрут **по умолчанию OFF**, а два отдельных рубежа паритета
никогда не включаются автоматически. Для активации оба должны быть явно
приняты после нативных A/B-проверок. Сейчас маршрут поддерживает
только ОДНУ модель-источник, ОДИН экземпляр сцены, ОДИН статический жёсткий непрозрачный
примитив/материал/узел, ОДИН активный основной вид и геометрию GPU
в пуле с совпадающими ревизиями модели. Никаких morph/skin/других авторских
LOD, батчей теней, shader graph, дополнительного состояния материалов или нестабильных
трансформаций. При включённых солнечных тенях или планарных отражениях выбор
на GPU отклоняется, и полный путь Legacy или переносимый путь CPU
остаётся авторитетным для того же источника во всех проходах.
Metal и GLES3 продолжают идти своими прежними путями.

Примитив RHI `Device::drawIndexedGpuIndirect` по-прежнему существует
для других проверенных применений, но маршрут Renderer3D этапа 16.3 отправляет
ограниченный массив из нескольких команд, сформированный на GPU, через свой существующий
вызов `drawIndexedIndirect`, так что представлены все видимые кластеры.
Цепочка clear/select/safety на GPU этапа 16.2 делает пустые и недействительные
слоты безвредными, а её явный откат может восстановить весь
исходный примитив, когда метаданные выбора недействительны.

Диагностика: `GeometryScene::Stats` показывает количество кандидатов GPU
в Vulkan, подготовленных пакетов и отказов; `ModelRenderStats`
показывает кандидатов Renderer3D, успешные отправки на GPU, отклонённые
пакеты и откаты на обычные отрисовки. Количество выходных треугольников GPU
остаётся неизвестным без асинхронного чтения и не угадывается по
LOD, выбранным на CPU.

**Реализация этапа 16.3 ещё не компилировалась и не тестировалась.**
Для копирования её пакетов и поведения пригодности написаны тестовые случаи
на записывающем устройстве, но перед включением
`setStrataVulkanGpuParityGates(true,true)` по-прежнему требуется фактический нативный A/B-фикстур
RGBA основной камеры Renderer3D Legacy против GPU. Несколько независимых
материалов/экземпляров, маршрутизация теней/отражений на GPU и оптимизированные
нагрузки страниц на GPU остаются дальнейшей работой по функциональности. Полная
передача дел по файлам/коммитам/API: `docs/agent-handoffs/strata-virtual-geometry.md`,
этап 16.3.

## Этап 16.4 — ограниченный GPU-рендеринг с несколькими материалами, экземплярами и камерами

Экспериментальный путь Vulkan Renderer3D теперь поддерживает **до 16
непрозрачных исходных примитивов/материалов** в одном неизменяемом меше
из общего пула и **до 16 исходных экземпляров** (дополнительно
ограничено общим пулом из 64 compute-отправок на кадр). GPU-пакеты строго
привязаны к исходному примитиву и к экземпляру. Renderer3D сохраняет
исходное состояние PBR-текстур и материалов и одно преобразование U0 на
ModelItem; он никогда не объединяет разные материалы в одно состояние
и не отправляет один и тот же примитив дважды. Несовместимые авторские LOD,
деформация/скиннинг, прозрачность или ревизии GPU-ресурсов всегда
уходят на существующую Legacy-геометрию или геометрию, выбранную CPU в портативном пути.

### Выборки GPU для каждой камеры

Основная камера и солнечная тень получают независимые выборки до
`Renderer3D::render`. Камеры прожекторных теней и зеркального планарного
отражения воды окончательно определяются только позже, в
`Renderer3DReflect.cpp`: STRATA теперь выполняет ограниченную GPU compute-
выборку **после вычисления фактической матрицы прожектора/зеркала, но
до начала каждого GPU-прохода рендеринга**. Перед использованием любого
GPU indirect-буфера рендерер проверяет идентичность прохода, размеры цели,
исходный материал/преобразование и текущий кадр RHI. Если compute
не удался, тот же исходный ModelItem рисуется через Legacy
в этом проходе. Ничто не выполняет синхронное чтение с GPU в
рабочем рендерере. Metal/GLES3/GL33 остаются на своих устоявшихся
путях выборки и fallback.

### Явное включение; никогда не включать непроверенные проходы по умолчанию

```cpp
scene.setStrataStagingEnabled(true);
scene.setStrataGpuMetadataEnabled(true);
scene.setStrataGpuCullingEnabled(true);
scene.setStrataVulkanGpuIndirectEnabled(true);

// All stay FALSE until actual native A/B pixel/depth parity is verified
// for this precise source SHA, device and shader configuration.
scene.setStrataVulkanGpuParityGates(false,false);
scene.setStrataVulkanGpuAuxiliaryParityGates(
    false,  // sun shadow depth
    false,  // spot shadow depth
    false); // planar-reflection RTT image
```

Два базовых флага приёмки охраняют совпадение исходной выборки GPU/CPU и
растра основного изображения; остальные три независимо подтверждают глубину
солнечной/прожекторной тени и зеркальное отражение. Установка флагов лишь
фиксирует ручную приёмку: она **не** выполняет и не доказывает тест.
Сцена, запрашивающая солнечную тень или отражения воды, отказывается от
GPU-маршрутизации источников, пока соответствующий флаг прохода не принят;
прожекторные тени могут независимо оставаться на Legacy. `Renderer3D`
ведёт счётчики GPU-кандидатов, подготовки и отправленных отрисовок
для всех камер; число треугольников на GPU нельзя вывести без
асинхронного чтения.

**Статус проверки:** все реализации Stage16.4 закоммичены,
но CMake и CTest НЕ запускались, а реальное пиксельное совпадение растра,
глубины и RTT на Vulkan НЕ проверено. Записывающие фикстуры расширены
для двух отдельных непрозрачных материалов, независимых преобразований экземпляров,
правильной идентичности пакетов main/sun/spot/reflection, уникальных
compute-слотов Vulkan, отклонения альфы и инвалидации по ревизии материала.
Эти тесты также остаются **невыполненными**. Для рабочей приёмки
требуется реальная A/B-фикстура Vulkan Renderer3D legacy/GPU, покрывающая
точные цвета, тени и планарный RTT; до общего финального набора владельца
нужно завершить Stage17 (LOD с потерями без трещин) и Stage18 (профилирование).
Полные передаточные записи Stage16.4a/b/c по файлам и тестам находятся в
`docs/agent-handoffs/strata-virtual-geometry.md`.


## Этап 17 — сертификация контуров LOD смешанной глубины, устойчивых к трещинам (2026-11-20)

Офлайн/импортный сборщик STRATA теперь выполняет **точные геометрические
проверки сохранения границы**, а не полагается только на
параметр `meshopt_SimplifyLockBorder`. Кандидат в более грубого родителя
принимается, только если сегменты его ориентированных рёбер периметра точно совпадают
с периметром ОБЪЕДИНЕНИЯ его более детальных потомков, с теми же
исходными ID вершин и обходом. Если хотя бы один сегмент удалён, разбит,
продублирован или меняет ориентацию, сборщик **не публикует
этого родителя**: его потомки остаются в отрисовываемой иерархии.
После построения независимая функция
`strata::auditSeamHierarchy(asset,report,error)` проверяет каждую
связь родитель-потомок. Только полностью успешная проверка устанавливает
`Asset::seamCertified=true`. Владение мешем/текстурой/материалом и
исходные неизменённые треугольники LOD0 остаются прежними.

### Через UV-швы, материалы и исходные меши

Поскольку швы материалов/UV могут дублировать ID вершин в *одной и той же
физической позиции*, функция уровня источника
`strata::auditModelSourceSeams(model,modelAsset,report,error)`
выполняет ВТОРУЮ, импортную геометрическую сертификацию границы.
Она сравнивает точные концы сегментов в мировом/bind-пространстве между
подходящими разбиениями по материалам и независимо преобразованными жёсткими
мешами Model. Ограниченный трёхмерный BVH находит концы, попадающие
внутрь граничного сегмента другого материала (существующее T-соединение),
и позиции, очень близкие, но не идентичные концам границы соседнего
материала (возможные микротрещины). Подозрительные
источники используют Legacy вместо заявления о безопасном LOD смешанной глубины.
Этот проверщик геометрии **не сдвигает вершины и не переписывает UV**;
исходные позиции уже должны совпадать для точной сертификации.

Полный API `buildModelAsset` возвращает
`ModelAsset::sourceSeamsCertified=true` только после того, как все разбиения
прошли проверки швов на уровне источника, тогда как каждый отдельный
`Asset::seamCertified` охватывает собственную иерархию родитель-потомок.
`SceneStaging` отказывается от несертифицированных ассетов, а
`GeometryScene` требует оба сертификата, прежде чем перейти к
портативному рендерингу Metal/Vulkan/GLES или Vulkan indirect GPU.
Статистика включает число сертифицированных источников/мешей по разбиениям,
отклонённых небезопасных грубых родителей, успешно сопоставленных физических
границ материалов и отклонённых источников.

### Гарантии, ограничения и проверка

Гарантируется **топологическая непрерывность сегментов для совместимой,
уже правильно сшитой статической исходной геометрии**, а не полная
пиксельная идентичность геометрии с потерями, непрерывность нормалей/UV-шейдинга,
плавный морфинг LOD или исправление трещин источника. Отдельно созданные
источники `Model`, другие ECS-миры, независимо сгенерированные чанки
ландшафта и анимированные/скиннированные/деформируемые поверхности не охватываются
текущей проверкой источника. Очень близкие отдельные поверхности могут отклоняться
консервативно. Новая проверка источника имеет предел в 250 000 граничных рёбер
и выполняется только при импорте/загрузке, а не в каждом кадре.
Обычный CPU/Legacy-рендерер сохраняется всякий раз, когда проверка
отклоняет источник.

Новые CPU-цели CTest `strata_seams` (внутри материала,
доказательство родитель/потомок смешанной глубины, T-соединения, немногообразные рёбра)
и `strata_source_seams` (шов материала с дублированными вершинами,
разные преобразованные меши, разбитое физическое ребро и зазор от малого смещения)
были **написаны, но НЕ ЗАПУСКАЛИСЬ** в Stage17. Ветка
не компилировалась нативно и не рендерилась после этих изменений.
По-прежнему требуется фактическая приёмка пикселей/глубины Vulkan/Metal/GLES3 и
визуальная приёмка смешанных LOD. Полная архитектура, конкретные коммиты,
тестовые случаи и ограничения:
`docs/agent-handoffs/strata-virtual-geometry.md`,
Stage17.1 и Stage17.2.

**Следующая функциональная работа:** необязательный стабильный гистерезис LOD и смягчение
«хлопков», координация границ ландшафта из нескольких источников, оптимизации
производительности Stage18 и единый общий финальный прогон владельца —
сборка и приёмка на GPU. НЕ включайте ручные флаги приёмки Vulkan
только на основании этого офлайн-сертификата CPU.


**Уточнение Stage17.2 для UV-островов:** пространственная проверка источника также
обрабатывает несвязанные по рёбрам треугольников компоненты внутри *одного*
материала/примитива. Разделённый UV-остров с дублированными ID вершин
сертифицируется, только когда его точный физический контур встречается с соседним
островом, а существующие T-соединения на подрёбрах/близкие зазоры вызывают
переход на Legacy. Два компонента, касающиеся в одной вершине, не считаются
ошибочно полностью связанными по рёбрам. Это уточнение и его регрессионная
фикстура закоммичены, но всё ещё **не скомпилированы и не выполнены**.


## Этап 17.3 — временная стабильность LOD (портативный CPU-селектор)

В AIVERSE STRATA теперь есть **включаемый по желанию временной гистерезис LOD
для каждого экземпляра и каждой камеры** в общем портативном индексированном рендерере на
Metal, Vulkan, GLES3/WebGL2 и desktop GL. Он стабилизирует
в противном случае частое переключение родитель/потомок, когда проецируемая
геометрическая ошибка колеблется около одного порога.

```cpp
render::GeometryScene scene;
scene.setStrataPortableRenderingEnabled(true);
const bool ok=scene.setStrataLodHysteresisFraction(0.20f);
scene.load(device, sceneJson, readFile);
```

Значение проверяется в диапазоне **0..0.45**; ноль — значение по умолчанию, при нём
сохраняются все прежние эталонные пороги и поведение памяти.
Ранее детализированный узел может оставаться детальным, пока его проецируемая
ошибка не упадёт ниже `threshold * (1-h)`, а ранее
грубый узел детализируется только после того, как ошибка превысит
`threshold * (1+h)`. У первого наблюдения **нет особого
смещения**: используется исходный порог.

История принадлежит экземпляру сцены, а не его
общему ModelAsset. Отдельные слоты исходного узла, примитива и камеры
отслеживаются независимо (основная камера, солнечная тень и т. д.).
Каждое обновление фиксируется только после полностью успешного
`PortableSource::prepareModel`; любой сбой выделения на GPU,
отсутствие резидентности потомка или повреждённый источник сохраняют прежнюю
историю и используют существующий fallback на родителя/Legacy. Изменение
политики, выгрузка источника или временное использование
постороннего прокси сбрасывает его историю. Неизвестные планарные отражения,
выбираемые поздно, по-прежнему используют **самый детальный резидентный фронтир**
и сбрасывают временное состояние, чтобы не пропустить отражённую геометрию.

Планировщик запросов страниц Stage15 также читает ТО ЖЕ предыдущее
состояние детализации: детальный фронтир, удерживаемый гистерезисом, продолжает
запрашивать страницы своих потомков, даже если немодифицированная мгновенная
CPU-выборка предпочла бы родителя. Это предотвращает лишнее
вытеснение/перезагрузку страниц GPU. Ни один из этих механизмов
не вводит отдельного владельца отрисовки и не меняет точные статические
сертификаты швов на границах источника из Stage17.1–17.2.
`GeometryScene::Stats` показывает сохранённые временные истории
и успешные переключения состояния детализации.

**Известные ограничения:** это стабилизация временной выборки, а не
геоморфинг вершин, затухание, сшивание ландшафта между источниками или
плавный шейдинг между разными внутренними областями с потерями. GPU-compute
выборка Vulkan/Metal из Stage16 по-прежнему использует исходную
политику без состояния; настоящие временные буферы GPU и совпадение CPU/GPU
в холодном/тёплом режиме — будущие задачи Stage17.4. Никаких нативных прогонов CMake,
CTest, запусков на оборудовании Metal/Vulkan/GLES3 или бенчмарка
времени кадра LOD для этого кода не проводилось. Расширенные существующие
фикстуры тестов `strata_view` и `strata_portable`
**написаны, но не выполнены**. См. передаточную запись Stage17.3 в
`docs/agent-handoffs/strata-virtual-geometry.md`.


## Этап 17.4 — временной гистерезис LOD только на GPU (Metal + Vulkan)

Портативный CPU-гистерезис LOD из Stage17.3 теперь имеет соответствующую
реализацию только на GPU для существующих compute-селекторов кластеров Metal/Vulkan.
Одна и та же настройка сцены управляет
полосой гистерезиса и для CPU, и для GPU:

```cpp
scene.setStrataLodHysteresisFraction(0.20f);

// These earlier flags are required for experimental Vulkan GPU raster.
// Keep all acceptance flags FALSE until passing real device parity:
scene.setStrataVulkanGpuIndirectEnabled(true);
scene.setStrataVulkanGpuParityGates(false,false);
scene.setStrataVulkanGpuAuxiliaryParityGates(false,false,false);

// NEW: independent acceptance for GPU temporal (warm-history) parity.
// Stateless GPU parity is NOT sufficient.
scene.setStrataGpuTemporalParityVerified(false);
```

### Как работают истории на GPU

`GpuClusterCulling` хранит независимые маски в GPU Storage для
каждой идентичности исходных метаданных, исходного примитива, токена экземпляра сцены,
камеры/прохода (main/sun/spot/planar reflection) и ревизии модели/политики.
Завершённая тёплая история позволяет ранее детализированному узлу
огрубляться, только когда проецируемая ошибка падает ниже
`threshold*(1-h)`, а ранее грубому узлу детализироваться только
выше `threshold*(1+h)`. Холодный старт использует исходный порог.
История отбрасывается при изменении ревизии источника/политики, изменении
viewport/clip-depth камеры или пропуске более двух эпох RHI.

Маски существуют **только на GPU** и тройно буферизованы по серийному номеру кадра RHI.
Маска предыдущего кадра остаётся только для чтения, пока текущий
кадр пишет в другой буфер. Ни один находящийся в работе слот не переиспользуется без
барьера завершения. Три compute-шага сбрасывают маску-кандидат,
выполняют тот же обход иерархии с гистерезисом и восстанавливают
предыдущую маску, если выборка сообщает о неверных метаданных или переполнении.
Никакое синхронное чтение с GPU не влияет на рендеринг; асинхронное чтение
зарезервировано для диагностики и тестовых фикстур.

У одного селектора есть явные пределы: до 262 144 узлов в его
метаданных, 256 независимых ключей временной истории и 32 MiB
логических масок в GPU Storage. При превышении пределов экспериментальная
временная GPU-отправка отклоняется; остаётся исходный портативный CPU/Legacy.
Эти пределы не учитывают дополнительные физические версии RHI,
исходную геометрию, indirect-аргументы или другие миры.

Для пользователей, вызывающих `GpuClusterCulling::dispatch` напрямую,
`GpuPassConfig` теперь предоставляет `hysteresisFraction`,
`temporalNodeCount`, `temporalPassId` и
`temporalRevision` (все нужны при h>0). На одну временную историю
может диспатчиться только один включённый экземпляр. 112-байтовая
`GpuSelectionUniforms` сохраняет все прежние 96 байт
ABI шейдера. `GpuSelectionSubmission::temporalApplied`
означает, что закодирована приемлемая последовательность GPU-команд, а не
что доказано совпадение на оборудовании. `GpuTemporalStats` и
`GeometryScene::Stats` показывают память масок, источники/камеры,
холодные старты и отклонения по бюджету.

### Тесты и оставшаяся работа

Регрессии на записывающем бэкенде написаны для независимых
историй камер, изменений ревизии, защиты барьером GPU для трёх кадров,
предела 32 MiB и поведения по умолчанию без состояния.
Реальные offscreen-фикстуры Vulkan на GPU ставят в очередь четыре последовательных
кадра и сравнивают асинхронно возвращённые выбранные кластеры
и маски детализированных узлов с холодным/тёплым эталоном CPU.
Отдельный включаемый по желанию runner для macOS `aiverse_strata_metal_temporal`
покрывает первые три последовательных временных кадра GPU
на реальном Metal. Они **ещё НЕ скомпилированы и не запускались**.
`AIVERSE_STRATA_METAL_GPU_TESTS=ON` собирает этот нативный Metal-
runner. Совпадение пикселей/глубины, теней и отражений для временного
режима Metal/Vulkan пока не сертифицировано. GLES3/GL33/WebGL2 по-прежнему
используют CPU/портативный гистерезис Stage17.3, без GPU compute.

Этот этап стабилизирует **какая геометрия LOD рисуется**; он не
морфит вершины, не делает переходы между внутренними поверхностями, не согласует
несвязанные чанки ландшафта и не гарантирует непрерывный шейдинг нормалей/UV.
Сертификация границ Stage17.1/17.2 остаётся
обязательной. Следующий Stage18 охватывает производительность, число дескрипторов/энкодеров,
пропускную способность GPU для масок, память/пиковое использование и сравнение времени кадра
с базовой линией без гистерезиса; Stage19 — это
единая финальная полная сборка, CTest и приёмка на реальном оборудовании.
Точные коммиты и передаточная запись для следующего агента:
`docs/agent-handoffs/strata-virtual-geometry.md`, Stage17.4.


## Навык движка и авторинг через MCP (2026-11-20)

Навык движка — [aiverse-strata](../skills/visuals/aiverse-strata/SKILL.md),
он зарегистрирован в `skills/catalog.json` (перегенерировать командой `python3 tools/skills/catalog.py generate`).
Он поставляется вместе с движком; глобальная установка навыка Codex не требуется.

Пересоберите `aiversemcp` и редактор, чтобы открыть общие операции Workspace:

- `strata_capabilities {}` сообщает, включает ли этот исполняемый файл сборщик,
  пределы анализа и пути к документации. `gpuVerified: false` означает, что эта команда
  не проверяет GPU; это не проверка возможностей устройства.
- `strata_model_analyze {"model":"models/scenery.glb"}` требует открытого проекта.
  Он загружает фактический источник и вызывает `buildModelAsset` с параметрами по умолчанию,
  сообщая о подходящих/пропущенных примитивах, точных причинах пропуска, числе узлов/meshlet,
  байтах вспомогательных индексов и сертификатах швов источника/примитивов. Не происходит
  никаких правок сцены, записи архивов, GPU-ресурсов или изменения флагов.

Анализ допускает не более 250000 вершин, 750000 индексов и 64 MiB суммарного
чтения файлов. Все пути зависимостей должны находиться внутри проекта, включая разрешённые
символические ссылки. Эти границы ограничивают вход импортера и работу сборщика, а не память
декодированных изображений. `AIVERSE_BUILD_STRATA=OFF` сохраняет обнаружение возможностей и возвращает
явную ошибку недоступности для анализа. Подходность для CPU и сертификация швов
не являются визуальным совпадением на GPU, числом вызовов отрисовки, использованием VRAM или измеренным ускорением.

Используйте существующее MCP-подключение AIVERSE. stdio-сервер может выполнять эти операции Workspace
локально без редактора (`--no-editor`); при подключении вызовы используют
реестр живого редактора. Перезапустите/пересоберите обе стороны, чтобы избежать устаревших списков инструментов.

## Отладочная визуализация кластеров и геометрии (2026-11-21)

`engine/render/StrataDebugView.{h,cpp}` (в `aiverse_strata_portable`) рисует
**фактически выбранный** портативный фронтир в виде оверлея. Это только
представление: он потребляет `PortableSource::debugSelection()` — именно те кластеры, которые
последний успешный `prepareModel` записал в алиас, — и выдаёт непосредственные
примитивы `Renderer3D::line/triangle`. Он никогда не выбирает LOD, не меняет
маршрутизацию, не загружает данные на GPU и не читает выборки с GPU. По умолчанию OFF.

```cpp
scene.setStrataPortableRenderingEnabled(true);
scene.setStrataDebugGeometryCaptureEnabled(true); // BEFORE load: wireframe/fill
scene.load(device, json, reader);
strata::DebugViewOptions debug;
debug.enabled = true;
debug.clusterBounds = true;        // AABB of each selected cluster ("chunk")
debug.clusterWireframe = true;     // triangle edges of each selected cluster
debug.clusterFill = false;         // translucent per-cluster colour
debug.instanceBounds = true;       // instance box coloured by route
debug.color = strata::DebugColorMode::LodDepth; // Cluster | LodDepth | Error | Material
scene.setStrataDebugView(debug);   // runtime toggle, false for invalid options
// after scene.draw(renderer): scene.strataDebugStats()
```

| Режим цвета | Значение |
| --- | --- |
| `Cluster` | стабильный хэш на (примитив, узел); соседние чанки различаются |
| `LodDepth` | глубина иерархии: красный = грубый корень … синий = самый детальный лист |
| `Error` | лог-нормированная `geometricError` в пространстве объекта: красный = грубо, синий = детально |
| `Material` | подготовленный примитив / разбиение по материалу |

Рамки экземпляров: зелёный = портативный STRATA, голубой = Vulkan GPU indirect (только
границы; для этого маршрута нет CPU-фронтира), оранжевый = подготовленный источник STRATA,
который в этом кадре ушёл на legacy, серый = экземпляр не STRATA
(`includeLegacy=false` скрывает их).

Ограничения и правила честности:
- Каркас/заливка требуют CPU-позиций, которые `uploadModel` освобождает. Захват
  копирует позиции `float3` только источников STRATA
  (`strataDebugGeometryBytes()`); без него, или после того как
  `setStrataDropArchivedCpuIndicesEnabled` отбросил CPU-индексы, рисуются только границы,
  а `DebugDrawStats::wireframeUnavailable` считает такие кластеры.
- Пределы на кадр `maxLines` / `maxTriangles` (по умолчанию 262144 / 131072) общие
  для всех экземпляров; отброшенные примитивы считаются в `truncated`.
- Оверлей показывает единый консервативный фронтир, используемый для main + солнечной
  тени (+ самый детальный для отражений), а не срез только для основной камеры.
- Прокси групп HLOD и прокси поверхностей не являются отрисовками STRATA и не
  показываются как кластеры.
- Ни один игровой хост или вьюпорт редактора сейчас не включает портативный STRATA, поэтому
  оверлей доступен только из кода, который настраивает `GeometryScene`
  (фикстуры, собственные хосты). Переключателя в MCP/редакторе пока нет.

Проверка: CTest `strata_debug_view` (записывающий RHI + записывающий приёмник:
транзакционный захват, OFF по умолчанию, счётчики границ/каркаса/заливки, пределы,
случаи с архивированными индексами и отсутствующими позициями, детерминизм цвета/маршрута).
Пиксели на экране не проверялись ни на одном бэкенде.
