# In-game update checks on Steam Deck / Proton and Windows

Pause → QoL → **Check for QoL Updates** is Off by default. Turn it On to queue a
check; it also queues one check at each game startup while already On and repeats
every ten minutes during the session while idle. Off does
not contact GitHub, cancels a staged apply, and preserves the other menu choices.
The toggle and installed worker arrive through the normal updater; no file edits
or manual service commands are needed.

## Steam Deck backend

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
manual fallback remain available. The Linux service described above is Deck-specific; native Windows uses the same
updater state machine through the frozen adapter below.

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

## Build identity and visible status

`VERSION` is the named-release version, currently `1.0.0`; it is not
bumped per PR. `write-qol-version.py` embeds the Actions run number (`b47`, for
example) and source SHA in the running ASI. Non-CI builds use a short commit if
provided, otherwise `local`. Each main Actions build gets a different number.
The displayed identity comes from the running binary, never a staged candidate.

The existing update toggle's label becomes a compact row such as
`QoL 1.0.0 b47 | Checking...`; On/Off remains the normal value control.
The 60-character native label buffer includes its terminator. Longer combinations
omit the release version before shortening any status. States are:

- Checking... → Downloading/staging → Staged - exit to apply.
- Applying update after exit → Updated YYYY-MM-DD HH:MM on next launch.
- Up to date; Updated MM/DD HH:MM after a later successful check.
- Update failed - retry for check/download/apply/request failures; Off → On retries.
- No published update when the selected beta channel has no release; Update
  cancelled for cancelled staging. Off shows the retained Updated timestamp when
  available, otherwise Updates off.

Dates use the computer's configured local time. The successful-install record survives
relaunch, errors and same-build checks; staging never changes it. Enabled startup
checks can briefly replace Updated with Checking, then Up to date plus Updated.
Only a successful install records a new time. The first upgrade also seeds this
receipt through the verified helper when an older updater performs installation.
The pre-existing five-file payload, hashes, backup and settings preservation stay.

The worker automatically writes `GTAIV-QoL-update-info.ini` beside the existing
status JSON. It contains fixed state codes and validated public build/date fields,
never authorization, private paths, API responses or exception text. The native
menu reads at most 4 KiB without the Windows profile cache and refreshes changed
labels on the game thread every 250 ms while paused, even with Compass HUD Off.
An unclaimed request older than 120 seconds or active progress older than five
minutes displays failure. Staged state does not expire. The existing worker poll
interval still applies. The install timestamp is displayed only when its source
SHA matches the running binary. No manual state-file edits are supported/needed.

For acceptance, first install this feature with the existing updater and relaunch;
the previous build cannot display the new states. Use the updater-only steps in
the top changelog entry. A genuine downloading/staged/applied sequence requires a
newer successful main build on the development channel (or a future published
beta on that channel); toggling while already current cannot manufacture an
update. This PR publishes no release. Offline testing may fail immediately or
wait for the network timeout. After reconnecting, Off → On explicitly retries.
No Deck acceptance result is claimed by the automated tests.

The native Game-menu version-footer extension now owns the identity display once
that footer has actually been shown this launch. The QoL update row then keeps
only its control name and live status/timestamp. Until then, or when unavailable,
it retains the build identity described above. See [camera/range/footer notes](CAMERA_RANGE_AND_FOOTER.md).

## Periodic session checks

The fixed interval is 600,000 ms, controlled only by the existing update toggle.
The Windows core's existing background tick owns an in-memory Schedule using
GetTickCount64. Startup/On still checks immediately once the bridge is idle;
every attempted request (including a write failure) anchors the next ten minutes.
No deadline/periodic job is persisted or scheduled in systemd. Pausing the game
does not stop the wall-clock interval; no gameplay window is opened.

Queued requests, Checking/Downloading/Applying receipts and a staged flag suppress
new requests. The worker commits Checking before deleting the request, and the
native tick reads request existence before the receipt, closing the handoff gap.
Busy checks are not duplicated even after the interval expires; once idle the
overdue check can run and starts a fresh interval, without catch-up bursts. Failed
checks retry at the next deadline. Unclaimed requests already failed by the
120-second watchdog can be replaced at that deadline, with only one request file.
The new Staged flag also suppresses checks after a failed apply with a valid stage.
Only a stale busy receipt is reset to failure by an idle worker holding the update
lock; this recovers worker restarts without confusing an active check with idle.

Off resets the schedule and removes the temporary/final request. The worker
cancels the stage before its running-game apply gate, so Off cancellation works
during play too. An already executing HTTP call cannot be recalled; opt-in is
checked before subsequent download/staging/apply. ScriptHook OnKill stops and
clears the core scheduler through an additive export; OnStart resumes a fresh
session. A process exit destroys the timer. The worker deletes requests when GTA
is no longer running; the next native startup also clears any old request before
its startup check. A request older than the live Linux GTA process start is
rejected, including exit/relaunch entirely between worker polls. The Linux
/proc start time is used because Proton's Windows PID can differ from Linux PID.
Legitimate staged update payloads remain available for normal exit installation;
only queued checks/deadlines are session-local.

Native timer decisions are tested with explicit clocks; Windows file/lifecycle
tests simulate busy, failed, staged and unloaded states. Worker tests cover a
mid-session newly available build through the real state path, immediate Off
cancellation, failed retry, stale request cleanup and Linux process-start parsing.
Use the newest changelog's updater-only acceptance steps. Download/publish times,
worker polling and network latency add to the interval; no hard completion time
is promised for failed/slow networks or a worker that is not running.


## Native Windows backend

Extract the complete Windows bundle and run **Install-GTAIV-QoL.exe**; no Python
installation is needed. Setup registers **GTAIV QoL Updates** in the current user's
HKCU Run key and starts the windowless frozen updater with `--watch`. It runs at
logon, without a Windows service, administrator rights or an authorization dialog.
The existing **Update GTA IV QoL** Desktop shortcut remains the manual fallback.
A writable game directory is required. Only dependency installers may request UAC.

The worker uses the very same `game_update_tick`, channel selection, GitHub digest,
manifest/file verification, stage and apply functions as Deck. The default public
channel includes published releases and prereleases, never drafts or PR artifacts.
The exact request/status/info protocol above is unchanged. Checking is committed
before request consumption; Off cancels stages during play. Requests older than
GTAIV.exe's **GetProcessTimes creation time** are rejected. Failure to read that
time fails closed, rather than accepting a possibly stale request. No-game polls
remove stale request/temp files and reset abandoned busy receipts under the shared
update lock. Process enumeration uses an in-process Toolhelp snapshot, not a shell/tasklist
process. Idle polling is every three seconds; there is no network polling
without an enabled game request and no window over gameplay.

Private staging, installed records, session/update/watch locks and frozen EXEs live
under `%LOCALAPPDATA%\GTAIV-QoL`. Apply revalidates the archive and process absence,
then uses the same verified installer transaction, backups, rollback and settings
preservation. A busy session lock defers the retained stage to another poll. Manual
updates share the update lock; installer transactions also take it. Successful
manual installs invalidate older stages even when the source SHA is unchanged.

Every successful install refreshes registration and the Desktop shortcut. Frozen
EXEs have content-addressed names: a mapped EXE is never replaced in place. The
registered worker rereads the configured game path each poll. A changed executable
causes the old worker to release its singleton lock before starting its successor;
its own apply completes before that handoff. Reinstalling a newer Windows bundle
refreshes frozen platform code. Payload updates refresh the shared updater script;
the successor loads it only when its hash matches the installed verified manifest.
Missing/tampered copies fall back to embedded code so manual repair stays available. No extra runtime download or
unverified code-loading mechanism is added. A killed worker can be restarted by
rerunning setup/the manual shortcut or at the next logon; HKCU Run is not a crash
supervisor. Stale busy state recovers on that restart. Offline/error cases leave
the manual shortcut available; Off → On queues a fresh request as usual.

### Windows acceptance (not yet performed on a stock user PC)

1. Fresh stock Windows 10/11 install with no Python; reboot and confirm worker
   running. CI covers frozen Python/Tk loading and registration idempotence, not
   actual user logon/reboot or the complete dependency/install flow.
2. Launch GTA IV; Pause → QoL → Check for QoL Updates starts Off. Turn On; observe
   Checking then downloading/staged without a window or changed loaded ASIs.
   CI covers request ordering, live staging and process guards, not gameplay/UI.
3. With a newer published release, exit and wait for apply; relaunch to confirm
   new build, retained choices and Updated timestamp. CI covers verified stage
   revalidation/exit apply and lock deferral; a real published upgrade remains.
4. Stage, turn Off before exit: nothing applies. CI covers cancellation during play.
5. Kill/restart the worker mid-stage; confirm recovery/no partial or duplicate
   apply and working manual shortcut. CI covers restart receipts, singleton
   handoff, manual invalidation and tamper rejection. Real interruption remains.

Windows CI also exercises GetProcessTimes on an actual Windows child process.
None of these tests claims native Windows GTA IV gameplay acceptance. This PR
changes only the Windows installer/backend: VERSION and the released five-file
payload/manifest remain unchanged; Deck delivery/behaviour is untouched.
