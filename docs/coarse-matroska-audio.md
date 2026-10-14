# Bounded Matroska audio clock reconstruction (D78)

**D80 extension:** [compressed packet indexing](matroska-compressed-index.md) adds
codec-aware durations/restarts and independent trim controls within the retained
packet/I/O bounds. Compact high-packet-count movie scaling remains a gate before
general automatic delivery. No new receiver or packaged-runtime proof is added.


The user's Proceed on 2026-10-14 authorizes the next timing increment from
merged decoder PR #58 (`820e89c`). Its final head `129ae6e` passed all 16 checks
in [run 38504706633](https://github.com/ilyalissoboi/send-airplay2/actions/runs/38504706633);
the OutputSamplingFrequency review thread is resolved. This increment is private,
optional and offline. Automatic policy, public reporting and release remain later.
[PR #59](https://github.com/ilyalissoboi/send-airplay2/pull/59) merged as `9763189`;
all 16 checks passed at `2bc7a5c` in run `38510002555`. Do not resume its closed branch.
[D79 indexed PCM](matroska-pcm-index.md) extends this checkpoint for long PCM
presentations. Its separate subset/bounds do not lift the compressed-family horizon.

## Behavior and finite scope

Expanded Matroska codecs with more than one source sample per timestamp tick now
have a bounded reconstruction path. Each window reopens the selected callback-backed
demuxer/decoder at the beginning, keeps fresh packet skip/discard state and counts
actual decoded source samples. Variable frames and trimmed first/final frames
therefore share one resampler phase across cold, forward and backward requests.
Observed timestamps still validate continuity within the declared tick plus one
sample; they cannot silently move the reconstructed clock or conceal larger gaps.
The initial absolute origin remains limited by container precision. This does not
recover information that the original timestamp rounding discarded.

A timestamp-zero seek can land after an Opus negative-time priming packet, and
FFmpeg clears consumed initial skip state when seeking. Expanded canonical
Matroska windows whose preroll reaches the beginning reopen even under a fine
clock. This preserves the first audible samples and repeats initial trimming once
per window. Established decoded format constraints survive reopening. The selected
original identity, source snapshot and deadline/cancellation context are retained.
All FFmpeg owners close before their I/O owner; no native path or network is used.
The pinned demuxer and flush behavior were inspected in FFmpeg 9.0.2
[matroskadec.c](https://github.com/FFmpeg/FFmpeg/blob/n9.0.2/libavformat/matroskadec.c)
and [seek.c](https://github.com/FFmpeg/FFmpeg/blob/n9.0.2/libavformat/seek.c).

**Ten-second presentation horizon at D78 (retained for coarse compressed families):** a coarse reconstruction window must end at
or before 480000 output samples. The HLS owner rejects longer video presentations
before its initial decode, worker, resource registration or fragment sealing,
and clears the owned store on failure. The boundary refusal performs no window
I/O and invalidates that decoder, as other window failures do. Existing per-operation
256-MiB requested-byte/1,048,576-callback, deadline and 20-second decoded-input
bounds include reopening/probing; they are not reset between those stages.
Ten seconds leaves room for a complete bounded codec frame within the existing
input budget. Lower I/O/deadline limits can still refuse a particular file.

This is a short-presentation correctness foundation, **not long-movie seeking**.
It converts each requested window lazily, retains no whole-movie PCM/index and
does not increase existing budgets. Rebuilding from the beginning is deliberately
not extended to arbitrary movie lengths. Next, build bounded exact packet/sample
anchors or an equivalent codec-aware index for long-file cold seeks, with scaling
and phase tests, before selected remux/automatic policy. Do not simply raise the
ten-second limit or reinterpret timestamp tolerance as exact seek evidence.
Legacy AAC/Dolby/DTS fixed-grid behavior and noncanonical standalone decoding keep
their existing policies. Sample-clock long presentations remain accepted.

## Owned fixtures and checks

[The generator](../tools/audio-codec-spike/generate_coarse_clock_fixtures.py)
retimes thirteen original D77 fixtures to nearest millisecond timestamps, keeping
compressed/PCM payloads, CodecDelay and signed DiscardPadding unchanged and
rebuilding cue offsets independently. It also synthesizes PCM at 44.1 kHz with
997/1537/2111/641-sample packet lengths and a short tail, fine/coarse controls,
a twelve-second video control and a 100-ms discontinuity. No new dependency,
implementation source or external media is adopted.
[Provenance](../tools/audio-codec-spike/fixtures/coarse-clock/provenance.json)
records all eighteen hashes, original hashes or original tone input and rounding.

`coarse_matroska_clock` tests fourteen positive cases: FLAC, Opus, Vorbis, TrueHD,
signed/float PCM, all four compressed 5.1 forms, negative leading padding and
the 44.1-kHz variable-packet case. Cold late/tail windows, reused forward/backward
windows and adjacent cuts at non-frame boundaries compare PCM against fine-clock
continuous decoding at MSE <1e-10 without a shift search. Literal audible end is
288144 samples, with a 144-sample EOF tail. The Opus beginning must contain signal.

Both HLS segments are generated cold-last then first. Their audio init and fragment
bytes must equal the fine-clock controls, and the independent MOV/AAC oracle
checks the literal 312000-sample video end. Sealed-cache reuse and one-time cleanup
remain required. Longer coarse video must fail with the exact horizon reason,
while the same fine-clock video prepares. Discontinuities, mid-reopen cancellation,
literal horizon+one and failed-object reuse have separate refusal checks.
Local resource exhaustion is not receiver natural EOF.

Build and run the README static/shared checks and the optional standalone codec
suite. CI explicitly builds `sap2_coarse_clock_tests` and includes its CTest.
Recorded local validation: codec 7/7; baseline static/shared 36/36 and 37/37;
enabled shared 38/38; standalone and integrated x64 UWP compilation and codec
AppContainer/import inspection. Actual PR-head CI is a separate merge gate.
No expanded packaged runtime or converted-audio receiver check ran. Screenbox
remains on `0.3.0-ci.207`; speakers and discrete converted surround remain deferred.
