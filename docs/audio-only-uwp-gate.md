# Packaged UWP AAC/M4A progressive gate

2026-10-18 (Asia/Tokyo), D98, gate passed on both recorded receivers.
The user authorizes the packaged UWP follow-up after native probe PR #80 merges as `377b7ce`; all 17 final-head
checks pass at `b105333`. Native D97 audio-only playback is not packaged proof.

Test-host implementation `153a840` adds explicit `type=audio/mp4|video/mp4`
for progressive scripts, an `.m4a` picker entry and sanitized source release
counters. The form supplies audio/mp4 for `.m4a` when progressive is selected.
Explicit types with remux/automatic delivery and unsupported explicit values
are rejected before opening a file. Existing defaults remain video/mp4; no
native core/API, decoder, Screenbox or transport implementation changes.
The source observer runs after its brokered streams close; callback failures
cannot prevent that completed cleanup. It logs only counts and remains optional
for the shared offline validation adapter.

## Build and runtime checkpoint

The current codec-free x64 UWP build passes the app C runtime, AppContainer,
no desktop Credential Manager import and no Botan DLL checks. VS 2026 MSBuild
builds the actual Native AOT MSIX without warnings/errors. Package 0.1.30.0 uses
the default internetClient/privateNetworkClientServer capabilities on a Private
Windows network; it adds no internetClientServer. Both native DLLs match build
output and no codec DLLs are present. Registration updates 0.1.29.0 in place,
preserving identity, PasswordVault credentials, grants and other app data.
The running executable/AppContainer and both loaded native hashes match the
inspected package. Local negative scripts reject the unsupported MIME and
explicit audio type with HLS before file access or a receiver request.

The fixture is D97's original 64-second stereo AAC-LC M4A, zero video streams,
SHA-256 `6f951e78d72ea8be4a687bc8dd1b12f0c1843dbaba41a56e3edde265d4c17d16`.
No private media is used. The user grants file picker access manually, remembered
under the fixed label audio-only. The app logs the expected 1,209,466-byte M4A.
Subsequent scripts use that brokered grant, existing PasswordVault profiles and uniquely model-selected receivers; names,
addresses, PINs, URLs and source paths are not logged.

## Receiver observations and attempts

The user confirms both receivers available with macOS 26.7.1/tvOS 26.6 unchanged.
All listening/prompt observations come from written instructions and independent
user replies; no Computer Use visual confirmation is used.

| Case | User observation | Sender evidence |
| --- | --- | --- |
| Mac first consent attempt | No successful cancellation claim | Consent expires after 15.159 s; no Stop, zero reads, one release |
| Mac consent cancellation retry | Yes: prompt closes; app remains responsive | Stop recorded; cancelled after 1.802 s; zero reads, one release |
| Mac first two playback attempts | No successful audio sequence | Both fail connection setup near 5 s, zero reads, one release each |
| Apple TV controls/natural end | Yes: tones, pause/resume, both seeks, natural end | All controls ok; MediaEnd at 64 s; one release |
| Apple TV second cast/Stop | Yes: clean Stop, no delayed restart | SenderStop, cleaned=yes; one release; three-second post-Stop wait |
| Mac later controls/natural end | Yes: all requested checks pass | All controls ok; MediaEnd at 64 s; one release |
| Mac second cast/Stop | Yes: clean Stop, no delayed restart | SenderStop, cleaned=yes; one release; three-second post-Stop wait |

The first cancellation attempt is a timeout, not a cancellation pass. The later
retry verifies cancellation. The two pre-media connection failures remain in the
record; the successful Mac retry uses the same package and script. Their
cause is not isolated, and this gate does not claim to repair it.

For each receiver, playback starts at zero, pauses/resumes, seeks to 32 seconds,
back to eight, then to 58 before natural end at 64. This is a bounded controls
check, not uninterrupted playback of all 64 seconds. A second cast in the same
process checks slot reuse and Stop eight seconds after startup; the script waits
three seconds afterward. Each successful playback source reports 20 reads,
1,209,468 cumulative bytes and exactly one release. Cumulative reads can overlap,
so the byte count need not equal unique file size. All eight source instances,
including the three unsuccessful attempts, release exactly once. Duplicate
dispose messages in the cancellation race do not imply duplicate source release.

Natural-end telemetry is captured before disposal and reports cleaned=no;
the subsequent release proves brokered-source cleanup. Explicit Stop reports
cleaned=yes. Cancellation never reaches playback and retains cleaned=no, with
source release complete. Preserve these distinctions. Progressive delivery
requests no conversion fragment store. No credentials, app data or grants are
reset. Final AppContainer/executable/two loaded DLL hashes still match the
inspected MSIX. No further D98 manual check is pending.

See [the sanitized evidence](validation/audio-only-uwp-gate-windows-2026-10-18.json)
for package/source hashes, attempts, exact control/state lines and observer scope.
PR #81 is merged as `45f39be`. All 17 CI checks pass at final head `0c16c4a` in
[run 39092030693](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39092030693).
This adds merge/CI evidence only; build/CI success and audible receiver
observations remain distinct.

## Next slice

Merged PR #81 records the completed UWP gate. The next proposed slice is
Screenbox AAC/M4A progressive routing with explicit audio MIME, controls and
queue/lifecycle handling. Inspect that repository's current instructions and
scope the change after user authorization. The existing automatic-audio HLS
path still requires video; it must not be used as an audio-file fallback.

This gate starts no Screenbox audio-file implementation or release. Only the
test host gains explicit progressive test options. Other codecs, long audio
files, audio-only HLS/conversion, HomePod/RAOP, discrete converted surround,
Chromecast validation and user-deferred remote-Stop classification remain
separate/deferred scopes. The published fork preview is unchanged.
