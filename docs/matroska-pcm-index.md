# Selected Matroska PCM packet index (D79)

**D81 extension:** [compact restart anchors](matroska-compact-index.md) stream
canonical selected audio without full packet tables, lifting the old decoder
packet-count cap under unchanged read/work bounds. Selected copy/remux policy
and public reporting remain next; synthetic scaling is not real-film proof.

The user's Proceed on 2026-10-14 authorizes the first long-file indexing increment
from merged PR #59 (`9763189`). Its final head `2bc7a5c` passed all 16 checks in
[run 38510002555](https://github.com/ilyalissoboi/send-airplay2/actions/runs/38510002555).
This slice establishes native selected-packet coordinates and exact PCM seeking.
Compressed duration/anchor reconstruction follows separately; automatic delivery,
public reporting, expanded packaged runtime and converted-receiver gates remain.
[PR #60](https://github.com/ilyalissoboi/send-airplay2/pull/60) merged as `531ff58`;
all 16 checks passed at final head `a2b3953` in run `38532987731`, with the precision
review resolved. Do not resume its closed branch. [D80](matroska-compressed-index.md)
extends this foundation to compressed durations/restarts under the retained bounds.

## Native coordinates and bounded construction

`read_mkv_audio_packets` selects the original enabled audio TrackNumber without
codec filtering, default-track fallback or order-based substitution. It scans
cluster/block headers with 256-byte read windows, skipping payloads and other
tracks. Coordinates retain payload offset/size, stored timestamp in nanoseconds
and signed per-block DiscardPadding. CodecDelay remains separate track metadata.
Original/disabled/ambiguous IDs, ContentEncodings, nonstandard track clocks,
selected lacing, malformed padding and metadata changes after the first cluster
are refused. Unselected padding is not interpreted. Signed minimum and nanosecond
overflow are checked at their appropriate native/decoder boundaries.

At most 262144 selected packets are retained. The native vector is at most 8 MiB;
the decoder's packed PCM index is about 10 MiB on the recorded x64 build. Temporary
construction vectors add bounded overhead; this is not a PCM cache. Existing
256-MiB requested-byte/1,048,576-callback and absolute deadline/cancellation limits
cover probing and indexing together. A packet-count refusal does not fall back to
an unbounded scan or decode. The number of packets, rather than file duration alone,
limits admission; heavily packetized movies can hit this limit.

## PCM clock and trim

Canonical Matroska PCM, under both coarse and fine clocks, now uses this index.
Validate the existing exact codec/packing/rate/layout subset, then count source
sample frames as `packet bytes / (channels * bytes per sample)`. Seed the initial
position from the stored clock, subtract CodecDelay once and accumulate actual
packet sample counts. Observed timestamps must agree within the original tick
plus one source sample. The initial absolute origin remains container-limited;
the output still reports that original precision, including 48 samples for 1 ms.
Admission retains the existing one-second absolute clock limit before replacing
the stream time base or narrowing precision. Three additional owned clock controls
check the literal 48000-sample boundary and refusal one nanosecond beyond it.
A third inventory-accepted tick would exceed `UINT32_MAX` output samples; the
pinned backend already refuses its scale above `UINT_MAX` nanoseconds during
format open, before the index guard. This is a backend admission result, not
direct runtime coverage of the narrowing guard at that unreachable scale.

Windows find the packet at/before target minus preroll by binary search, flush the
decoder, read nearby selected payloads and supply source-sample packet PTS/duration.
Packet skip/discard is bounded to 65536 samples and applied once by the decoder.
Whole-packet trimming and selected lacing remain outside this first index subset.
The existing ten-second **window**, 20-second decoded-input and seven-day requested
presentation bounds remain. Indexed PCM HLS is no longer limited to a ten-second
movie; coarse FLAC/Opus/Vorbis/TrueHD retain D78's movie horizon.

The combined trim fixture exposed a pinned-backend behavior: FFmpeg's generic
[demux skip propagation](https://github.com/FFmpeg/FFmpeg/blob/n9.0.2/libavformat/demux.c)
can replace first-block skip side data with track initial padding. The PCM path
constructs packet trim from native fields and avoids that overwrite. This is not
a generic FFmpeg fix or proof for compressed families/noncanonical standalone
decoding. Their next timing gate must include an independently prepared audible
control for CodecDelay plus negative first-block padding; a same-demuxer comparison
alone can reproduce the same error on both sides. Preserve earlier tests as their
recorded evidence rather than expanding their trim claims.

## Owned validation and scaling scope

[Five owned fixtures](../tools/audio-codec-spike/fixtures/pcm-index/provenance.json)
contain nominal 30.003-second stereo PCM at 44.1 kHz, packet lengths
997/1537/2111/641 samples and a short tail, alongside copied original all-IDR video.
[The generator](../tools/audio-codec-spike/generate_pcm_index_fixtures.py) records
input/hash/sample provenance and uses no external encoder or new dependency.
Coarse/fine windows compare at MSE <1e-10 without shift searches. Six AAC segments
requested cold-last and out of order must equal the fine-clock controls byte for
byte; the independent MOV/AAC oracle checks the literal 1464000-sample video end.

The trim case declares CodecDelay 132 source samples, first-block skip 192 and
final discard 132. Its independently prepared control physically inserts 60
silent frames and removes the appropriate source samples, with no trim metadata.
Expected audible end is the literal 1439857 output samples, independently calculated
as the ceiling of `(1323132 - 132 - 132) * 48000 / 44100`. Cold/repeated tails are
144 samples. Raw fine-clock and coarse indexed inputs must match that prepared
control at the beginning, middle, EOF and generated AAC joins.

[The virtual source](../tools/audio-codec-spike/virtual_audio_source.h) synthesizes
one hour of original one-second PCM/video cycles with all clusters above 4 GiB.
Only headers and one payload cycle are allocated. Opening must request <64 MiB
and read <4 MiB actual PCM. Cold/backward/forward windows request <32 MiB and
read at most three one-second PCM packets; independent literal tone samples,
144-sample EOF, source failure and failed-object reuse are checked. These are
synthetic callback/index bounds, not real-film, brokered-host or receiver timings.
The recorded local x64 run requested 31772981 bytes and read 1657860 actual PCM
bytes at open. Each of four near-end/backward/middle/repeated windows requested
11576608 bytes and read 384000 PCM bytes, with literal sample expectations.
The source uses several sized Void elements below 1 GiB because the pinned
FFmpeg EBML skip helper takes a signed-int length. A single multi-gigabyte Void
caused resynchronization/read-budget refusal in the initial synthetic attempt;
that container form remains outside this scaling evidence.

Native baseline tests independently check payload coordinates, original IDs,
signed values/sizes, lacing refusal and literal 262145-packet rejection. The
standalone codec suite now has `matroska_pcm_index` alongside the retained seven
checks; CI builds its target explicitly. Static/shared, enabled shared and x64
UWP compilation/import gates are required. Actual-head CI remains the merge gate.
Recorded local gates: standalone codec 8/8, baseline static/shared 36/36 and
37/37, enabled shared 38/38, standalone/integrated x64 UWP compilation and
AppContainer/import checks. Formatting, whitespace and owned fixture hashes are
part of final validation. These do not add an expanded packaged-runtime result.

## Next gate

Add codec-aware exact durations/anchors for FLAC, Opus, Vorbis and TrueHD, with
variable blocks, first/final trim, large cold seeks and sparse-index bounds.
Do not extend the start-rebuild horizon or infer a fixed cadence for those inputs.
Real-file startup/seek/memory and brokered storage measurements still follow;
native/CI success is not receiver interoperability. No ABI, package publication
or Screenbox pin changes; the pin remains `0.3.0-ci.207`. Audio-only speakers and
discrete converted surround remain hardware-deferred.
