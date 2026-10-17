# Native AAC/M4A audio-only URL feasibility

2026-10-17 (Asia/Tokyo), D97. The user asks whether existing MacBook Pro/Apple TV
can test audio-only casting, then authorizes the proposed bounded probe.
The result is positive for the recorded native progressive path: both receivers
play the true audio-only fixture and pass the user's listening/control checks.
No production code, dependency, public ABI, package or Screenbox behavior changes.

## Fixture and sender

The original [fixture generator](../tools/audio-only-probe/generate_fixture.py)
uses Python's standard library to write 64 seconds of 48-kHz stereo 16-bit PCM
tones, then external FFmpeg 9.0.2 encodes AAC-LC at 192 kbit/s in faststart M4A.
ffprobe verifies exactly one AAC-LC stereo stream and no video. No private file
or third-party fixture content is used. The generator/test signal follows the
repository Apache-2.0 license; FFmpeg/ffprobe are existing external development
tools, not newly shipped dependencies. The generated binary stays uncommitted.

Fixture SHA-256:
`6f951e78d72ea8be4a687bc8dd1b12f0c1843dbaba41a56e3edde265d4c17d16`
(1,209,466 bytes). Each second has 0.75 seconds of tone and a quarter-second gap,
with short fades. Eight-second intervals identify left 440 Hz, right 660 Hz,
both 880 Hz, left 440 Hz, both 1100 Hz, right 660 Hz, left 440 Hz and both 880 Hz.
The test does not change receiver volume.

The existing static `airplay2-api-host` is rebuilt with CMake from merged source
`449457aaac2a1f5112c0fdf1d12ec32cac839620`, codec backend OFF. It serves the M4A
directly with `--content-type audio/mp4`, without remux, automatic audio policy,
transcoding or a dummy video track. Its SHA-256 is
`f23234515d45d10cb28d758a7e785f0c7743455feabb3cc81c86adb345f306c5`.
Both loaded OpenSSL/Botan dependency hashes match the recorded build on every
cast. A current shared build is used only for private discovery; addresses and
credential contents are not retained. Five existing static HTTP/range/source/
controller/URL-session CTests pass separately from receiver results.

The first five-second discovery scan misses the Mac while finding Apple TV.
After the user confirms both available, a retry finds the recorded Mac. No
playback attempt is made against a guessed address or an unobserved receiver.

## Observations

The user explicitly confirms both available with macOS 26.7.1/tvOS 26.6 unchanged.
The Mac model is Mac14,10; Apple TV is AppleTV14,1. Written instructions and
user listening are used; no Computer Use visual confirmation is used.

| Case | User observation | Sender result |
| --- | --- | --- |
| Mac pending-consent cancellation | Prompt appears/closes; user says Yes | Cancelled near 5.05 s; zero media reads; one source release |
| Mac AAC/M4A controls/natural end | Audible tones, pause/resume, both seeks, natural end; Yes | All controls ok; media_end at 64 s; clean exit |
| Mac separate Stop | Audible playback stops cleanly; no delayed restart; Yes | sender_stop; clean exit |
| Apple TV AAC/M4A controls/natural end | Audible tones, pause/resume, both seeks, natural end; Yes | All controls ok; media_end at 64 s; clean exit |
| Apple TV separate Stop | Audible playback stops cleanly; no delayed restart; Yes | sender_stop; clean exit |

The controls sequence reads status, pauses after four seconds, resumes after
seven, seeks to 32 seconds, back to eight, then to 58 before natural end. This
proves bounded playback/seeks/end on a 64-second input, not uninterrupted playback
of all 64 seconds. Separate Stop casts end eight seconds after startup. All five
processes exit zero; every summary has no media failure, media cleanup and exactly
one source release. The four successful playback runs report joined controller
cleanup and zero failed reads; the consent-cancel case never reaches playback
and keeps its startup controller `cleaned=no` flag, with media cleanup/release
complete. Preserve these distinct flags rather than equating them.

See the [sanitized evidence](validation/audio-only-url-probe-windows-2026-10-17.json)
for timestamps, module/fixture hashes, control/state summaries and observer scope.
Sender playing telemetry alone is not the audible result; the user's independent
Yes responses establish that observation for these two receivers.

## Consequences and next slice

The native public progressive path already supports this bounded AAC/M4A use
case when the host supplies the audio MIME type. No new transport implementation
is needed to demonstrate it. The current HLS-remux/automatic-audio paths still
require video, and the released Screenbox preview has not gained audio-file
routing through this experiment. Audio-only AAC success does not establish MP3,
FLAC or other file handling, audio transcoding, long-file behavior, queue handoff,
packaged UWP behavior, HomePod or a RAOP/audio-packet sender.

The proposed next slice is an owned AAC/M4A brokered-source gate in the packaged
UWP host using progressive delivery and explicit audio MIME, then Screenbox
audio-file routing/controls. Audio-only HLS and broader codec conversion should
be designed as separate changes. The original speaker-inclusive requirement
remains; actual speaker transport/compatibility stays hardware-deferred. The
probe does not resume discrete converted surround, Chromecast validation or
user-deferred remote-Stop classification. No new release is published.

The separate UI/publication [checkpoint PR #79](https://github.com/ilyalissoboi/send-airplay2/pull/79)
is merged as `7403044`. Do not repeat its completed release work. The
[continuation queue](CONTINUATION.md#open-issues-and-ordered-next-work) and
suggested opening prompt now match D97's packaged-UWP-then-Screenbox order;
this documentation review starts no new implementation or receiver gate.
