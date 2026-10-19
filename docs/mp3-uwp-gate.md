# D104: packaged UWP progressive MP3 gate

2026-10-19 (Asia/Tokyo), in progress. The user authorizes this slice after
native #88 merged as be5ae92, with all 17 checks passing at final head 552ebca in
[run 39201242905](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39201242905).
Both recorded receivers are available; macOS 26.7.1/tvOS 26.6 are unchanged.

## Scope and planned checks

The test host adds an MP3 picker entry, audio/mpeg for progressive form casts,
and explicit type=audio/mpeg scripts. Explicit MIME still requires progressive
delivery and unknown types refuse before brokered file access. Default video
behavior, native API/playback, codec profile and Screenbox are unchanged.
The default capability package advances to 0.1.32.0, with the alternate-server
manifest 0.1.33.0. Only the default codec-free x64 package is planned for this gate;
update registration in place to preserve identity, PasswordVault and file grants.

Use the unchanged original 64-second stereo CBR 48-kHz and VBR 44.1-kHz fixtures
from tools/mp3-probe/fixtures, with their committed provenance/hashes. The user
grants each file manually and provides written listening/prompt observations.
No Computer Use visual confirmation or private media is required.

Inspect the actual MSIX, AppContainer and loaded executable/two native modules.
For both CBR/VBR files on each receiver: request start near eight seconds,
pause/resume, seek to 32 seconds (higher BOTH marker), back to eight (RIGHT),
then near 58 and allow natural end. A separate cast from zero gets Stop after
eight seconds, followed by a three-second observation interval. Mac unanswered
consent cancellation is a separate manual Stop check. Record attempts, exact
source release counts and telemetry separately from user observations.

Build, runtime, negative-script and receiver results are pending. Native D103
success is historical evidence for its exact binary bytes, not a packaged pass.
No AAC fallback is currently justified by that native gate. Public/default MP3
admission, automatic audio-only conversion and Screenbox routing/queues remain
later slices. Preserve speaker/HomePod/RAOP, discrete surround, Chromecast,
remote-Stop classification and separate long-input/cache-quota deferrals.
