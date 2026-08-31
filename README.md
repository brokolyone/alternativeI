# AltTools — кроссплатформенный аналог Process Hacker (Windows + Linux)

Process Hacker / System Informer сам по себе Windows-only; AltTools — тот же
класс инструмента (менеджер процессов, потоков, хендлов, модулей, сети и
служб), но одинаково работающий и на Windows, и на Linux, плюс отдельная
CLI-утилита для посекторного backup/restore MBR/GPT/дисков.

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
  диалог свойств процесса (Threads/Modules/Memory/Handles/Network/Environment,
  с фильтром по каждой вкладке), вкладки Performance (графики), Services,
  системная Network (все соединения всех процессов), System (сводка по ОС/
  CPU/памяти/аптайму) и Startup (что запускается при входе в систему).
  Полная локализация интерфейса EN/RU (`src/gui/i18n.h`), переключаемая из
  Settings; там же тема (System/Light/Dark, применяется без перезапуска) и
  интервал обновления списка процессов.
- `src/diskutil/` — отдельная CLI-утилита без Qt-зависимости для посекторного
  backup/restore MBR/GPT/дисков. См. раздел ниже.

## Статус

Реализовано и протестировано (Linux — вручную/скриншотами; Windows — компиляция
и линковка проверяются на каждый релиз через CI на `windows-latest`/MSVC, но
без ручной проверки на реальном железе):
- [x] Дерево процессов (parent-child), поиск/фильтр (рекурсивный), сортировка,
      настраиваемые колонки (правый клик по заголовку), в т.ч. Command Line
- [x] Terminate (в т.ч. целого дерева процессов), Suspend/Resume, изменение
      приоритета и CPU affinity, Open File Location, Copy Process Info,
      экспорт списка в CSV
- [x] Подсветка новых процессов; автообновление приостанавливается при
      зажатом Ctrl, чтобы не терять выделение при мультиселекте
- [x] Диалог свойств процесса: Threads, Modules, Memory regions, Handles,
      Environment, Network (у каждой вкладки свой фильтр) — на Linux
      проверено на реальных процессах (тесты, живые TCP-соединения, pipes/
      eventfd)
- [x] Performance-вкладка: графики CPU/память/диск I/O/сеть с автомасштабированием
- [x] Services-вкладка: список + Start/Stop/Restart
- [x] Network-вкладка: соединения всех процессов в одной таблице
- [x] System-вкладка: ОС/ядро/hostname/архитектура/ядра CPU, live CPU/память/
      аптайм
- [x] Startup-вкладка: автозагрузка (Linux `~/.config/autostart` +
      `/etc/xdg/autostart`; Windows `HKCU`/`HKLM` `...\CurrentVersion\Run`),
      пока только для просмотра
- [x] О программе / Настройки: EN/RU локализация всего интерфейса, тема
      System/Light/Dark (без перезапуска), интервал обновления, память
      размера/позиции окна
- [x] `diskutil`: info/backup/restore для MBR/GPT/произвольного LBA-диапазона/
      целого диска, с обязательной SHA-256 верификацией — проверено на реальном
      GPT-образе и на loop-устройстве (см. ниже)
- [x] Установщик (NSIS/.deb) и portable-версии (ZIP/tar.gz) для обеих платформ
      через CPack — см. `docs/packaging.md`

Дальше (по приоритету):
- [ ] Windows: подписанный kernel-mode драйвер (аналог KPH) для операций,
      недоступных из user-mode — принудительное закрытие защищённых хендлов,
      резолвинг имени хендла без риска зависания (см. `docs/windows-driver.md`)
- [ ] Startup-вкладка: включение/отключение и удаление записей, не только
      просмотр
- [ ] Резервное копирование secondary GPT (в конце диска), сейчас `--region gpt`
      захватывает только primary-копию в начале

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
