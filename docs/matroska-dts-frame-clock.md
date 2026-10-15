# DTS lace clocks in converted HLS (D86)

The user's Proceed on 2026-10-15 (Asia/Tokyo) authorizes this slice from merged
[PR #66](https://github.com/ilyalissoboi/send-airplay2/pull/66), `1fe9f61`.
All 16 final-head checks passed at `cbe7440` in run `38669659266`.
The branch is `codex/matroska-dts-frame-clock`.

## Cause and correction

D85's bounded diagnostic accepted the private video's source-zero seed and
initial offset window, then refused at the canonical DTS frame-grid guard.
Read-only header/timing inspection identified laced Matroska audio blocks.
The same refusal is reproduced using existing owned DTS-core packets grouped
into eight-frame laces, with a 1-ms container clock and a 10,666,667-ns declared
frame duration. No private media or packet data is incorporated into the controls.

Each 512-sample frame at 48 kHz spans 10 2/3 ms. The pinned demuxer first rounds
an eight-frame block duration to integer ticks, then divides that duration into
integer per-lace timestamps. Combining the rounded block anchor and those
derived offsets can exceed the original one-tick grid tolerance. The owned
fixed-lace test fails with the original guard before the correction.

[RFC 9559 section 10.3.5](https://www.rfc-editor.org/rfc/rfc9559.html#section-10.3.5)
assigns the block timestamp to its first lace frame and requires following
frames to be contiguous. The correction therefore counts actual decoded samples
within one known native block position, instead of trusting twice-rounded
continuation PTS. Every new block still passes the unchanged frame-grid and
continuity checks. The pinned demuxer's same-block packet-position behavior is
verified by owned fixtures; unknown positions retain the strict generic path.
No rounding tolerance, track selection or conversion route is changed.

This original adapter code incorporates no FFmpeg/RFC implementation source.
The existing adopted decoder/profile/dependencies and Apache-2.0 license remain.
FFmpeg's [Matroska demuxer source](https://github.com/FFmpeg/FFmpeg/blob/master/libavformat/matroskadec.c)
corroborates the duration subdivision mechanism; behavior of the actual selected
runtime is established by the owned controls, not inferred from that moving branch.

The change applies only to canonical Matroska DTS decoding. Default decoder
timing, other codecs, native selected-copy/index lacing refusals and public API 5
remain unchanged. The per-operation block clock is limited to 256 decoded frames,
matching the one-octet lace count. DTS-core profile/layout/cadence, discontinuity,
trim, read/callback/deadline, decoder-window and cache bounds remain enforced.

## Controls and pending gates

[The generator](../tools/audio-codec-spike/generate_dts_lacing_fixtures.py) reuses
existing Apache-2.0 owned compressed audio/video and independently authors fixed,
Xiph and EBML laces, an exact 256-frame lace control, and 5-ms / 32-ms inter-block
gaps. [Provenance and hashes](../tools/audio-codec-spike/fixtures/dts-lacing/provenance.json)
record the owned packet source and literal sample/duration expectations. The
32-ms gap is three complete DTS frames, testing continuity beyond grid alignment.

Native controls compare cold-tail/backward/forward windows against unlaced PCM
at fixed sample positions, without correlation shifts (MSE < 1e-10). They require
unchanged reported absolute precision, literal 792064-sample DTS EOF including
64 encoder-padding samples, byte-identical AAC fragments against D85's unlaced
packet control, exact 792000-sample video-span trim, lazy cache generation and
one-time cleanup. Both inter-block gaps remain unsupported with complete cleanup.

## Validation checkpoint

The owned controls pass and a bounded read-only diagnostic now accepts private
video preparation. Local Windows Release suites pass: static **39/39** (71.61 s),
shared **40/40** (90.28 s), enabled shared **41/41** (266.23 s), and standalone
codec **13/13** (133.01 s). These concurrent suite timings are not media-performance
calibration. The enabled private hook retains 16 legacy plus 140 automatic
scenarios, including the three bounded measurement modes.

Private package **0.1.0.7**, core/package source `db35a6f`, passes bundled
**137 automatic cases across 61 fixtures**, retained legacy/public lifetimes and
both gap-refusal controls. The actual AppContainer token and all six loaded
module hashes match registration/native build. Six-DLL UWP import/profile checks,
absence of private validation exports in the default shared build, formatting,
whitespace and 145 owned fixture hashes pass.

The folder-granted repeat is waiting for the manual Windows folder picker;
brokered first/middle/last measurements follow it. These gates remain pending.
Private input identity/content must remain outside committed evidence. No receiver,
publication or Screenbox pin change; audio-only speakers and discrete converted
surround remain hardware-deferred. Inspect actual PR-head CI before merging.
