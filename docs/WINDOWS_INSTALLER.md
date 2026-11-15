# Windows installer: delivery and acceptance

This is an installer front-end around the existing 1.0.0 payload, not a new mod
release. VERSION, ASIs, game settings, pause-menu UI and the released manifest
remain unchanged. tools/windows/build_installer.py pins the release payload
SHA-256, source a182069 and b134 identity before freezing Python/Tk with
PyInstaller 6.16.0 (build dependency only). package-nexus.py emits the Windows ZIP
and verifies nested payload read-back. Python/PyInstaller/Tcl/Tk notices travel
with it. The executable is unsigned; SmartScreen may ask for confirmation.

The shared one-missing-requirement flow, PE/file checks, author downloads,
checksum checks, import logic and install_bundle backup/rollback engine are
reused. Windows adapters provide registry/Steam VDF discovery, native Microsoft
runtime execution (with UAC only when required by that verified installer),
Windows locks/process checks, Tk progress/dialogs, a per-user frozen updater and
Desktop .lnk. There is no Python install prerequisite, Proton tool, pfx scan,
Wine registry operation or systemd service. %LOCALAPPDATA% stores updater state;
HKCU Run starts its windowless request worker. The existing pause-menu opt-in
continues controlling checks/staging. A staged update is not applied while the
game runs; file locks serialize installer/manual checks/background requests.

The native installer never assumes it can write protected Steam game folders:
write access is required and failures retain backups rather than report success.
It does not run all of QoL as administrator. No fresh log-upload credentials are
created. The verified payload's Linux service helper is not executed on Windows;
its shared install_bundle function is invoked with the Linux worker disabled.
The updater uses its bundled Windows adapters while preserving the payload's
original files/hashes in installed.json. Same-build checks therefore remain true.

## Preventing spurious updates

The existing native build/test workflow still runs for regression coverage, but
payload_policy.py suppresses all its mod artifact publication for changes limited
to installer/docs/tests/workflow paths since v1.0.0. Without a new update-payload
artifact, the development selector skips an installer-only main run and retains
the previous delivery. The public release's GTAIV-QoL-update.zip also stays
untouched. The Windows workflow downloads and verifies that exact released ZIP.
After independently verified merge, attach ONLY GTAIV-QoL-1.0.0-Windows.zip to the
existing v1.0.0 release; do not retag, replace the update ZIP or replace Deck ZIP.
Future component/payload-input changes bypass this installer-only guard and build
normal payloads. A future named Windows baseline needs an explicit pin update.

## Evidence and remaining acceptance

CI runs the Python suite on Linux and Windows; fixtures cover Windows registry
Steam libraries, native runtime handling, shortcut command construction, shared
payload packaging and the artifact publication guard. A frozen EXE smoke removes
Python from PATH and verifies bundled Python/Tk, actual COM .lnk creation and no
console window. These are installer/loader checks, not Windows gameplay proof.

Still required on a real stock Windows 10/11 PC with NO Python installed:

1. Steam GTA IV CE 1.2.0.59, closed; fully extract the Windows ZIP and run EXE.
2. Verify registry/library discovery, ambiguity picker and manual GTAIV.exe pick.
3. Check one requirement at a time: ASI Loader v10.0.2, native ScriptHook, CE patch,
   X86 runtime 14.44.35211.0, FusionFix 5.1.2 host + assets. Test automatic setup
   and author download → Retry/import. No Proton/prefix UI may appear.
4. Reject unsupported/custom FusionFix hosts without overwriting. Cancel UAC or
   a failed dependency and check Retry; final checklist only after success.
5. Install with file backups; verify duplicate removal, retained settings and a
   deliberately failed bundle write rolling back. No manual file or INI edits.
6. Launch the game; check all seven components via pause-menu controls. Native
   Windows compilation/loader tests pass; equivalent Windows gameplay coverage
   is not claimed until this actual acceptance is recorded.
7. Use the real Update GTA IV QoL shortcut, then rerun the same installer and
   verify updater handoff/up-to-date behaviour without a false updated timestamp.
8. Check repeated worker starts, pause-menu checks On/Off, staging while playing
   and apply after exit; verify restored settings and relaunch. The executable
   and installed updater must work with Python absent throughout.
