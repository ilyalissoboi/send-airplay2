# Private stereo AAC HLS composition (D71)

The user's 2026-10-13 Proceed authorizes this increment after decoder PR #50
merged as `65bb8a7`. It implements an optional private native builder, not a
new public delivery mode, Screenbox feature or published package. Audio-only
speaker casting and discrete 5.1/7.1 output remain deferred for unavailable hardware.

## Behavior and policy

`StereoAacHls::open` borrows one absolute startup context and selects the original
enabled source identity before checking its codec, using D69/D70's rules. It
calls the host size callback once, snapshots that size, opens the selected
decoder and indexes video independently of audio codec support. Multiple MP4
audio tracks require an explicit ID; no fallback to a supported/commentary track.

The output entry point is `main.m3u8`, with copied compressed video in
`index.m3u8`/`init.mp4`/`s<N>.m4s`, original WebVTT associations, and a separate
`audio.m3u8`/`audio-init.mp4`/`a<N>.m4s` stereo AAC-LC rendition. CODECS includes
`mp4a.40.2`, and EXT-X-MEDIA declares LANGUAGE, CHANNELS="2" and the audio group.
The shipping remux keeps its default behavior and does not link FFmpeg.

The original fixed downmix maps FL/FR directly, center and corresponding
back/side surround at sqrt(0.5), and LFE to both outputs at 0.5. All weights
share a divisor equal to the larger absolute row sum (at least one), so fully
correlated normalized inputs have headroom. A sole center/mono channel maps
equally to both outputs at unity. There is no loudness normalization, automatic
DRC or clipping. Nonfinite PCM, unsupported speaker masks or a mixed value beyond
normalized headroom fail. This matrix is an engineering choice, not a receiver
speaker-mapping observation or a claim of surround output.

Output is 48 kHz, stereo, AAC-LC, nominal 192 kbit/s ABR. Each segment uses its
absolute video boundary rounded once to the 48 kHz clock, then snapped down to
the AAC 1024-sample grid; the final end remains exact to that clock. Independent
EXTINF rounding is never accumulated to locate audio boundaries. An
audio segment can round up after grid snapping; its finalized durations raise
the common target of audio, video and subtitle playlists together. A bounded
encoder epoch uses 48 AAC frames of preroll and lookahead (1.024 seconds each),
retaining only canonical packets. Late and backward requests do not convert the
whole movie first. Startup decodes/encodes only one tiny window to establish the
format/configuration, and seals no fragments.

Stored audio DTS is audible packet PTS + 1024. The first fragment includes the
encoder priming packet; the init edit removes priming once. The last sample's
duration and finite edit trim the movie end. AAC still decodes a complete final
frame: a consumer must honor presentation duration rather than play codec padding.
Selected audio's initial empty edit is silence, and selected audio ending before
video is deliberately padded with silence; content beyond video's end is omitted.

The HLS builder opts into a fixed-frame Matroska clock: seed at presentation zero,
require constant decoded frame sizes greater than twice the timestamp quantum,
and snap rounded frame timestamps only within one tick of the common sample grid.
Off-grid timing, ambiguous clocks and cadence changes fail. This fixes relative
seek phase across epochs; the initial absolute anchor remains limited by the
container clock. D70's default clock policy remains unchanged. DTS-core Matroska
joins have evidence; other fixed-cadence Matroska codecs need additional fixtures.

## Storage, cancellation and bounds

The host supplies per-cast `seal`, `read_at` and `clear` callbacks. No file paths,
native filesystem access, networking or storage implementation is selected here.
Sealing must atomically publish the entire immutable fragment or fail. Read
callbacks support short reads and cooperate with the combined cast/request context.
`clear` must be nonthrowing and delete only this cast's objects.

One worker serializes decoder/encoder use. Concurrent requests for one fragment
coalesce into one job; a cancelled/expired waiter does not cancel shared work.
There are at most four pending jobs plus the current job, one 30-second absolute
generation deadline per job, and the existing decoder's bounded source reads.
Epoch PCM is at most ten seconds with up to eight input channels; encoded working
data is at most 1 MiB, with AAC packets at most 8192 bytes. No full-movie PCM cache.

A mandatory quota admits the movie using twice the nominal AAC byte rate plus
segment overhead, and checks actual sealed fragment bytes before every seal.
There is no eviction or regeneration after success. Generation/seal failure poisons
unsealed entries; no tentative size is published and future jobs cannot continue
with an invalid decoder. Admission is an estimate, not a guarantee that actual
encoding will fit the quota. Generation/seal failures stop future unsealed jobs;
a storage-read failure fails its request. The future playback controller must
propagate these failures as terminal and tear down the cast.

`stop` is idempotent: cancel/join the worker, wait for active cache callbacks,
release the decoder, clear storage once, release callback objects and resources.
Retained audio-resource copies become inert. The host must stop MediaServer first
and release copied video resources before releasing original source storage;
those retain source callbacks according to the existing remux contract. Callbacks
must cooperate and must not reenter stop. A blocked noncooperative callback cannot
be forcibly interrupted by C++.

Accepted video currently has zero decode origin, no trim/empty/complex video edits,
and either no composition delay or a single unit-rate reorder edit cancelling the
initial composition delay and covering the complete decode span. Each audio
segment must be nonempty and at most seven seconds; longer GOP spans fail rather
than violate the ten-second epoch budget. Movie duration is at most seven days.
Protected media, format changes, discontinuities, unsupported layouts/profiles
and D70's other source limits remain refusals. DTS-HD/dependent E-AC-3 are unproven.

Master bandwidth values include estimated AAC allowances, and indexed MKV video
already uses cluster-span estimates. They are not measured VOD averages/peaks.
Measured bandwidth/authoring validation, broader timelines/long-GOP policy and
long-movie performance remain explicit gates before public integration.

## Offline validation and its limits

Ten original 16.5-second movies include actual compressed AAC, AC-3, E-AC-3 and
DTS-core MP4, DTS Matroska, B-frame video, text subtitles, short audio and edited
audio. A variable-frame-duration video's 6.490-second span
becomes 6.506667 seconds of AAC, requiring a shared target duration of seven.
The generator, tool configuration, arguments, input tones and SHA-256s are
in `tools/audio-codec-spike/fixtures/hls/provenance.json`. No downloaded media or
development FFmpeg executable is redistributed; the optional backend retains
the pinned LGPL profile recorded in [dependencies](dependencies.md).

The tests verify literal 5.1/7.1 matrix weights/headroom and encoded isolated
speaker routing through the private encoder; one cold late generation
followed by backward reads; concurrent coalescing/immutable bytes; copied video
packet bytes with no muxed audio; preserved subtitle association/cues; and an
independent MOV demux/AAC decode of all three fragments. The global presentation
contains exactly 792000 stereo samples (16.5 s), without timestamp gaps/overlaps.
Both joins are compared with continuous source windows at fixed offsets, without
correlation shifts, with RMS below 0.004. Earlier Matroska epoch phase failed
at RMS 0.049276; the canonical clock correction resolves that fixture.

The oracle uses demuxed global PTS/durations and manual skip handling. In a newly
opened standalone late-fragment MOV, FFmpeg otherwise reapplies initial stream
padding to the first fetched packet. Cold decode uses the same sealed bytes and
is compared with continuous decode after one 1024-sample synthesis-overlap frame,
with RMS below 0.001; the first frame can differ. This is not a proof of
artifact-free receiver seeking. Final
AAC codec output contains padding; tests trim only according to independently
demuxed packet/frame presentation duration, not a guessed test-file length.
The composed master and audio playlist omit INDEPENDENT-SEGMENTS; only the
copied-video playlist keeps it. A master tag would extend the independence claim
to audio under [RFC 8216 section 4.3.5.1](https://www.rfc-editor.org/rfc/rfc8216.html#section-4.3.5.1),
which the cold synthesis result does not establish.

Quota admission, startup cleanup after size/probe/selection/cancellation failure,
cooperative cancel during decoder reads, waiter cancellation
and absolute expiry, Stop while generating/waiting, retained-resource refusal,
atomic seal failure and failure poisoning are checked. Desktop codec, decoder
and HLS suites pass 3/3. Baseline static/shared CTest pass 35/35 and 36/36. The
x64 UWP private library compiles, and existing codec DLL app CRT/import inspection
is checked separately. No new packaged builder runtime or receiver run is added.

Reproduce with the standalone codec manifest/triplets from
[the spike](stereo-aac-codec-spike.md), build target `sap2_stereo_aac_hls_tests`,
then `ctest --test-dir <codec-build> -C Release -R stereo_aac_hls --output-on-failure`.
CI adds this roughly six-second test to the existing desktop codec job without
changing the dependency cache/profile or rebuilding FFmpeg for a new dependency.

Next: complete authoring/performance edge gates, integrate an explicit stereo
delivery option through playback/C ABI/bindings/CLI, implement authorized host
cache callbacks, and test a new packaged runtime. Then manually observe Apple TV
and Mac video/audio, lip sync, cold/forward/backward seeks, pause/resume, Stop,
natural end and queue continuation. No package publication or merge is authorized
by this implementation checkpoint alone.

PR #51 review follow-up: finalized snapped audio durations determine the common
target, which is also raised in video/subtitle playlists. A startup RAII owner
arms host cache cleanup before context validation, allocation, size, probe and
selection. Regression checks cover the rounding boundary, consistent subtitle
targets and once-only cleanup on those early failures. Existing fixture hashes
are preserved; only the original rounding fixture is added.

Merge checkpoint: PR #51 merged as `e09c57f` at final head `475ae61`, with all
16 checks passed in run `38330651001` and both review threads resolved. The final
independence declaration correction was pushed as `9994955` after that merge and
is carried separately on fresh branch `codex/aac-hls-independence` from main.
Inspect its own PR-head checks before merging; it adds no receiver observation.
