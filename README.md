# AlternativeI — Process Hacker analog (Windows + Linux)

Кроссплатформенный аналог Process Hacker / System Informer: менеджер процессов,
потоков, хендлов, модулей, сети и служб, плюс отдельная CLI-утилита для
посекторного backup/restore MBR/GPT/дисков.

## Архитектура

- `src/core/` — платформонезависимое ядро, каждый интерфейс реализован отдельно
  под Windows (`src/core/windows/`) и Linux (`src/core/linux/`), выбор backend'а
  по `#ifdef` на этапе компиляции через `*Factory.cpp`:
  - `IProcessProvider` — список процессов + on-demand детали (потоки, модули,
    память, хендлы, окружение, сеть). Windows: Toolhelp32/PSAPI +
    `NtQuerySystemInformation`. Linux: `/proc`.
  - `ISystemMonitor` — общесистемные CPU/память/диск/сеть. Windows:
    `GetSystemTimes`/`GlobalMemoryStatusEx`/PDH. Linux: `/proc/stat`,
    `/proc/meminfo`, `/proc/diskstats`, `/proc/net/dev`.
  - `IServiceManager` — список/старт/стоп/рестарт служб. Windows: Service
    Control Manager. Linux: `systemctl`.
- `src/gui/` — Qt Widgets: дерево процессов (parent-child, как в Process Hacker),
  диалог свойств процесса (Threads/Modules/Memory/Handles/Network/Environment),
  вкладки Performance (графики) и Services.
- `src/diskutil/` — отдельная CLI-утилита без Qt-зависимости для посекторного
  backup/restore MBR/GPT/дисков. См. раздел ниже.

## Статус

Реализовано и протестировано:
- [x] Дерево процессов (parent-child), поиск/фильтр (рекурсивный), сортировка
- [x] Terminate, изменение приоритета
- [x] Диалог свойств процесса: Threads, Modules, Memory regions, Handles,
      Environment, Network — на Linux проверено на реальных процессах
      (тесты, живые TCP-соединения, pipes/eventfd)
- [x] Performance-вкладка: графики CPU/память/диск I/O/сеть с автомасштабированием
- [x] Services-вкладка: список + Start/Stop/Restart
- [x] `diskutil`: info/backup/restore для MBR/GPT/произвольного LBA-диапазона/
      целого диска, с обязательной SHA-256 верификацией — проверено на реальном
      GPT-образе и на loop-устройстве (см. ниже)

Написано, но не собрано/не протестировано (нет Windows-тулчейна в этом окружении):
- Все Windows-бэкенды (`src/core/windows/*`) — реализованы по документированным
  и полу-документированным (`NtQuerySystemInformation`/`NtQueryObject`, как в
  самом Process Hacker) API, но требуют сборки и проверки на реальной Windows.

Дальше (по приоритету):
- [ ] Windows: подписанный kernel-mode драйвер (аналог KPH) для операций,
      недоступных из user-mode — принудительное закрытие защищённых хендлов,
      резолвинг имени хендла без риска зависания (см. `docs/windows-driver.md`)
- [ ] Резервное копирование secondary GPT (в конце диска), сейчас `--region gpt`
      захватывает только primary-копию в начале
- [ ] Установщик/упаковка (см. `docs/packaging.md`)

## Сборка GUI-приложения

### Linux

```sh
sudo apt install qt6-base-dev cmake ninja-build build-essential
cmake -S . -B build -G Ninja
cmake --build build --target alttools
./build/alttools
```

### Windows

Требуется Qt6 (MSVC kit) и Visual Studio / CMake:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -DCMAKE_PREFIX_PATH="C:\Qt\6.x\msvc2019_64"
cmake --build build --config Release --target alttools
```

## diskutil: посекторный backup/restore

Отдельный исполняемый файл, без зависимости от Qt:

```sh
cmake --build build --target diskutil
```

```
diskutil info <device-or-image>
diskutil backup --region {mbr|gpt|disk|custom} [--start-lba N --sectors N] \
                 <device-or-image> <output-file>
diskutil restore --region {mbr|gpt|disk|custom} [--start-lba N] [--yes] \
                  <input-file> <device-or-image>
```

- `--region mbr` — первые 512 байт (классический MBR или protective MBR у GPT).
- `--region gpt` — protective MBR + primary GPT-заголовок + таблица разделов
  (по факту читает заголовок, чтобы вычислить точный размер региона).
- `--region disk` — весь диск/образ целиком.
- `--region custom --start-lba N --sectors N` — произвольный диапазон секторов.

**Правила безопасности, встроенные в инструмент, а не в документацию:**
- `backup` — только чтение, всегда безопасен.
- `restore` **без `--yes` ничего не пишет** — печатает план (куда, какой байтовый
  диапазон, SHA-256 источника) и завершается.
- `restore --yes` пишет данные, затем **перечитывает записанное и сравнивает
  SHA-256 с исходным образом** — расхождение считается фатальной ошибкой
  (ненулевой exit code), а не предупреждением.
- Запись образа большего размера, чем целевое устройство/регион, отклоняется
  до какой-либо записи.
- Запись в блочное устройство сопровождается отдельным явным предупреждением
  в выводе.

Проверено end-to-end в этом окружении на реальном GPT-образе (создан `parted`,
сверен с `sgdisk -p`) и на подключённом через `losetup` loop-устройстве:
`info` корректно читает MBR/GPT (диск GUID, разделы, типы, имена, совпадают с
`sgdisk` побайтово); `backup --region gpt`/`--region disk` + порча региона +
`restore --yes` восстанавливают диск до состояния, идентичного оригиналу
(побайтовое совпадение SHA-256 всего файла); dry-run подтверждённо не меняет
целевой файл; восстановление образа большего размера, чем устройство,
корректно отклоняется.

**Дисковый модуль не тестировался на реальном физическом железе** — только на
файлах-образах и loop-устройствах в этом контейнере. Прежде чем указывать
инструменту на настоящий `/dev/sdX` или `\\.\PhysicalDriveN`, потренируйся на
виртуальных дисках/образах и убедись, что понимаешь, какой именно диапазон
байт будет перезаписан.
