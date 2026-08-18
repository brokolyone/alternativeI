# AlternativeI — Process Hacker analog (Windows + Linux)

Кроссплатформенный аналог Process Hacker / System Informer: менеджер процессов,
потоков, хендлов, модулей, сети и служб, плюс работа с диском на уровне секторов
(backup/restore MBR/GPT/разделов).

## Архитектура

- `src/core/` — платформонезависимое ядро. `IProcessProvider` — интерфейс сбора
  данных о процессах; для каждой ОС есть отдельная реализация:
  - `src/core/windows/ProcessProviderWin.*` — WinAPI (Toolhelp32 + PSAPI),
    user-mode.
  - `src/core/linux/ProcessProviderLinux.*` — `/proc`.
  `ProcessProviderFactory.cpp` выбирает нужный backend по `#ifdef` на этапе
  компиляции.
- `src/gui/` — Qt Widgets: таблица процессов с сортировкой/фильтром,
  контекстное меню (Terminate, Priority), автообновление раз в секунду.

## Статус / дорожная карта

Реализовано (v0, MVP):
- [x] Список процессов: PID/PPID/имя/пользователь/CPU%/память/потоки/приоритет/путь
- [x] Terminate, изменение приоритета
- [x] Поиск/фильтр, сортировка по колонкам

Дальше (по приоритету):
- [ ] Дерево процессов (parent-child) вместо плоской таблицы
- [ ] Вкладки свойств процесса: потоки, модули/DLL, память (regions), хендлы,
      переменные окружения, сеть (TCP/UDP-соединения процесса)
- [ ] Windows: переход на `NtQuerySystemInformation` (SystemHandleInformation,
      SystemProcessInformation) для хендлов/модулей — как в оригинальном
      Process Hacker; подписанный kernel-mode драйвер (аналог KPH) для операций,
      недоступных из user-mode (принудительное закрытие чужих хендлов, unhook и т.д.)
- [ ] Linux: `/proc/[pid]/maps`, `/proc/[pid]/fd`, `/proc/net/tcp[6]` для сети
- [ ] Общесистемные графики (CPU/память/диск/сеть) в стиле Process Hacker
- [ ] Менеджер служб (Windows Services / systemd unit)
- [ ] **Дисковый модуль**: посекторное чтение/запись через `\\.\PhysicalDriveN`
      (Windows) и `/dev/sdX` (Linux); backup/restore MBR (512 байт) и GPT
      (protective MBR + заголовки + таблица разделов), опционально полный образ
      диска/раздела (аналог `dd`/Clonezilla) со сжатием и контрольными суммами.
      **Это самая рискованная часть — требует отдельного тщательного тестирования
      на виртуальных дисках перед любой работой с реальным железом.**
- [ ] Установщик/подпись для Windows-драйвера, права CAP_SYS_ADMIN/setuid или
      polkit-хелпер для привилегированных операций на Linux

## Сборка

### Linux

```sh
sudo apt install qt6-base-dev cmake ninja-build build-essential
cmake -S . -B build -G Ninja
cmake --build build
./build/alternative_hacker
```

### Windows

Требуется Qt6 (MSVC kit) и Visual Studio / CMake:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -DCMAKE_PREFIX_PATH="C:\Qt\6.x\msvc2019_64"
cmake --build build --config Release
```

Часть привилегированных операций (принудительное закрытие защищённых хендлов,
работа с чужими процессами повышенной защиты) в MVP не реализована — для этого
в дальнейшем понадобится подписанный kernel-mode драйвер, как у оригинального
Process Hacker.

## Предупреждение о дисковом модуле

Прямая запись в MBR/GPT/сектора диска необратимо уничтожает данные при ошибке.
Модуль будет разрабатываться с обязательным dry-run/подтверждением, чтением
контрольных сумм перед записью и тестированием исключительно на виртуальных
дисках/образах, прежде чем давать доступ к реальным `PhysicalDrive`/`/dev/sdX`.
