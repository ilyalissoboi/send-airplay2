# Compact Matroska audio restart anchors (D81)

The user's Proceed on 2026-10-14 authorizes this slice from merged
[PR #61](https://github.com/ilyalissoboi/send-airplay2/pull/61), `00c845d`.
Its final head `27cbb1e` passed all 16 checks in
[run 38555473407](https://github.com/ilyalissoboi/send-airplay2/actions/runs/38555473407).
The active branch is `codex/matroska-compact-index`; inspect its actual PR head.
This extends [D80](matroska-compressed-index.md) without a public ABI, delivery
policy, package, dependency or Screenbox change.

## Scan and reconstruction contract

Canonical unlaced PCM/FLAC/Opus/Vorbis/TrueHD retains the existing original-ID,
metadata, codec, clock and signed per-block trim admission. A constant-memory
native scanner yields one selected packet and a containing Cluster/block cursor.
It skips other tracks and payloads apart from bounded header read-ahead. Resume
coordinates belong to the same immutable source/selection. BlockGroup padding is
interpreted before yielding its selected packet; unselected padding remains ignored.
Sized and unknown-size Cluster handling uses the existing EBML reader.

Open still scans and validates every selected packet's codec duration and original
timestamp. It retains independently restartable anchors at least one source second
apart, rather than storing per-packet offsets/durations/trim/restart ordinals.
Each anchor holds a native cursor, exact source-sample position, packet ordinal
and preceding raw packet coordinate. The preceding coordinate is used only to
seed Vorbis's previous overlap mode; it is not sent to the fresh decoder as an
extra trimmed packet. TrueHD anchors begin at major sync. Its subsequent unit
durations inherit that sync, as in D80. FLAC/Opus/PCM restart as before.

A window binary-searches the anchor preceding its requested preroll and opens a
fresh native scanner/parser there. It reconstructs positions, durations and trim
while feeding nearby complete packets to the flushed decoder. The existing one-
second minimum history, rate-dependent resampler phase, exact decoded continuity,
original absolute precision and EOF drain remain. CodecDelay/initial skip applies
only at packet ordinal zero; Vorbis's bounded intrinsic initial overlap admission
is unchanged. Terminal scan ordinal/sample position must match the opening scan.
A failed window poisons the owner; no fallback rescans/decode from file start.

The legacy `read_mkv_audio_packets` vector helper still refuses packet 262145.
The canonical decoder now uses the scanner and is independent of that helper's
retained-table limit. Legacy video/remux frame limits are unchanged.

## Resource limits

At most **65536 anchors** are retained, with explicitly bounded vector capacity.
An anchor is 72 bytes on the recorded x64 build: at the cap, coordinates occupy
4.5 MiB. No complete PCM or compressed-payload cache is introduced. A private
adapter statistic reports scanned packets, retained anchors and allocated anchor
bytes for offline scaling controls; it is not a C ABI/export.

Two fixed header pages serve both codec and container reads. They use 1024-byte
pages for small TrueHD/Opus/Vorbis packets and 256-byte pages for PCM/FLAC. Adjacent
pages permit headers crossing a boundary without repeatedly re-reading the same
tiny units. Larger complete Opus packets use direct bounded reads. Cache hits
still check cancellation; refills share the unchanged requested-byte/callback
budget with AVIO and payload decoding. Pages are invalidated at every operation.

Retain **256 MiB requested bytes / 1048576 callbacks per decoder operation**, the
separate native inventory-probe budget, caller deadline/cancellation, 1-TiB source,
seven-day presentation, ten-second window, twenty-second input and 1-MiB compressed
packet bounds. The old decoder packet-count refusal is removed by changing what
is retained, not by increasing read/work limits. A long gap between restartable
units, a short-read-heavy host or a large/expensive scan can still exceed these
budgets. Open remains linear in selected packet count, not lazy or constant-time.
The high-count TrueHD control is near the byte ceiling and uses a small owned
encoded cycle; higher-bitrate real files are not promised to fit. Source-scan I/O
and brokered-host performance remain necessary profiling gates.

## Owned offline controls

[The high-count test](../tools/audio-codec-spike/compact_index_tests.cpp) extends
[the sparse source](../tools/audio-codec-spike/virtual_audio_source.h), retaining
only headers and one original one-second cycle. Clusters remain above
`0x1'0000'1000`; each has 1200 original TrueHD units or 50 Opus packets. The new
[Opus silence generator](../tools/audio-codec-spike/generate_compact_index_fixtures.py)
uses the native development-only FFmpeg 9.0.2 encoder with `opus_delay=20`.
[Its provenance](../tools/audio-codec-spike/fixtures/compact-index/provenance.json)
records the input/output hashes and command. TrueHD reuses D80's owned tone
fixture, with literal independent tone samples rather than a decoder oracle.

- One hour of TrueHD scans **4320000 packets**, retaining 3600 anchors.
- Two hours of Opus scans **360000 packets**, retaining 7200 anchors.
- Cold late, backward, middle and repeated windows check known tone/silence,
  nearby requested bytes and offsets above 4 GiB. Literal audible EOF is
  172800000 TrueHD frames or 345599880 Opus frames, with a 144-frame tail.
- Opus's initial 120-frame pre-skip is applied once across all repeated cycles.
- Cancellation and failed-owner reuse retain the no-new-callback guarantee.
- Literal anchor count 65536 is admitted with bounded allocation; 65537 is refused.
- Native codec-free tests retain literal signed padding, unselected-track behavior,
  stable EOF and backward/forward same-Cluster resume. Streaming 262145 packets
  succeeds while the retained vector helper still refuses that count.

High-count callbacks deliver complete requested pages across synthetic boundaries;
the existing PCM/FLAC and timing/trim controls retain short reads. This avoids
mistaking repeated artificial AVIO short reads for scan cost. Existing fine/coarse,
variable 44.1-kHz resampling, independent trim, malformed headers, exact interior
continuity and byte-identical AAC/HLS controls remain in the full codec suite.

## Validation and next gate

Local Windows x64 Release checks pass: full codec **10/10** (59.22 s), static
**36/36** (46.72 s), shared **37/37** (50.13 s) and enabled shared **38/38**
(63.24 s). After adding mixed unknown/sized Cluster resume controls, native MKV
checks pass in both static/shared builds. After the final bounded-capacity and
readability edits, compact/compressed checks pass **2/2** (12.93 s); compact alone
is 1.88 s, including admitted/refused anchor-boundary controls. The CI target and
CTest filter explicitly include the new compact check; no dependency cache key
or profile changes are needed.

Recorded final x64 direct metrics: TrueHD open requested **252582724 bytes**, with
**294912 retained coordinate bytes** and 3600 anchors; late windows requested
267892 bytes, early/middle 145476/146500. Opus open requested **14812385 bytes**,
with **589824 retained bytes** and 7200 anchors; each measured window requested
11319 bytes. Observed synthetic open CPU/wall time was about 1.1 s / 0.09 s in
this run, not a real-host/movie performance guarantee.

Standalone and integrated x64 UWP compilation pass; five probe/codec DLLs and six
integrated DLLs pass AppContainer/app-CRT/import inspection. Seven touched C++
files pass clang-format dry-run/Werror; whitespace checks, 81 D77-D81 fixture
hashes and 503 relative documentation targets pass. Default-codec tests needed
OS credential/loopback access outside the sandbox; the enabled build also needed
its existing codec DLL directory on the process PATH. Successful reruns above
supersede those local setup failures. Inspect the actual PR-head CI before merging.
None is receiver interoperability. No real
film, expanded packaged-runtime session, converted receiver or new Screenbox pin
is included. Speakers/audio-only transport and discrete converted surround remain
hardware-deferred.

Next is selected-track copy/remux eligibility and automatic stereo-AAC delivery
under the explicit profile in [the policy plan](automatic-audio-transcoding-plan.md),
then versioned public reporting. Keep source-scan budget refusals explicit and
profile real-file/brokered-host work before general performance claims. Expanded
packaged runtime and converted-receiver gates remain.
