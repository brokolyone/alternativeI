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

### Известная проблема: Windows Defender помечает `.exe`/инсталлятор как троян

Готовые сборки из GitHub Releases (`alttools-*-win64.exe`, `alttools.exe`,
`diskutil.exe`) могут детектиться Defender'ом как
`Trojan:Win32/Sabsik.FL.A!ml` (или похожий `!ml`-детект). Это **облачная
ML-эвристика**, а не сигнатура конкретного вредоноса — суффикс `!ml`
означает "похоже на вредонос по поведенческим признакам", не "найден
известный троян". Известная причина, а не баг:

- Бинарники **не подписаны** (нет Authenticode-сертификата) и не имеют
  накопленной репутации в облаке Microsoft SmartScreen/Defender — у любого
  свежего неподписанного `.exe` от малоизвестного издателя резко повышенный
  шанс ложного срабатывания.
- Сама функциональность (перечисление и завершение чужих процессов,
  управление службами, сырое чтение/запись `\\.\PhysicalDriveN` у
  `diskutil`) — ровно те паттерны, которые эвристика помечает как
  подозрительные. Process Hacker и System Informer, на которые ориентируется
  этот проект, годами ловят похожие детекты у разных антивирусов по той же
  причине.
- NSIS-инсталлятор дополнительно повышает шанс детекта: этот формат часто
  используют малварь-дропперы, так что эвристики к нему настороженнее, чем
  к MSI.

Что можно сделать самостоятельно:
1. **Проверить, что файл действительно наш** — сверить SHA-256 скачанного
   файла с тем, что указан в описании релиза на GitHub (там для `.deb`
   хэш публикуется автоматически; для `.exe` при сомнениях можно
   пересобрать из этого исходника локально и сравнить).
2. **Отправить false positive в Microsoft**: https://www.microsoft.com/wdsi/filesubmission
   — "Submit a file for malware analysis", категория "Software developer".
   После подтверждения детект для конкретной сборки (по хэшу) снимается
   у всех пользователей Defender.
3. **Добавить исключение** в Defender (`Защита от вирусов и угроз` →
   `Управление настройками` → `Исключения`) — оправдано только если вы
   доверяете источнику сборки (собрали сами или скачали из официального
   релиза этого репозитория).

Полноценное решение — Authenticode code-signing сертификат для CI-сборок;
это отслеживается отдельно (см. `docs/packaging.md`, раздел "Code signing").

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
