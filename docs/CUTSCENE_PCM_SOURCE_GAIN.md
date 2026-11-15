> The user confirmed the captured DirectSound PCM source is plainly audible on
> the Deck in the current build. The follow-up calibration applies half of the
> gain difference from unity: applied = 1 + (requested - 1) / 2. The saved slider
> remains requested percent; session/source logs carry requested and applied gain.
> The b99 evidence limits below are historical, not the current user verdict.

# b99 audibility investigation and source-level gain

## What the session establishes

b99 reports mastering property 15.8, verified=1, no external writes, while the
user hears no boost. Every DS record reports two buffers and two getter failures.
The log does not contain peaks, sample data, caps, HRESULTs, playback cursors or
output routing. It cannot empirically identify the carrying backend. Claiming
that property verification proves audibility was wrong. DS failed counters also
included failed GetVolume before SetVolume was reached; they are not evidence
that a particular setter HRESULT occurred. No private log dump is committed.

## Mechanism

Stop amplifying the unproven mastering voice. Observe its property only and
label signal verification as zero. The new mechanism applies gain to captured
DirectSound source PCM at its documented Lock/Unlock boundary, bypassing missing
volume controls and their 0 dB ceiling. Preserve a pristine source copy, render
scaled/clamped PCM into the native locked buffer, and read the bytes back.
Game Lock sees the pristine input, preventing read/modify/write accumulation.
Existing buffered PCM is rewritten at scene/slider changes; end/skip/interrupt/
script-stop restores the original bits, not divided/clipped approximations.
The unchanged cinematic admission/restoration gate remains the owner.

Support integer PCM 8/16/24/32 and float32, including validated extensible PCM
formats. Unknown/compressed formats fail closed and log unsupported capture.
Budget: <=1 MiB per buffer, <=16 MiB total pristine data (three copies, <=48 MiB).
No allocation on normal streaming callbacks. Native HRESULTs are forwarded;
missing volume controls are no longer faked as GetVolume success. Deferred
mastering operations remain untouched. Gain is never also applied to mastering
or the same PCM-ready buffer's volume control, avoiding double amplification.

PCM trace records source ID, percentage/multiplier, real input/output peak/RMS,
clipping, PCM byte read-back, source writes, playing and cursor advancement.
Capture records caps/format; control probes and Lock/Unlock log HRESULTs. A
nonzero moving/playing source with measured increased RMS identifies an active
candidate; property-only, silent, unsupported or stationary records do not.
A source peak is not a microphone/speaker measurement: the next Deck session
must establish that this active source carries the audible scene. If it doesn't,
this is failed acceptance and an XAudio source/route-level probe is still needed.
There is no empirical claim that the uninstrumented b99 sources carried its mix.
A pure-XAudio scene with no captured supported DS source is NOT claimed fixed.

Percent uses amplitude-relative convention: 100%=1x, 200%=2x (~+6.02 dB),
1580%=15.8x (~+23.97 dB), not +15.8 dB. Clipping counts make saturation explicit;
use 100/200% on the same scene for acceptance. The labelled % and request agree.

Windows test runs the production source hooks against a playing PCM buffer with
DSERR_CONTROLUNAVAIL for both GetVolume/SetVolume. It measures 2x samples/RMS,
100 streaming rewrites without accumulation and exact restore after clipping
and repeated scenes. Portable tests cover formats, alignment, saturation and
bit-identical restore. These controlled tests do not replace the Deck comparison.
