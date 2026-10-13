# Native stereo AAC readiness (D72)

The user's latest Proceed authorizes this increment after PRs #51/#52 merged.
Fresh branch `codex/aac-hls-readiness` starts from `c092c9b`. It strengthens the
optional private [HLS builder/cache](stereo-aac-hls.md); public playback, C ABI,
bindings, CLI, packaged runtime and receiver integration remain subsequent work.
Speaker audio and discrete 5.1/7.1 output remain deferred for unavailable hardware.

## Long video GOPs

Video packet bytes and random-access boundaries stay copied. Audio can now have
more segments than video: usable video boundaries snap down to the absolute
48 kHz / 1024-sample AAC grid; gaps longer than seven seconds get additional cuts
on the absolute six-second grid. Subframe intermediate cuts are skipped, and the
final movie end remains exact. No duration rounding is accumulated. Existing
short-GOP cuts are preserved. The input admits at most 100802 increasing boundaries
from zero, with a seven-day end limit. Invalid bounds fail before source I/O.

Each audio span remains at most seven seconds, keeping preroll/lookahead within
the ten-second PCM epoch budget. Common audio/video/subtitle target durations are
retained. A long video GOP still produces a long copied-video segment: splitting
audio does not establish HLS authoring suitability or receiver playback for that
video. No video encoder or synthetic keyframe is introduced.

The original `aac-long-gop.mp4` fixture contains 33 H.264 frames at 2 fps, with
one sync frame and encoded P-frames. Its 16.5-second video stays in one segment;
audio uses three spans (5.994667, 5.994667, 4.510667 seconds), with common target 17.
Independent MOV/AAC decode has exactly 792000 stereo presentation samples.
Fixture generation commands, license and SHA-256 are in the existing HLS
`provenance.json`; prior fixture bytes are unchanged.

## Terminal failures

`failure()` reports the first fixed private category: unsupported, malformed,
source error, codec error, resource limit, cache error or generation timeout.
Publishing a fatal conversion, seal or storage-read failure cancels shared work
and rejects all generated audio fragment requests, including earlier sealed fragments. Zero or
oversized reads before cached EOF are storage failures. Raw host exception text
is discarded. The category persists through Stop; intentional Stop does not
replace it or create a failure.

Individual request cancellation/deadline and full-queue admission remain local:
they do not poison a cast or cancel shared generation. There are still four
pending jobs plus one generating job, with an independent 30-second absolute
generation deadline. Callbacks must cooperate; native code cannot interrupt a
noncooperative host. Expiration of the full worker deadline is mapped in code;
this increment tests short request expiry and cooperative Stop, without adding
a 30-second idle test to every CI run.

The future playback controller must observe this signal and stop its media server
and receiver session. Video/playlist callbacks retain their existing remux lifetime
contract. Failure publication performs no join or storage cleanup on a callback
thread; explicit Stop joins the worker, drains active cache calls and clears once.
Tests cover source failure and seal failure after earlier successful output,
throwing/zero/oversized cached reads while another job is generating, fixed
diagnostics, retained-resource refusal, cancellation during a cache callback,
pending-job bounds and once-only cleanup. Public termination reasons are unchanged.

## Synthetic scaling evidence and its limits

An original callback-backed progressive MP4 indexes exactly one hour: 7200 video
samples at 2 fps and 337500 DTS frames at 512 samples / 48 kHz. Sample entries and
compressed packet cycles come from the owned `dts.mp4` fixture. Original tables
use `co64`; an extended-size sparse free box places media above 4 GiB. Only
metadata and one short packet cycle are allocated. No physical hour-long or
multi-gigabyte fixture, full-file conversion or full-movie PCM is created.

Startup seals no fragments, invokes host size once and is required to read at
most 32 MiB of metadata and 1 MiB of packet payload. Cold requests near the end
and then halfway backward each require at most 4 MiB of payload I/O and seal
only their requested fragment. Independent MOV/AAC decode checks literal global
timestamps, audible finite stereo output and exact final presentation duration.
These deterministic I/O bounds are CI assertions; timings are observations,
with no machine-dependent millisecond pass threshold.

Local Windows x64 Release observation on 2026-10-13: startup 22 ms,
metadata 16583104 bytes, maximum cold source read 1626020 bytes, maximum fragment
generation/read 260 ms, largest observed offset 4931071779. These values describe
the synthetic callback source, not real storage latency, a large movie's video
bitrate, peak process memory or Apple TV/Mac seeking.

All four optional desktop CTest suites pass (10.77 s total); static/shared
baseline suites pass 35/35 and 36/36. The x64 UWP private adapter compiles and the
existing codec DLLs pass AppContainer/app-CRT import inspection. This is no new
packaged-builder runtime or receiver test. The shared HLS oracle/helpers were
extracted without changing their assertions or existing fixture data.

Reproduce using the standalone cached codec build from
[the spike](stereo-aac-codec-spike.md): build `sap2_stereo_aac_readiness_tests`, then
`ctest --test-dir <codec-build> -C Release -R stereo_aac_readiness -V`.
CI adds the test to the existing desktop codec job and preserves the dependency
profile/cache; it adds no FFmpeg rebuild or extra codec matrix job.

## Remaining sequence

1. Resolve authoring metadata policy and validation, especially estimated
   BANDWIDTH/AVERAGE-BANDWIDTH and long copied-video segments. Broader video
   timelines and cold AAC synthesis limitations remain explicit; do not claim
   independent audio segments or complete authoring conformance.
2. Connect explicit stereo delivery and selected source identity through native
   playback/C ABI/bindings/CLI, with authorized per-cast host storage and terminal
   failure supervision. Preserve existing delivery defaults and optional builds.
3. Check real-file/host performance and cache behavior, then a new packaged runtime.
   Synthetic one-hour offsets are not the real-file gate.
4. Manually observe Apple TV/Mac video/audio, lip sync, cold/forward/backward seeks,
   pause/resume, Stop, natural end and queue continuation. Keep exact receiver,
   firmware, host and binary evidence. Ask before publishing or merging.

No receiver, UI confirmation or package publication is performed in D72.

Merge checkpoint: PR #53 merged as `25b84a1`; all 16 checks passed at final
head `6bf1bd2` in run `38336511817`. The subsequent [D73 authoring increment](stereo-aac-authoring.md)
adds a 20-second copied-video cap and measured first-period estimates/audits.
D72's 22 ms/1 MiB startup policy above is historical: D73's isolated calibration
adds one bounded decode/encode and admits up to 4 MiB startup payload I/O.
