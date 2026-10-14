# Selected Matroska compressed-audio index (D80)

**D81 extension:** [compact restart anchors](matroska-compact-index.md) supersede
D80's full decoder packet table and its packet-count cap. The legacy vector helper
retains that cap. D80 below is the dated foundation and evidence, not the current
retained-memory contract. PR #61 merged as `00c845d` after all 16 final-head checks
passed at `27cbb1e` in run `38555473407`.

The user's Proceed on 2026-10-14 authorizes the next indexing increment from
merged [PR #60](https://github.com/ilyalissoboi/send-airplay2/pull/60), `531ff58`.
Its final head `a2b3953` passed all 16 checks in
[run 38532987731](https://github.com/ilyalissoboi/send-airplay2/actions/runs/38532987731).
The implementation branch `codex/matroska-compressed-index` is merged; do not resume it.
This adds codec-aware durations and restart coordinates to D79's native selected
packet path. It adds no automatic policy, ABI, package or Screenbox change.

## Clock, restart and trim contract

Canonical unlaced Matroska FLAC, Opus, Vorbis and TrueHD now use the native
original-TrackNumber index under both fine and coarse clocks. Packed PCM retains
its existing counting route. Source/backend identity and metadata validation still
precede indexing, with no fallback to another audio track.

Duration/restart analysis uses the already adopted FFmpeg 9.0.2 public parser API
in complete-frame mode, with its existing native parser configuration. The core
prefix optimization is specific to that inspected implementation; loaded component
versions must match the build headers before using it. It is not a portable
promise that arbitrary future parser versions accept partial packet prefixes.
The adapter is original Apache-2.0 code; no FFmpeg implementation is copied or patched,
no external codec library is added, and source/license provenance remains in
[dependencies.md](dependencies.md). The existing four-library, network/GPL/nonfree
disabled profile and default codec-free builds remain.

| Codec | Analysis and restart |
|---|---|
| FLAC | Padded prefix of at most 64 bytes; parser block sample count, including short tails. Each complete frame is restartable. |
| Opus | Complete bounded packet for framing/duration analysis. Each packet is a lookup candidate; decode at least one second of preceding history. |
| Vorbis | Padded prefix of at most 64 bytes plus validated setup headers; variable overlap duration. Decode preceding packets to rebuild overlap. |
| TrueHD | One access unit per Matroska block, with matching 12-bit word length. First unit must contain a major sync. Non-major durations inherit that sync; lookup restarts at the preceding major sync. |

Accumulate coded source-sample counts from the original first timestamp, subtract
CodecDelay once, and validate observed timestamps within the original tick plus
one source sample. The original clock must be no coarser than one second before
precision narrowing or clock replacement. Binary lookup and fresh decoder state
replace start reconstruction; the ten-second movie horizon is removed within
this admitted subset. The initial absolute origin still has container precision.

Report that original absolute precision (48 output samples for 1 ms), but validate
decoded-frame continuity using the exact indexed clock. Coarse reporting must
not conceal actual payload/trim gaps. A 16-sample interior-discard fixture checks
this distinction and failed-owner poisoning.

Native signed DiscardPadding supplies packet skip/discard side data. Negative
first-block skip replaces rather than adds CodecDelay; decoder consumption applies
the trim once per epoch. Positive final padding and actual decoder EOF bound the
audible tail. Coded-unit duration need not equal the final decoded sample count.
PCM still refuses complete-packet trimming; compressed packets admit equality.

Vorbis is a narrower first-trim subset: the native decoder intrinsically removes
its first complete overlap frame, overwriting a different skip value. Admit
CodecDelay/first skip within that intrinsic frame and no first discard. Smaller
or absent delay retains the corresponding initial presentation silence. A skip
beyond the intrinsic frame is explicitly refused instead of being silently lost.
This is not a generic backend
trim fix. Other intermediate trim gaps, lacing, encoded/nonstandard tracks and
format changes remain subject to refusal.

## Resource bounds and remaining scaling gate

The existing 262144 selected-packet cap, 256-MiB requested-byte/1,048,576-callback
per-operation budgets, cancellation/deadline and source bounds remain. A compressed
packet is at most 1 MiB; each header has zero padding for the parser. The decoder
index is about 12 MiB at the cap on x64, including restart coordinates; native
construction vectors add bounded temporary overhead. No complete PCM cache or
background source work is introduced. Window/input/presentation limits remain
ten seconds, twenty seconds and seven days respectively.

The packet-count cap still excludes many full movies. For example, 40-sample
TrueHD units at 48 kHz reach it after about 218 seconds, and 960-sample Opus packets
after about 87 minutes. These are consequences of the retained cap, not duration
guarantees. Opus analysis reads complete compressed packets and can also hit I/O
budgets. No refusal falls back to an unbounded scan or full-prefix decode.
**Compact streaming anchors and high-packet-count movie scaling remain a gate
before general automatic delivery.** Do not simply raise the limits.

## Owned offline evidence

[The generator](../tools/audio-codec-spike/generate_compressed_index_fixtures.py)
creates [20 owned/provenanced controls](../tools/audio-codec-spike/fixtures/compressed-index/provenance.json)
from synthesized tones and D77's original black all-IDR video. FFmpeg CLI is a
development-only encoder/decoder; its version, commands, inputs and hashes are
recorded. No downloaded sample or tool is shipped.

Thirty-second fine/coarse movies cover all four families. FLAC uses 44.1 kHz,
1537-sample coded blocks and a short tail to exercise rational resampling phase.
Cold late, forward/backward, repeated and adjacent cuts compare PCM against
ordinary fine-clock demuxing without the new index, at MSE below 1e-10 without
shift searching. Literal audible EOF is 1440144 output samples with a 144-sample
tail. Six AAC segments generated in order 5,0,3,1,4,2 match fine-clock fragment
bytes; an independent MOV/AAC decoder checks the 1464000-sample video end,
sealed-cache reuse and one-time cleanup.

Four short trim cases compare against physically prepared float PCM controls.
They decode the original elementary/Ogg source without authored negative block
padding, then independently insert leading silence and remove samples. Thus a
same-Matroska-demux skip overwrite cannot reproduce itself on both sides.
Literal audible ends are FLAC 95736, Opus 95960, Vorbis 95984 and TrueHD 95980.
Vorbis covers matching overlap priming; a separate extra-skip mutation is refused.
Two missing-CodecDelay controls preserve literal 1024-sample initial silence and
the 97024-sample end against ordinary fine-clock demuxing. This retains valid
zero-delay admission rather than requiring metadata equal to codec priming.
Malformed duration/restart headers, retained cancellation and source-failure
poisoning checks remain separate from positive evidence.

A synthetic one-hour FLAC source repeats an independently authored 48000-sample
block, stores only one payload cycle and metadata, and places clusters above
`0x1'0000'1000`. Literal tone samples and EOF validate cold late/backward/middle/
repeated windows. Recorded x64 measurements: open requested 9983339 bytes and read
1216644 payload bytes; each window requested 606600 and read 48192 payload bytes.
This is callback/index scaling for FLAC, not one-hour proof for every codec or
real-film/brokered-host performance. Sized Void elements remain below 1 GiB under
the pinned backend's existing skip limitation.

The retained fourteen coarse-clock cases include stereo and 5.1, PCM variants and
leading padding. Update their old reconstruction/horizon expectations to indexed
behavior while retaining phase, AAC, cancellation and failure coverage. The new
`matroska_compressed_index` CTest is explicitly built/run in codec CI alongside
the eight existing checks. Actual-head CI is a merge gate.
Recorded local validation: codec 9/9, baseline static/shared 36/36 and 37/37,
enabled shared 38/38, standalone/integrated x64 UWP compilation and import checks.
Formatting, whitespace, owned fixture hashes and relative documentation links
are checked before the PR. These are offline build/test results only.

Native/CI success adds no expanded packaged-runtime or receiver observation.
Real-file startup/seek/memory, brokered input/cache, automatic selected copy/policy
and versioned reporting remain work. Screenbox stays on `0.3.0-ci.207`; audio-only
speakers and converted discrete surround remain deferred for unavailable hardware.
