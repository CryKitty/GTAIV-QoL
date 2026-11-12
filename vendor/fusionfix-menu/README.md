# Verified FusionFix menu host seed

This directory contains only the compressed verified build91 menu host, its GPL-3.0 licence/notices, and the two build91 bridge source inputs. No old installer, ASI bundle, credentials, Git history or private diagnostics are included.

The decompressed `verified-build91.asi.zlib` SHA-256 must be:
`29b726c2efca00de185c7afe00f763601979bccd4bee7dca996be77a8c33a29d`.

`python tools/restore-verified-menu-host.py build` verifies that hash, applies the existing bounded navigation/menu adaptation and writes the host/licence into build/. Public CI uses this path and needs no private artifact access. Tests check the real seed and reject corruption. The seed was extracted as data from the supplied checksum-verified build91 payload; its old installer was never executed. Inspection found only the standard GitHub Actions PDB path, not a personal home path or private session/credential value.

## Corresponding source and build procedure

Upstream FusionFix source: https://github.com/ThirteenAG/GTAIV.EFLC.FusionFix/tree/cad0970e2c82712d86c849974b62d078331ddd88 (5.1.2; GPL-3.0, including upstream submodules).

The original bridge build uses the current `tools/patch-fusionfix-menu.py` and `tools/fusionfix-compass-range.inc`, with **build91-qol-menu.inc** in place of tools/fusionfix-qol-menu.inc and **build91-qol_settings_api.h** in place of src/qol_settings_api.h. Work in a disposable source copy; these retained inputs describe the base seed rather than changing the current bundle API.

1. Clone that exact upstream commit and its recursive submodules into ff-menu-build.
2. In an MSVC x86 developer prompt (the original used Windows VS2026), run `python tools/patch-fusionfix-menu.py ff-menu-build` with those base inputs.
3. From ff-menu-build, run `premake5.exe vs2026 --with-version=5.1.2.1`, then `"%VSINSTALLDIR%MSBuild\Current\Bin\MSBuild.exe" build\GTAIV.EFLC.FusionFix.vcxproj /m:2 /property:Configuration=Release /property:Platform=Win32`.
4. The output is bin/GTAIV.EFLC.FusionFix.asi. Compiler/linker versions and timestamps can change its bytes; it is not automatically accepted as the pinned seed. Do not replace the seed hash or bypass patch preimage checks without separate review/game testing.
5. The shipped navigation adaptation and exact byte preimages are in tools/restore-verified-menu-host.py. Current source-level menu intent is also in tools/fusionfix-qol-menu.inc. The original seed's startup, sections and relocations are retained by that bounded adaptation.

The host is modified FusionFix, not an official upstream binary, and is covered by its own GPL-3.0 terms in LICENSE.txt; the root MIT licence does not replace those terms. The full public tree contains the bridge source and adaptations; upstream source remains available at the pinned public URL above.
