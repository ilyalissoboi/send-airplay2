# Bounded Matroska DTS seeks (D87)

The user's Resume work and confirmation that PR #67 merged authorize this slice
from `origin/main`, merge `8a284ad`, on `codex/matroska-dts-bounded-seek`.
All 16 checks passed at PR #67's final head `9064f78`, run `38677650564`.

## Cause and bounded correction

D86 converted the authorized brokered video's first fragment, but its middle
request exhausted 256 MiB requested source bytes. A read-only diagnostic around
the unchanged decoder locates that refusal inside `avformat_seek_file`, before
the first selected packet reaches window decoding. The selected audio index has
13 initially learned entries; the already parsed video index has 10292 entries.
Seeking on the audio index scans forward to extend it. A diagnostic-only video
coordinate reaches the same middle window within the unchanged read budget.

FFmpeg's [Matroska seek implementation](https://ffmpeg.org/doxygen/trunk/matroskadec_8c_source.html#l04517)
corroborates per-stream index extension by cluster scanning. That moving source
is explanatory; the owned reproduction and selected runtime establish behavior.
No FFmpeg implementation source is copied into this Apache-2.0 adapter.

Canonical Matroska DTS now compares already parsed, bracketing keyframe entries
on the selected audio and a unique video stream. It uses the nearer preceding
coordinate, preferring audio on ties. This changes only where the demuxer seeks:
selected audio identity, discarded streams and conversion route stay fixed.
Video coordinates first back off by the maximum 256-frame DTS lace span: a cue
inside a long lace must not skip the earlier block containing required history.
The existing maximum-lace regression exposed this case during development.
Comparing distances matters after a late seek: dynamically learned audio entries
can bracket a large unvisited gap. Missing, ambiguous, malformed or uncovered
coordinates retain the existing bounded seek path. No seek/read failure triggers
an alternate-coordinate retry. The decoded first frame must cover the requested
preroll within the reported clock precision; a late landing refuses rather than
inventing leading silence. Default decoding, other codecs and MOV seek rounding
remain unchanged. Byte/callback/deadline, PCM-history and cache limits remain.

## Independent owned controls

The original [synthetic source](../tools/audio-codec-spike/virtual_dts_seek_source.h)
reuses the first 96 existing Apache-2.0 DTS-core packets and one owned AVC frame
from `fixtures/hls/dts.mkv`. Each 1.024-second cluster carries 96 512-sample audio
frames in eight-frame fixed laces, plus two AVC samples with original filler NALs.
128 clusters expose a virtual movie exceeding 1 GiB while allocating only headers
and one payload cycle. No large media file is stored or committed. Audio-cued
and video-only-cued variants differ only in index metadata, with identical media.

Before correction, the new control reproduces the read-budget refusal in 0.19 s.
After correction, cold tail, backward middle/early, repeated tail and literal
1456-frame EOF windows match the audio-cued control sample for sample without
correlation shifts, retaining original ID 42, six channels and 48-sample absolute
precision. Each request stays below a literal 64-MiB test ceiling. Missing cues
still exhaust exactly 256 MiB, misleading late cues refuse insufficient preroll,
failed owners perform no more callbacks, and a source read failure is not retried.

## Validation results

The focused seek/lace tests pass (2/2, 8.13 s), including the retained 256-frame
PCM/AAC control. Default static/shared suites pass 39/39 (49.89 s) and 40/40
(54.73 s). The actual uninstrumented desktop adapter now accepts the private
middle window with 76,775,424 requested source bytes; this direct decoder
diagnostic is separate from the packaged benchmark below. Final enabled shared
CTest passes **41/41** (105.99 s); standalone codec CTest passes **14/14** (75.98 s).
Four touched C++ files pass formatting; whitespace, 145 owned fixture hashes and
relative documentation targets pass. Six UWP DLLs retain the x64 AppContainer/app
CRT profile; private hooks remain absent from the default shared build.

Private package **0.1.0.8**, core/package source `731db15`, passes bundled and
hash-verified folder-granted **137 automatic cases across 61 fixtures** each,
retained legacy/public lifetimes and both inter-block gap refusals. All three
runs, including the brokered benchmark, have actual AppContainer token and six
loaded DLL hashes verified against registration and the native build. Every
tested source/store releases once, removes its child and preserves sibling data.

The user granted the same authorized video through the Windows file picker.
Each benchmark mode prepares a fresh owner, generates one requested fragment,
verifies a byte-identical cached repeat and cleans up. Overall result: **PASS**.

| Window | Preparation | Cold fragment | Cached repeat | Requested source bytes for request/repeat | Sealed bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| First | 168 ms | 138 ms | 7 ms | 39,682,048 | 104,559 |
| Middle | 167 ms | 314 ms | 13 ms | 76,775,424 | 144,607 |
| Last | 197 ms | 64 ms | 0 ms | 3,179,339 | 282 |

Each preparation makes 1542 calls requesting 45,395,831 source bytes
(45,179,352 returned). The middle request makes 2343 calls, returning 76,730,907
bytes, within the original 256-MiB requested-byte ceiling. The last cached
request is below the millisecond timer's resolution; zero does not mean no work.
The process capture records 1546.875 ms total CPU, 106,692,608-byte peak working
set and 132,493,312 private bytes, including startup/picker/all three owners.
These are whole-process snapshots, not per-phase memory/CPU measurements.
Cold means a fresh owner/window/cache; the OS page cache is not flushed.
This establishes three bounded samples on one host/input, not sustained
whole-film throughput, audible output, A/V synchronization or receiver EOF.
[Sanitized evidence](validation/matroska-dts-bounded-seek-windows-2026-10-15.json)
records categories, counters, run IDs and module hashes without private input identity.

Next gate: manually observe copied and converted playback on the recorded
Apple TV/Mac receivers, including selected audio, stereo output, pause/resume,
forward/backward seeks, Stop/cancellation and natural EOF/queue behavior as
applicable to the native and packaged hosts. Keep receiver/firmware and host
results explicit. No receiver was contacted here. Private input identity/content
stay outside committed evidence. No publication or Screenbox pin change;
speaker transport and discrete converted surround stay hardware-deferred.
