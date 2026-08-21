# Packaging & installation (not implemented)

Notes on how this project would actually get onto a user's machine.
Nothing here is built yet — no `.deb`, no AppImage recipe, no Windows
installer script — this is the plan, written down so it doesn't have to be
re-derived later, and so packaging decisions (elevation model in
particular) get made deliberately instead of by accident.

## Linux

Two targets ship: `alttools` (the GUI, needs Qt6) and `diskutil`
(the CLI, no Qt dependency — worth keeping installable on its own for
headless/server use).

### Privilege model

Most of the GUI runs fine unprivileged: your own processes' threads/
modules/memory/handles/environment/network, and unprivileged `/proc`
system stats. Things that need root:
- Inspecting *other users'* processes' `/proc/[pid]/{maps,fd,environ}`
  (the kernel itself enforces this, not this project).
- `IServiceManager::start/stop/restart` — `systemctl` prompts via polkit
  when D-Bus-activated, or fails outright when shelled out to directly
  without a polkit agent in the session; document this rather than
  silently `sudo`-wrapping it.
- `diskutil` against a real block device — needs read (backup) or
  read-write (restore) access to `/dev/sdX`, which is root-only by
  default on most distros.

Rather than making the whole GUI setuid/setcap (a large attack surface for
very occasional need), the plan is:
1. Ship a **polkit policy** (`org.alternativei.diskutil.policy`) that
   allows an authorized admin to run `diskutil` with an
   `org.freedesktop.PolicyKit.exec` action, invoked via `pkexec diskutil
   ...` for the specific backup/restore call — not a standing elevated
   process.
2. Leave service control to whatever polkit rules the user's own
   `systemd`/`polkit` setup already has for `systemctl`; don't try to
   grant broader rights than that from this project's packaging.
3. The GUI itself runs as the invoking user always; it shells out to
   `pkexec diskutil ...` only for the specific disk operation the user
   confirmed, the same "elevate the one action, not the whole app" model
   as the Windows side (see `docs/windows-driver.md`).

### Package formats

- **`.deb`** (Debian/Ubuntu and derivatives): straightforward — CMake
  install rules (not yet added to `CMakeLists.txt`) plus a `debian/`
  control tree. Dependencies: `libqt6widgets6`, dynamically linked.
- **AppImage**: bundles Qt6 itself, so it runs on distros without a
  matching system Qt version. Better default for "download and run"
  distribution; `linuxdeployqt` or `linuxdeploy` + the Qt plugin handles
  the bundling. `diskutil` has no Qt dependency, so it can ship as a
  second, tiny, statically-linkable binary alongside the AppImage rather
  than inside it.
- Both need the polkit policy file installed to
  `/usr/share/polkit-1/actions/` regardless of package format.

## Windows

### Elevation model

Same "elevate the one action, not the whole app" principle as Linux:
- `alttools.exe` ships **without** an
  `requireAdministrator` application manifest — it launches at normal
  user privilege.
- Actions that need more (opening a protected process via the future
  kernel driver, installing/starting that driver's service, `diskutil`
  against `\\.\PhysicalDriveN`) trigger a UAC prompt at the point of use —
  either by re-launching a small elevated helper via `ShellExecuteW` with
  `lpVerb = L"runas"`, or (for the driver specifically) because
  `StartServiceW` against a `SERVICE_KERNEL_DRIVER` inherently requires
  it.
- `diskutil.exe` itself stays a plain console app with no manifest-forced
  elevation; a user running it against `\\.\PhysicalDriveN` directly from
  an elevated terminal is the expected path, same as `diskpart`/`dd for
  Windows`.

### Installer

MSI via WiX, or a simple NSIS installer if MSI's authoring overhead isn't
worth it for a tool this size — no strong reason to prefer one over the
other yet. Either way the installer needs to, itself running elevated:
1. Copy `alttools.exe`, `diskutil.exe`, and the Qt6 runtime DLLs
   (`windeployqt` generates the exact dependency set) to
   `%ProgramFiles%\AltTools\`.
2. **Not** install or start the kernel driver as part of a default
   install — until `docs/windows-driver.md` is actually built, signed,
   and audited, there's nothing to install. When it exists, the installer
   registers it as a demand-start `SERVICE_KERNEL_DRIVER` (never boot-
   start) and copies the signed `.sys`/`.cat`/`.inf` alongside it.
3. Register an uninstaller entry, Start Menu shortcut, and (optionally)
   file-type or `PATH` registration for `diskutil.exe` so it's usable from
   a terminal without hunting for the install directory.

### Code signing

The *application* binaries should be Authenticode-signed with a standard
code-signing certificate regardless of the driver question — an unsigned
`.exe` downloaded from the internet gets a SmartScreen warning that erodes
trust in the whole project. This is a separate, cheaper certificate than
the EV cert the kernel driver needs (see `docs/windows-driver.md`'s
signing section) and can happen well before the driver work does.

## What to actually build first

1. CMake `install()` rules for both targets (currently missing entirely —
   `cmake --build` produces binaries in the build tree only).
2. The Linux polkit policy + a packaging smoke test (build the AppImage,
   run it on a clean container, confirm `pkexec diskutil` prompts
   correctly).
3. Windows installer, once there's a Windows machine in the loop to build
   and test it on.
