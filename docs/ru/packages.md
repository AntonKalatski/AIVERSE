# Пакеты: дополнительный контент из каталога

В репозитории движка — код и минимум ассетов. Тяжёлый дополнительный контент (библиотеки
PBR-материалов, их исходники, шаблоны проектов, наборы моделей, звук, эффекты) лежит в каталоге
пакетов [AIVERSE-PLUGINS](https://github.com/chromealex/AIVERSE-PLUGINS). Проект скачивает только
то, что использует.

| Что | Где |
|---|---|
| Каталог: пакеты по разделам (`materials/`, `textures/`, `templates/`, `models/`, `audio/`, `vfx/`, `ui/`, `scripts/`, `plugins/`, `tools/`) | AIVERSE-PLUGINS, `<раздел>/<id>/aiverse.package.json` |
| Архивы `<id>-<версия>.zip` | GitHub-релизы `<id>@<версия>` репозитория AIVERSE-PLUGINS |
| Индекс (его читают движок и сайт) | `index.json` в его `main`, пишет workflow `publish` |
| Клиент | `tools/pkg/aiverse_pkg.py` (Python, только стандартная библиотека; `--json` у всех команд) |
| Кэш на компьютере | `~/.aiverse/packages/<id>/<версия>/` (`AIVERSE_PACKAGES_HOME`), скачанное проверяется по SHA-256 |
| Сайт | страница **Каталог** (`catalog.html`): превью, описание, минимальная версия движка, скачивание |

## Как пользоваться

```sh
python3 tools/pkg/aiverse_pkg.py search terrain              # ещё --category materials, --kind content
python3 tools/pkg/aiverse_pkg.py info terrain-pbr
python3 tools/pkg/aiverse_pkg.py add terrain-pbr --project ~/Games/MyGame
python3 tools/pkg/aiverse_pkg.py status --project ~/Games/MyGame
python3 tools/pkg/aiverse_pkg.py install --project ~/Games/MyGame   # после клонирования: всё из aiverse.lock
python3 tools/pkg/aiverse_pkg.py verify --project ~/Games/MyGame    # код 1, если установленные файлы изменены
python3 tools/pkg/aiverse_pkg.py remove terrain-pbr --project ~/Games/MyGame
python3 tools/pkg/aiverse_pkg.py fetch terrain-pbr-sources   # исходники для запекания: печатает папку в кэше
python3 tools/pkg/aiverse_pkg.py create <id-шаблона> ~/Games/New   # новый проект из пакета-шаблона
```

- Пакеты **`content`** копируются в проект в папку `install.mount` (у `terrain-pbr` —
  `materials/terrain`); `install.mergeJson` добавляет их библиотеку материалов в `materials.json`
  проекта. Id, которые проект уже использует с другим значением, не перезаписываются (`add`
  останавливается; `--force` перезаписывает). Файлы становятся частью проекта, поэтому сборке
  не нужна сеть, а `aiversebuild` пакует только то, что лежит в проекте.
- `game.json` → `"packages": {"terrain-pbr": "^1.0.0"}` — нужные диапазоны версий; `aiverse.lock`
  рядом фиксирует точную версию, хеш архива и хеш каждого установленного файла. Коммитьте оба файла.
- Пакеты **`source`** (входные данные запекания) и другие данные инструментов в проект не копируются:
  инструменты вызывают `aiverse_pkg.package_dir('<id>')` и получают папку в кэше (скачивается один раз).
- Пакеты **`template`** — целые папки проектов: `create` копирует такой пакет и ставит пакеты из его
  `game.json`.
- Диапазоны версий: точная `1.2.3`, `^1.2` (та же старшая), `~1.2.3` (та же средняя), `>=1.0.0`, `*`.
  У каждого пакета есть `minEngine`; `add` предупреждает, если этот checkout движка старше.
- Без сети: `AIVERSE_PACKAGES_OFFLINE=1` берёт только кэш и последний скачанный индекс.
- C++-инструменты и редактор находят пакеты в кэше через `engine/pack/Packages.h`
  (`aiverse::pack::cachedPackageDir("vegetation-pbr")`). Игра кэш не читает.

## Что вынесено из репозитория движка (2026-11-22)

| Пакет | Было | Осталось в движке |
|---|---|---|
| `terrain-pbr` (материалы, 14 PBR-материалов земли) | `engine/assets/materials/terrain` | только шахматка (для пустых проектов) |
| `vegetation-pbr` (материалы: кора, атласы листвы, проекция коры, зима) | `engine/assets/materials/vegetation` | — |
| `terrain-pbr-sources` (текстуры) | `tools/terrain/pbr_sources` | — |
| `vegetation-pbr-sources` (текстуры) | `tools/vegetation/pbr_sources` | — |

Инструменты, которые читали эти папки, теперь берут их из каталога: `tools/terrain/install_pbr_pack.py`
(то же, что `add terrain-pbr`), `bake_pbr_pack.py --root <копия содержимого пакета>` (исходники из
каталога), `build_detail_materials.py`, `bake_landscape_pbr.py`, `tools/vegetation/*`. Превью леса
TerraWeave в редакторе берёт текстуры `vegetation-pbr` из проекта, иначе из кэша.

## Навыки указывают нужные пакеты

Навык репозитория перечисляет нужные пакеты во frontmatter:

```
---
name: aiverse-terra-vegetation
description: ...
packages: vegetation-pbr, terrain-pbr
---
```

`python3 tools/skills/catalog.py search|plan` показывает их (`contentPackages`, в `plan` — готовые
команды установки), а `python3 tools/pkg/aiverse_pkg.py skill <id-навыка> --project P` ставит их.
Агенты: прежде чем выполнять такой навык в проекте, поставьте его пакеты; файлы пакетов в репозиторий
движка не копируются.

## Добавить или обновить пакет

Pull request в AIVERSE-PLUGINS (его `CONTRIBUTING.md`); владелец проверяет и принимает. CI проверяет
манифест, превью, лицензию и содержимое; после слияния `publish` выпускает версию и обновляет
`index.json`, который движок и сайт читают напрямую. Маленькое содержимое лежит в `files/` пакета,
большое — `archive` по https или `source` (папка публичного GitHub-репозитория на зафиксированном
коммите). Опубликованная версия не меняется: для изменений нужна новая `version`. Тяжёлые ассеты
в репозиторий движка больше не коммитятся.
