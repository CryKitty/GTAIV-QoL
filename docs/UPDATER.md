The recurring update can now be enabled in Pause → QoL → **Check for QoL Updates**
(default Off). The installer puts the updater in the game folder and registers a
Deck Linux user worker. The ASI queues a request; checks/staging happen on the
host and the existing installer applies only after exit. No loaded ASI is replaced.
First-time setup and the manual updater remain. Full flow, errors and acceptance:
[IN_GAME_UPDATES.md](IN_GAME_UPDATES.md). Canonical channel selection below is unchanged.

# One GUI installer with an integrated updater

Download/extract the complete Nexus beta archive and run **Install-GTAIV-QoL.desktop** in Desktop Mode (Execute/Trust if asked). The archive includes the exact verified payload and the current dependency guide/updater sources. There is no second required updater download. [INSTALLER_AUDIT.md](INSTALLER_AUDIT.md) records what was already implemented versus fixed and gives the exact fresh-install checklist.

## First-install flow

The GUI verifies its bundled payload and checks for a newer published public beta. If available it downloads that matching payload; if not, it can continue from the archive with no private authorization. It discovers GTA IV in Steam/SD-card libraries, validates Complete Edition 1.2.0.59 and checks ASI loader, native ScriptHook, CE patch, X86 runtime, supported FusionFix host/configuration and all 151 required 5.1.2 assets. Proton native-loader setup is checked as well.

The wizard presents one missing requirement at a time, skips already satisfied requirements and shows the complete checklist only at the final review. Once ready, it offers Install only; after completion there is only Finish/Close. Use automatic setup for the current requirement or open its author download page and save supported ZIP/EXE files in Downloads. **Retry / check downloaded file** validates and places downloaded loader/ScriptHook/CE/FusionFix files; archives are retained. The pinned Microsoft X86 installer runs under the selected Steam Proton tool, in GTA IV’s existing prefix, and the runtime is checked again. The compatibility tool is read from Steam’s app configuration automatically, with an exact recorded last-run fallback when no mapping exists; no Proton-version choice/check is presented. If settings cannot be resolved, setup asks for a normal Steam launch/close and Retry instead of guessing. Launch GTA IV once from Steam if no prefix exists. Incompatible existing files are preserved and reported; optional DXVK/Vulkan wrappers are not installed over the user’s renderer.

Only after checks pass can the guide install all seven components through the existing checksum/backup/duplicate-removal/rollback path. A final pre-install check catches changed dependencies. The updater is installed under ~/.local/share/gtaiv-qol-updater and creates **Update GTA IV QoL** on the Desktop. Existing uploads keep their configuration; fresh installs create no upload credentials. Failed checks/installations do not report “ready to play.”

## After installation: public beta channel

New Nexus installs default to `beta`. The installed shortcut checks **published GitHub releases**, including beta prereleases and final versions; drafts and incompatible/missing update assets are excluded. Semantic versions are ordered numerically, so beta.10 follows beta.2, and a newer installed beta is not downgraded. The asset must be named **GTAIV-QoL-update.zip** and supply GitHub’s SHA-256 digest. The archive’s repository, version, canonical branch, shape and every file hash must match the selected release.

No private development token or log-upload token is sent by this channel, and it never opens private-token setup. Until a beta release/update asset is published it reports no published update; the supplied archive can still install. Network/API failures report unavailable without requesting a private token. Re-launching the same verified installed-version installer hands off to the updater instead of repeating dependency setup. The persistent shortcut runs independently of the downloaded archive.

Checks happen when launched, not periodically or during game launch. Close GTA IV before installation/update; game-process/session locks prevent hot replacement. No Steam launch-option rewrite or background polling service is added. Prefix-scoped native ASI-loader configuration is verified through Proton’s registry, not by asking the player to edit files.

## Development channel on public main

Existing `dev` users keep their channel. Selection is the newest completed successful **build.yml** push/workflow_dispatch run on **CryKitty/GTAIV-QoL main**, excluding PRs, forks, other branches, failed/in-progress runs, expired artifacts and mismatched digests/manifests. The repo name was reused for a new repo ID; old fine-grained tokens scoped only to the renamed archive do not grant access to the new repo. GitHub requires authentication for Actions artifact downloads even for public development builds. The GUI explains how to create/update a read-only token for the new repo with Actions: Read and Contents: Read. Keep the separate RoadNav upload token unchanged.

The separately retained `release` channel excludes prereleases; it is not the Nexus default. `--development`, `--betas` and `--releases` are maintainer/compatibility tools. The installer preserves existing channel choices. No manual config/INI editing is needed to migrate the existing development install.

## Migrate an existing Deck install

Do this only after the new main build and ZIP read-back have passed CI:

1. Sign into GitHub in the Deck's Desktop Mode browser. Open https://github.com/CryKitty/GTAIV-QoL/actions/workflows/build.yml and select the newest **successful main** run, not a PR run.
2. Download artifact **GTAIV-QoL-Nexus-beta-Deck**. Extract GitHub's artifact ZIP, then extract the contained **GTAIV-QoL-0.1.0-beta.1-Deck.zip** into one folder. Keep its complete contents together; this is the integrated installer, not a source ZIP or loose ASI.
3. Close GTA IV and run **Install-GTAIV-QoL.desktop** (Execute/Trust if asked). Follow the normal dependency checks/Retry/Install flow. The bundled payload needs no update token to install. An old same-version state from feature/cutscene-pause must still refresh the installed updater to main instead of handing off to the old installation. Menu settings and existing channel are preserved; no INI, Steam launch option or log-token edits.
4. For continued **development** updates, open GitHub Settings → Developer settings → Personal access tokens → Fine-grained tokens. Edit the existing QoL read-only token's repository selection to include the **new public GTAIV-QoL**, or create a new token limited to it; permissions Actions: Read and Contents: Read. Do not broaden to all repositories or replace the separate upload token. If you kept the same token and added the new repo, no re-entry is needed; otherwise enter the replacement only in the updater's local password dialog when prompted.
5. Launch the installed **Update GTA IV QoL** shortcut. Confirm it selects a successful new-repo/main build, preserves menu choices and reports success. Future checks use that shortcut; no archive redownload is required.
6. Re-point external automation: repository **CryKitty/GTAIV-QoL** (new repo ID; reselect it in integrations that bind by ID), workflow **build.yml**, branch **main**, status **completed**, conclusion **success**, event **push/workflow_dispatch**; explicitly exclude pull_request runs. Select artifact **GTAIV-QoL-update-payload**, validate its GitHub digest and matching repository/main/SHA/run manifest. Do not select GTAIV-QoL-dev or feature/cutscene-pause. If a helper supports only the old branch without a GUI/source update, replace it with the newly generated installer/updater instead of editing game settings.

The new updater bootstrap refreshes older bootstrap revisions while preserving newer ones. The integrated installer also verifies installed updater identity/hash before its repeat-launch handoff. No public beta release is needed for this one-time development migration.

## Public source and release contract

The authoritative functional pointer is **UPDATE_SOURCE** in tools/steam-deck/qol-updater.py: CryKitty/GTAIV-QoL / main. Manifest generation, Nexus metadata, release validation and beta-tag ancestry checks share it. The old GTAIV-QoL-dev stays private with its original history; this fresh repository has none of that history or private session references.

Public CI uses the checksum-verified vendored FusionFix menu seed and licence, not an archive Actions artifact. See vendor/fusionfix-menu/README.md for corresponding source/build instructions. New Nexus installs use the token-free public beta channel; published betas must carry **GTAIV-QoL-update.zip** with GitHub's digest (produced by CI as **GTAIV-QoL-public-beta-update**). A tag, Actions artifact or Nexus upload alone does not publish that release. No beta release is published by this migration.

## Current UI acceptance

Run the rebuilt installer from the complete archive. Confirm each missing dependency has its own screen with only applicable actions; completed dependencies disappear. Steam compatibility configuration is automatic, including when multiple versions are installed. Final review shows all checks green and only Install; completion offers only Finish/Close. In game, use QoL → Road Line On/Off; legacy Flat/Twisted both become On and Off stays Off. Hold D-pad Left over a compass icon on foot/in a vehicle: its ring fills for the existing 900 ms hold and Setting waypoint sits below it. Release/phone/combat/control/focus gates still cancel. No file edits or save/cloud changes are required.
