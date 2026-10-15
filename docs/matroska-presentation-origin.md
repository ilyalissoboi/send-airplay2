# Matroska origins in converted HLS (D85)

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

Implementation validation is in progress. Private package 0.1.0.6 adds these
owned fixtures to the shared automatic catalog, then repeats the brokered
private-file measurement. Keep that input's filename/path/hash/content outside
committed evidence. Runtime results, loaded hashes and performance remain
unclaimed until measured; receiver playback/A-V sync/EOF remain separate gates.
