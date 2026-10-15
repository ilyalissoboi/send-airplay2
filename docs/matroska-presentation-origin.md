# Matroska origins in converted HLS (D85)

**Subsequent D86 result:** [DTS lace clocks](matroska-dts-frame-clock.md) correct
the frame-grid refusal recorded below with owned controls and unchanged bounds.
The brokered first fragment now converts, but its middle request refuses at the
resource limit after 256 MiB requested source bytes. Complete performance and
receiver gates remain open; this preserves D85's historical evidence.

The user's Proceed on 2026-10-15 (Asia/Tokyo) authorizes this slice from merged
[PR #65](https://github.com/ilyalissoboi/send-airplay2/pull/65), `76abc3c`.
All 16 final-head checks passed at `9290fb3` in run `38663140087`.
The branch is `codex/matroska-presentation-origin`.

## Timeline contract

D84's authorized private video refused before conversion because its first
Matroska video timestamp was nonzero. The private video-composition path now
uses that first video decode/cue timestamp as its presentation origin. Indexed
and full-scan copied video decode times subtract the exact native tick origin;
compressed packet bytes, durations, composition offsets and reorder edits stay
unchanged. Ordinary remux and selected-copy paths keep their original clocks.
MP4's existing trim/edit restrictions remain unchanged.

Audio decoder requests add the source origin rounded once to the 48-kHz clock;
returned PCM is labeled with the zero-based AAC epoch clock before encoding.
This clips audio preceding the video and preserves initial silence when audio
starts later. Indexed and fixed-cadence source clocks keep their existing
delay/trim and seek rules; each legacy decoder owner is seeded at source zero
before an offset seek, including the isolated authoring measurement owner.
Original track selection and failure routes are unchanged.

WebVTT cues subtract the same origin on their microsecond clock. Cues ending
before video are discarded; overlapping cues are clipped at zero. Private
indexed composition retains text preceding the first video cue so it can apply
that clipping. Later cross-segment look-back remains bounded as before.

The absolute source origin plus presented duration stays within seven days;
native tick boundaries are converted directly rather than accumulating EXTINF
rounding. Existing source-read/callback/deadline, packet/anchor, segment, decoder
window and cache quotas remain. This does not accept arbitrary edits, timing
discontinuities, DTS-HD, new codecs or discrete surround output. Backend/private
validation exports stay OFF by default; the public API remains version 5.

## Owned controls and pending gates

[The generator](../tools/audio-codec-spike/generate_origin_fixtures.py) extracts
existing Apache-2.0 owned compressed packets and authors four Matroska containers
with literal origins, Cues and captions. No external media, dependency or encoder
implementation is added. [Provenance and hashes](../tools/audio-codec-spike/fixtures/origin/provenance.json)
record each owned source. The controls include 83-ms video with earlier/later
DTS audio, indexed reordered video, and a shared 123-ms FLAC/video origin.

The focused native test checks literal native/48-kHz origins, rebased video
fragment headers and unchanged payloads, subtitle clipping/overlap, cold-tail
then backward/forward cache reads, exact output EOF, retained cleanup and audible
references at fixed offsets without correlation shifts. A common FLAC offset
must preserve every AAC fragment byte against its original zero-origin source.
Lossy-reference RMS is bounded by 0.006 absolute and 6% relative, with a 0.0005
absolute floor for quiet AAC tails. Existing stricter legacy join tests remain.

## Recorded validation and remaining gate

[Sanitized dated evidence](validation/matroska-presentation-origin-windows-2026-10-15.json)
records native/package source `b946bb7`, with no native changes after that commit.
Local Windows Release suites pass: static **39/39** (47.07 s), shared **40/40**
(55.40 s), enabled shared **41/41** (99.07 s), and standalone codec **12/12**
(86.65 s). The final focused origin check passes in 3.25 s, including the partial
initial DTS-cadence refusal/cleanup control. Existing legacy join, delay/trim,
large-seek and resource/refusal checks remain. The runtime catalog now covers
57 fixtures and 126 automatic scenarios (129 desktop scenarios including the
three bounded measurement modes), alongside 16 legacy forced-stereo scenarios.

Private package **0.1.0.6** passes both bundled and hash-verified folder-granted
126-case/57-fixture matrices on Windows 11 x64 10.0.26200. Each retains the 16
forced-stereo cases, nine original public lifetimes, 11 public offline checks,
33 automatic public lifetimes and explicit outside-profile AAC refusal. Expected
failures/decisions, one-time source/store release, child removal and sibling
preservation pass. The user selected the visible copy of owned fixtures. Actual
AppContainer tokens and all six loaded DLL hashes match registration/native
build for both runs and the private-video measurement. Six-DLL UWP import/profile
inspection passes; default shared exports exclude the private hooks. Seven
touched C++ files pass formatting; whitespace and 139 owned fixture hashes pass.
Inspect actual PR-head CI before merging.

The authorized brokered private video records **MEASURED_REFUSAL** again, at a
different existing guard. Preparation returns media-unsupported (20) in 29 ms
(30 ms elapsed), with 332 source callbacks, 5,746,551 requested bytes and
5,553,062 returned bytes. No media fragment is requested/sealed; accepted
conversion storage clears/releases once and its child is removed. These timings
measure refusal, not conversion throughput. Process CPU/memory snapshots include
app/runtime/picker overhead; no speed comparison with D84 is established.

A bounded read-only desktop diagnostic accepts the source-zero seed and initial
source-origin window (864 samples on the 48-kHz clock), then reports
`AudioDecodeFailure::unsupported` at
`Matroska timestamp is off the canonical frame grid` during the representative
first-period measurement. The origin requirement is passed; the fixed-cadence
audio clock needs separate investigation. This observation does not establish
whether packet timestamps, decoder frame phase or a real source discontinuity
causes the refusal. No tolerance was widened and no track/route was substituted.
The input's filename/path/hash/content remain excluded from committed evidence.

Next proposed slice: reproduce that fixed-cadence timing mismatch using owned
Matroska DTS controls, distinguish valid coarse timestamps/decoder phase from
discontinuities, and preserve refusal for actual discontinuities. After a validated
correction, repeat first/middle/last cold/warm brokered measurements. Copied and
converted receiver playback, A/V synchronization, controls and natural EOF remain
subsequent gates. No receiver was contacted, package published or Screenbox pin
changed; audio-only speakers and discrete converted surround remain hardware-deferred.
