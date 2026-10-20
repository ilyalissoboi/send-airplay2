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
recorded receiver model. The user confirms both receivers available and macOS 26.7.1/tvOS 26.6 unchanged.
Mac converted-FLAC audible controls/seeks/natural end pass by user report. Broader
checks are active and remaining written observations are pending; telemetry alone
does not prove receiver playback.

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

## Paused checkpoint

The user explicitly paused testing on 2026-10-20. No native test host remains and
the owned cache has zero entries. Do not start further casts, tests or CI until
explicit resume; previously pending readiness questions are suspended. Draft
[PR #92](https://github.com/ilyalissoboi/send-airplay2/pull/92) stays incomplete.

The runtime tests use checkpoint `4cd545a` with native source unchanged from merged
`d378249`. Before each of eleven attempts, the actual executable and seven DLL
paths/hashes matched the build. All eleven source instances released once with
zero failed reads and empty post-run cache; failed-start URL cleaned=no remains
separate from media_cleaned=1. Seven completed cases have explicit user PASS:

- Mac FLAC conversion: audible controls/seeks/natural end, and separate Stop.
- Mac progressive AAC/M4A, CBR MP3 and VBR MP3: audible controls/seeks/natural end.
- Apple TV FLAC conversion: audible controls/seeks/natural end, and separate Stop.

Both receivers' OS versions are explicitly unchanged (macOS 26.7.1/tvOS 26.6).
Natural-end and Stop cases have sender media cleanup and the matching written
observations. Initial state snapshots can still be near the beginning despite a
requested eight-second start; retain them alongside the user's audible marker
confirmation, without claiming sample-exact start positioning.

Keep these failed or nonqualifying attempts:

- Original Mac FLAC cancellation fired after 5088 ms, but the user requested a
  retry, so prompt appearance/closure is unconfirmed.
- Two retries started before the timer (1866/4087 ms, stop_fired=no). The user
  reports no prompt for either. Neither is pending-consent cancellation evidence.
- Mac WAVE returned receiver HTTP 400 after 15.39 seconds, before active playback.
  This timing is consistent with historical consent expiry, but the cause is not
  established. The driver stopped the matrix immediately. Seven following cases
  never started. Retain the later user batch Yes reply without converting it to
  eight passes.

The user questioned repeated cancellation for unchanged code. **Scope correction:**
no further cancellation retest is required in D107. Reuse earlier lifecycle
evidence; repeat manual cancellation only for relevant lifecycle/routing/ownership
changes or a concrete defect. Do not re-run it automatically with every pass.

On explicit resume, coordinate only the remaining necessary format/timeline checks.
Mac WAVE needs a retry with prompt readiness; Opus, Vorbis, MP4 FLAC, MOV PCM,
Matroska FLAC/PCM and edited PCM have not run. Apple TV's remaining progressive
and conversion-matrix cases have not run. Reassess redundant coverage from the
new-path evidence before requesting more manual work. Do not reset pairing,
network or consent settings to force a prompt. Refresh the unchanged build/source
stamp before new casts; keep the hardware/user deferrals intact.

The [partial validation record](validation/automatic-audio-only-native-windows-2026-10-20.json)
retains fixture/build hashes, all eleven attempts, separate observations and the
pause. The gate is not complete. Packaged automatic audio-only validation follows
the native gate and review/merge; Screenbox routing/queue work remains later.
