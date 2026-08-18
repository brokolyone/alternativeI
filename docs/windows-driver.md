# Windows privileged-operations driver — architecture (not implemented)

This document specs out the kernel-mode driver that Process Hacker itself
ships (`KSystemInformer`/historically `KProcessHacker`) and that this
project doesn't have yet. It's written instead of code because building,
signing, and testing a Windows kernel driver needs a Windows machine, the
WDK, and (for anything beyond local test-signing) a real code-signing
certificate — none of which exist in this Linux sandbox. Shipping unaudited,
untested kernel code would be worse than not having the feature.

## Why user-mode isn't enough

Everything implemented so far (`src/core/windows/*`) works from user-mode
with standard privileges (`SeDebugPrivilege` at most). Three gaps need
kernel help:

1. **Forced handle close on protected processes.** `DuplicateHandle`/
   `NtClose` from user-mode fail against processes with a higher
   protection level (PPL/PP, anti-malware-protected processes, etc.).
   Process Hacker's driver does this by walking the target's handle table
   from kernel context, which isn't gated by the same ACL check.
2. **Handle *name* resolution without the hang risk.** `NtQueryObject(...,
   ObjectNameInformation, ...)` on certain handle types (named pipes and
   mailslots in particular) can block forever waiting on a lock the owning
   thread holds indefinitely. Process Hacker's driver reads the object's
   name directly from the kernel object header instead of calling into the
   object's own query routine, sidestepping the hang entirely. (The
   current `handles()` implementation in this repo deliberately skips name
   resolution for exactly this reason — see the comment in
   `ProcessProviderWin.h`.)
3. **Reading/writing memory of protected or PPL processes**, and a few
   other operations (e.g. querying a process's protection level, which
   isn't exposed cleanly from user-mode) that this project doesn't
   implement yet at all.

## Non-goals

This is an admin diagnostic tool, not a rootkit. The driver must **not**:
- Hide itself from the OS's driver list, Device Manager, or `driverquery`.
- Provide a generic "read/write arbitrary kernel memory" or "get a full-
  access handle to any process including protection bypass" primitive to
  user-mode. Even though Process Hacker's own driver has historically had
  exactly this shape and has been flagged as a BYOVD (bring-your-own-
  vulnerable-driver) target for that reason, this project should scope the
  IOCTL surface as narrowly as the three gaps above require, not "whatever
  might be useful later."
- Skip input validation on any IOCTL because "the caller is us, our own
  GUI." Any user-mode process that can open the device handle can send
  IOCTLs; the driver's ACL controls *who* can open the handle, but every
  request still has to be validated as if it were hostile, because a
  local Administrator invoking a buggy IOCTL is a privilege-escalation-to-
  kernel bug regardless of intent.

## IOCTL surface (proposed)

A `FILE_DEVICE_UNKNOWN` device, exposed under `\Device\AlternativeHackerKph`
with a symbolic link the user-mode side opens by name. KMDF (Kernel-Mode
Driver Framework), not raw WDM — far less boilerplate, well-trodden for
this exact class of driver.

| IOCTL | Input | Output | Does |
|---|---|---|---|
| `IOCTL_AH_OPEN_PROCESS` | target PID, desired access mask | duplicated handle (limited to a project-defined allow-list of access rights, never `PROCESS_ALL_ACCESS`) | Opens a protected-process handle the caller couldn't get itself |
| `IOCTL_AH_CLOSE_REMOTE_HANDLE` | target PID, handle value | status | Forces closure of one handle in another process's handle table |
| `IOCTL_AH_QUERY_OBJECT_NAME` | target PID, handle value | name string | Reads the object name from the kernel object header (no hang risk) |
| `IOCTL_AH_QUERY_PROTECTION` | target PID | protection level enum | Exposes `PS_PROTECTION` cleanly |

Every input struct is validated for size and pointer bounds
(`METHOD_BUFFERED` for all of these — the payloads are small and fixed-
size, so there's no reason to take on the extra risk of `METHOD_NEITHER`'s
direct user-buffer access). PIDs are re-validated against
`PsLookupProcessByProcessId` inside the driver; the driver never trusts a
raw `EPROCESS`/`ETHREAD` pointer passed up from user-mode.

## Access control

- The device object's ACL grants access only to the Administrators group
  (`IoCreateDeviceSecure` with a matching SDDL string), so a non-elevated
  process can't even open a handle to it.
- The GUI process itself doesn't need to run elevated for its own sake —
  only the specific action that needs the driver (opening the device
  handle, or an explicit "Run as administrator" re-launch path) does. This
  mirrors Process Hacker's own UAC behavior: most of the tool works
  unelevated, and it prompts only when a feature genuinely needs it.

## Signing

Windows won't load an unsigned kernel driver outside test-signing mode.
For a real release:

1. **Development**: `bcdedit /set testsigning on` on a dev/VM machine,
   self-signed test certificate. Never ship a build that requires
   test-signing to end users — Secure Boot machines reject test-signed
   drivers outright, and it trains users to disable a security control.
2. **Release**: Microsoft now requires drivers to go through the [Windows
   Hardware Dev Center dashboard's driver attestation signing]
   process for a standard (non-WHQL) kernel driver: sign the driver with
   an EV code-signing certificate, submit it, Microsoft cross-signs it.
   This needs a real company/organization identity behind the EV cert —
   not something that can be provisioned from an agent session.
3. Run **Static Driver Verifier** and **Driver Verifier** (stress mode,
   all flags) against every build before it goes anywhere near a release
   channel; a kernel driver crash is a bugcheck, not a caught exception.

## Deployment

- Ships as a Windows service of type `SERVICE_KERNEL_DRIVER`, installed/
  started via `CreateServiceW`/`StartServiceW` from the (elevated) GUI
  installer, not auto-started at boot — load it on first use, unload it
  when the GUI exits (`IOCTL`-free — just `ControlService(SERVICE_CONTROL_STOP)`
  then `DeleteService`).
- Ship a `.cat` catalog alongside the `.sys` and `.inf` so `signtool
  verify` and Windows' own signature check both pass cleanly.

## What to build first, when this becomes buildable on Windows

1. `IOCTL_AH_QUERY_OBJECT_NAME` alone — lowest risk (read-only), directly
   fixes the handle-name gap already called out in the code.
2. `IOCTL_AH_QUERY_PROTECTION` — also read-only.
3. `IOCTL_AH_CLOSE_REMOTE_HANDLE` and `IOCTL_AH_OPEN_PROCESS` last — these
   are the ones that actually need careful access-mask allow-listing and
   the most scrutiny before shipping.
