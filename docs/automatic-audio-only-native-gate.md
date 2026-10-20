# D107: native automatic audio-only receiver gate

2026-10-20 (Asia/Tokyo). The user authorizes the native gate after
[PR #91](https://github.com/ilyalissoboi/send-airplay2/pull/91) merged as `d378249`.
All 17 checks passed at final head `3daa9a1` in
[run 39246535013](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39246535013).
The [API 6 profile and finite subset](automatic-audio-only.md) are unchanged.
No packaged UWP, Screenbox, codec-profile, quota or publication change is part
of this native-only gate.

## Preparation

The Windows x64 shared native C API host is rebuilt from merged `d378249`, with
`native_changes=no`, the existing enabled FFmpeg 9.0.2 port-6 backend, and the
same original runtime source. The new
[development fixture generator](../tools/audio-only-probe/generate_automatic_fixtures.py)
reuses the repository's original 64-second pulsed LEFT/RIGHT/BOTH marker author.
External development FFmpeg/ffprobe versions and output hashes are recorded;
these tools are distinct from the runtime backend. All generated media stays
in ignored local storage; no private or downloaded media is used.

The generated matrix contains native WAVE PCM and FLAC, AAC/M4A, Ogg Opus and
Vorbis, FLAC MP4, PCM MOV, FLAC/PCM Matroska and edited PCM MOV. The edited
timeline has initial silence [0,8), followed by source [8,64); marker times
at/after eight seconds remain the same. Existing committed 48-kHz CBR and
44.1-kHz VBR MP3 clips retain their original provenance and bytes.

All ten newly generated clips pass `--check-audio --audio-only` with the expected
selected identity, route/family and stereo output. Cache storage is empty after
each offline check. Read-only native discovery found a unique instance of each
recorded receiver model. Receiver availability/version confirmation and all
audible/prompt observations are still pending; discovery/preparation do not prove
receiver playback.

## Planned observed checks

Use saved pairings without resetting credentials or receiver/network settings.
Before each cast, verify the actual loaded native host and seven dependency DLL
paths/hashes against the inspected build. The native driver configures the explicit
Apple Audio v1 profile, checks the resolved action/families/original ID, retains
fixed-field telemetry only, and records any failed attempts. No addresses, device
identifiers, credentials, PINs, bearer URLs or private labels enter committed logs.

On each available receiver, start near eight seconds, listen for RIGHT pulses,
pause/resume, seek to 32 seconds (higher BOTH marker), seek back to eight (RIGHT),
then seek near 58 and allow natural end. Repeat across the finite generated
matrix and direct MP3 cases, recording sender controls/end/cleanup/source-release
evidence separately from written audible observations. A separate converted cast
starts at zero (LEFT) and receives Stop after eight seconds; require clean silence
and no delayed restart. Mac pending-consent cancellation is a separate converted
run: leave consent unanswered and cancel after five seconds, verifying prompt
closure and source/cache cleanup. If recent grant reuse suppresses the prompt,
record that limitation rather than claiming pending-consent coverage.

Telemetry is not proof of sound, channel correctness or receiver UI closure.
No Computer Use visual confirmation is permitted; use written user observations.
Short generated media does not prove sustained long-file performance or exact
sample-accurate startup/gapless playback. Package and Screenbox queue gates stay
separate, as do speaker/RAOP, discrete-surround and Chromecast hardware deferrals,
user-deferred remote-Stop classification and long-input/cache-quota evaluation.

## Current status

Prepared and offline-validated; no D107 receiver cast has run yet. Await the user's
availability confirmation before starting coordinated playback. Native receiver
results and exact sanitized evidence will be recorded when the gate finishes.
Packaged automatic audio-only validation follows this native gate and review/merge.
The [preparation record](validation/automatic-audio-only-native-windows-2026-10-20.json)
contains the current fixture/build hashes and offline results, explicitly without
any receiver playback observation.
