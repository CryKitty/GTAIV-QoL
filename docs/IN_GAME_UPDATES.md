# In-game update checks on Steam Deck / Proton

Pause → QoL → **Check for QoL Updates** is Off by default. Turn it On to queue a
check; it also queues one check at each game startup while already On. Off does
not contact GitHub, cancels a staged apply, and preserves the other menu choices.
The toggle and installed worker arrive through the normal updater; no file edits
or manual systemd commands are needed.

The installer still handles first-time game discovery, dependencies and placement.
It now places `GTAIV-QoL-Updater.py` beside GTAIV.exe and installs/enables the Linux
user unit `gtaiv-qol-updates.service`. This is the existing verified Python updater
with a background worker mode, not an unverified program downloaded separately.
The installer embeds it in its verified helper, so even an older installed updater
can deliver it in one update without changing the five-file payload protocol.

The Windows ASI **only writes a small request file** in the shared game folder.
It does not launch Linux Python from Proton. The installer-managed systemd user
worker runs on the Deck host, including Game Mode, polls the shared folder and
uses the configured update channel and existing authorization. It never opens an
authorization dialog over the game. Public beta checks require no token; development
Actions downloads use the existing read-only authorization. No release is published
by this feature. Until a public beta is published, beta checks can report no update.

Development selection is unchanged: newest successful non-PR build.yml push or
workflow_dispatch on CryKitty/GTAIV-QoL main. Published beta selection still includes
prereleases. Downloads are checked against the GitHub digest, repository/branch/
version/build identity and every file hash. During play only the private staging
area under `~/.local/share/gtaiv-qol-updater/staged/` changes; loaded ASIs are not
replaced. After GTA IV exits, the worker checks processes again, revalidates the
archive and invokes the existing installer with its session lock, backups,
rollback and preference preservation. The next launch uses the installed build.
If a session lock is still busy, apply is deferred. Do not relaunch the game while
an update is being installed; the normal manual updater has the same restriction.

Manual upgrades reload the installed worker, including a changed game path.
During its own apply the worker finishes first, then systemd restarts the new code.
The worker shares the manual updater's lock. A manual update or channel change
invalidates an older pending stage, preventing a stale overwrite. Turning the
option Off prevents apply. Failed/tampered stages never install. Offline/auth/
service errors leave the manual Update GTA IV QoL shortcut available; Off → On
or the next enabled startup queues a fresh check. The first-time installer and
manual fallback remain available. This worker backend is Deck/Linux-specific;
it is not a Windows-native updater executable.

Local worker status: `GTAIV-QoL-update-status.json` beside GTAIV.exe. The request
file is `GTAIV-QoL-update-request.txt`; it is consumed after a check request. No
token or private API response is written to either file. These are automatic
state files, not settings the user edits.

## Updater / pause-menu acceptance

1. Close GTA IV and update once using the existing updater after this PR merges
   and its main build succeeds. Fresh installs use the normal guided installer.
2. Launch in Game Mode, open Pause → QoL and confirm Check for QoL Updates starts
   Off. Turn it On. Continue playing: there must be no installer window, game
   interruption or replacement of loaded ASIs. Check failure/offline must not crash.
3. With a newer successful build/published beta available on the configured channel,
   allow the download, then exit GTA IV. Wait for the worker to finish before
   relaunching. Confirm the new build and retained menu choices on the next launch.
4. Repeat with the option already On at startup; then stage a newer build and turn
   it Off before exit. That stage must not apply. Test manual updater fallback and
   a failed network check. No new release needs to be published just to test this PR.

Automated tests cover native default/On-edge/startup requests, menu registration,
opt-in persistence, game-folder placement and spaced paths, request consumption,
staging versus live apply, revalidation/tamper rejection, Off cancellation and
manual-update invalidation. Live Deck systemd/Game Mode/exit acceptance remains.
