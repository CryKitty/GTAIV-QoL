> Current b99 follow-up supersedes mastering-property amplification and adds
> measured DS source PCM gain. See CUTSCENE_PCM_SOURCE_GAIN.md. This document
> records the earlier b84 audit, not proof that its mastering voice was audible.

# Cutscene Audio output audit (b84)

## Evidence

The supplied b84 pause log records a captured XAudio2 mastering output, a 15.80x
request around tick 126941160, returns/reapplications at scene transitions, and
restoration to 1.00 around 127192493. This disproves the inference that no current
audio path ran because GTAIV-QoL-audio-layout.log stopped growing. That latter
file is a static installer metadata inventory. No private log is committed.
The b84 records do **not** contain actual output levels, HRESULTs, sound samples
or audibility proof. They cannot establish whether the requested gain reached or
stayed on the audible bus.

## Actual mechanism and source gaps

The old metadata voice boost is removed. Cutscene Audio (%) multiplies every
captured **XAudio2 mastering voice** and captured **DirectSound secondary buffer**
while loaded && !finished && decoded cinematic state is 8/9. It is a whole-output
cutscene control, not a dialogue-only bus. Default 100% is unity. Pause holds the
same gain; ending/skipping/unloading/leaving that cinematic state restores unity.
Failed state reads fail closed to unity. OnKill and fresh script start restore.
The detector is independent of the Pause During Cutscenes toggle.

b84 only applied XAudio2 gain when the requested multiplier changed. A cached
original setter can bypass the private vtable and replace it; a deterministic
production-output test reproduces this loss. That source gap is proven; the
supplied log does not prove it occurred on this Deck. The old path also ignored
write failures/read-back, and DirectSound clamps at 0 dB. A full-volume DS buffer
cannot be boosted by its volume API: that limit is **not fixed by pretending the
clamped value is the requested multiplier**. No unverified sample-memory/DSP
patch is introduced in this PR. Full positive boost on such a backend remains
an honest limitation and requires a separately verified amplification mechanism.

## Change and proof contract

Normal ticks reconcile raw output getters against the last owned write. External
changes become the unboosted game baseline; adjustments never multiply a previous
boost again. Captured setters keep game-requested levels separate. Deferred
XAudio operations queue unscaled game levels so a later commit cannot replay an
old cutscene boost into gameplay; reconciliation applies current gain after the
committed level is observed. Output writes/read-backs and failures are counted.
DS reconciliation reports headroom-limited buffers separately from successful
clamped API writes. A successful API write is not a claim of audible PCM gain.

Session records identify engaged/adjusted/restored, percentage, cinematic state,
XAudio output/verification/failure counts and baseline/target/actual, plus DS
counts/limits. Periodic output samples include raw before/after and external-write
repairs. No-output/failed/limited records are failed or partial acceptance.

Windows executable tests cover positive gain, repeated cached-setter overwrites,
raw getter verification, failed setters, exact restore, no stacking, deferred
commit after restoration and DS headroom limits. These are controlled tests, not
a Deck sound recording. The PR remains a candidate until the user compares the
same scene at 100/200%, checks skip/interruption/back-to-back restoration, and
new logs establish the actual backend/read-back. It does not claim complete
positive gain across a DS buffer already at unity.
