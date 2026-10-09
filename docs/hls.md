# HLS delivery (D60)

Status: **phases 1-3b passed on the recorded receiver; 4 and subtitles not
started.** Phases 1 (PR #26), 2 (PR #27) and 3a (PR #28) are merged; 3b (the
C/C# delivery option) is on `claude/hls-api` from `5df2b80`. Records:
[phase 1](#phase-1-result-2026-10-09), [phase 2](#phase-2-result-2026-10-09),
[phase 3a](#phase-3a-result-mkv-input-2026-10-09) and
[receiver-validation.md](receiver-validation.md#hls-phase-1-d60-2026-10-09).

## User request and scope

- 2026-10-09: "add HLS support to the base library", after setting the
  Screenbox integration aside for now.
- Scope chosen by the user: **serve plus built-in remux** — the library serves
  HTTP Live Streaming (HLS) presentations to the receiver, and it can build
  them itself by remuxing local files whose codecs the Apple TV decodes
  natively (MKV in particular) into fragmented MP4 without transcoding.
- Earlier questions that led here: casting MKV whose streams the Apple TV can
  decode, and casting what VLC produces (a live, unsized stream). The first
  needs remuxing; the second needs serving a growing presentation.

Not in scope: transcoding, encryption/FairPlay, TLS, adaptive bitrate ladders,
subtitles, and I-frame playlists (trick play), unless a later decision adds them.

## Why HLS

Today the library hands the receiver one progressive MP4 URL (`mediaType`
`file`, D29) that tvOS plays with byte-range requests. That needs a container
tvOS opens directly (MP4/MOV) and a known size. HLS lifts both limits:

- **Other containers.** tvOS does not open MKV, but the same H.264/HEVC and
  AAC/AC-3/E-AC-3 streams play from fMP4 HLS segments. A remux rewrites only
  the container; samples are copied byte for byte.
- **Unsized and growing media** (later phase): an `EVENT` playlist can grow
  while the receiver plays, which suits a transcoder's live output.

## Apple requirements that shape the design

From Apple's *HLS Authoring Specification for Apple Devices* (developer.apple.com,
read 2026-10-09). Numbers are the specification's items.

| Item | Requirement | Consequence |
|---|---|---|
| 1.1 | Video MUST be H.264, HEVC, Dolby Vision or AV1 | Remux only these (first: H.264, HEVC) |
| 1.2, 1.5 | H.264 in fMP4 or TS; HEVC MUST be fMP4 | Output fMP4 only |
| 1.10 | SHOULD use `avc1`/`hvc1` (parameter sets in the sample description) | Write `avc1`/`hvc1` entries |
| 2.2, 2.5 | AAC, HE-AAC, ALAC, FLAC, AC-3, E-AC-3 among supported audio | First: AAC, AC-3, E-AC-3 |
| 2.25 | ALAC and FLAC MUST be in fMP4 | Possible later |
| 7.3 | fMP4 `tfdt` MUST continue from the previous segment | Decode times are absolute |
| 7.4 | Video segments MUST start with an IDR frame | Cut only at sync samples |
| 7.5-7.7 | Target 6 s; segments MUST NOT exceed target + 0.5 s | Target from the longest segment |
| 8.1 | EXTINF sums within one frame of the real duration | Durations from sample timing |
| 8.6, 8.8 | `EXT-X-PLAYLIST-TYPE` `VOD` for static content, `EVENT` for growing | Both types |
| 8.18 | No HTTP redirects | Serve every resource directly |
| 8.20 | fMP4 MUST have `EXT-X-MAP` | Init segment resource |
| 9.5, 9.6 | SHOULD separate audio/video; multichannel MUST be a separate audio rendition | Muxed first, separate renditions if the receiver needs them |
| 10.1 | Playlists MUST use gzip content encoding | Deviation, see below |
| 10.4 | SHOULD use the recommended MIME types | `application/vnd.apple.mpegurl`, `video/mp4` |
| 11.4, 12.4 | Unencrypted segment URLs SHOULD NOT reveal titles; SHOULD NOT be static | Random bearer path per server; generic names |

The specification is written for App Store delivery over the internet. Two
items do not fit a sender on the local network, and they are recorded as
deviations to verify on the receiver rather than as requirements met:
gzip playlists (10.1; local playlists are small) and TLS (11, 12; the existing
private bearer path and receiver-only source check apply, as for progressive
playback).

## Architecture

### 1. Media server resource sets (phase 1)

`MediaServer` serves one representation at a random 128-bit bearer path. HLS
needs several: the playlist, the init segment and every media segment. A
**resource set** is a fixed table of named resources served below the same
private base path:

    http://<local address>:<port>/media/<128-bit hex>/<name>

- Names are single path segments of letters, digits, `.`, `_` and `-`, at
  most 64 characters, starting with a letter or digit; unique in the set. No
  directory serving: only the table's names exist, everything else is 404.
- Each resource has its own content type and `MediaSource`; every `size()` is
  called once at start, as today. A table of about 1,800 segments (three hours
  at 6 s) costs callback objects, not media bytes.
- Range, HEAD, receiver-only source check, connection bound, per-request
  deadline and diagnostics are unchanged and apply per request. The
  single-source `start` keeps its URL and behavior.

A fixed table fits video on demand (VOD), including the built-in remux,
because every segment's size is known before serving (below). Growing
presentations need resources added while serving; that is phase 4.

### 2. HLS presentations

A presentation is a resource set with:

- `index.m3u8`, a media playlist: `EXT-X-VERSION:7`, `EXT-X-TARGETDURATION`,
  `EXT-X-PLAYLIST-TYPE:VOD`, `EXT-X-INDEPENDENT-SEGMENTS`, `EXT-X-MAP` for the
  init segment, one `EXTINF` per segment, `EXT-X-ENDLIST`.
- `init.mp4`, the fMP4 initialization segment (`ftyp` + `moov` with `mvex`).
- `s<N>.m4s`, the media segments (`moof` + `mdat`), named only by index.

The session sends the playlist URL in the same `insertPlayQueueItem` as today
(`mediaType` `file`); phase 1 showed the receiver needs nothing else for HLS.
MRP ownership binds on the URL duration (D30); for a VOD playlist the receiver
reported the playlist's duration on both channels and ownership bound as for
progressive playback. An `EVENT` playlist may report no finite duration; that
is a phase 4 question.

**End of media.** For progressive files the receiver's final MRP position
equals the duration, which the session's exact `at_end` rule detects. At the
end of HLS it reported URL `stopped` with a final MRP position 0.08-0.10 s
short of the playlist duration, so the session called it a receiver Stop.
Engineering choice: once the URL channel reports stopped/idle, an MRP
paused/stopped position within 0.5 s of the duration (`near_end`, the same
bound as the ownership duration match) ends the session as `media_end`. A pause
alone, however close to the end, still never ends the session. Consequence: a
receiver Stop in the last half second is reported as `media_end`.

### 3. Built-in remux (phases 2-3)

Original code, no new dependency. Inputs are read through the existing random
access `MediaSource` (the C interface's `read_at`), so packaged hosts can pass
brokered files the same way they do for progressive casting.

1. **Demux** builds a sample table per selected track: decode time,
   composition offset, duration, size, file offset and sync flag, plus the
   codec configuration (`avcC`, `hvcC`, AAC `AudioSpecificConfig`, `dac3`,
   `dec3`). MP4/MOV input reads `moov`'s sample tables; MKV input reads
   `Tracks` and every `Cluster`'s block headers (EBML, RFC 9559) without
   reading frame payloads (phase 3a).
2. **Segmenting** cuts before a video sync sample, so each segment starts
   with an IDR frame (7.4). Segment k starts at the first sync sample whose
   decode time is at least k x 6 s: cuts follow that grid, as ffmpeg's
   segmenter does, so one long GOP lengthens one segment without delaying
   every later cut (cutting 6 s after each segment's start gave a 13.9 s
   segment on the test clip). The target duration is the longest segment,
   rounded (7.7, RFC 8216 4.3.3.1). Audio samples go to the segment whose
   video decode-time span contains their decode time.
3. **Sizes before bytes.** A segment's `moof` is a function of its sample
   table only, and its `mdat` holds those samples' bytes, so every segment's
   size is known from the tables. `read_at(offset)` then builds the `moof` in
   memory and copies sample bytes from the input on demand: no temporary file,
   no full-segment buffering beyond the 64 KiB response chunk.
4. **Writing** uses one `moof` per segment with a `traf` per track, `tfdt` with
   the source's absolute decode times (7.3), and `trun` with per-sample size
   and duration, plus flags and composition offsets for video (version 1 when
   an offset is negative). Video sample entries are `avc1`/`hvc1` (1.10); MKV
   HEVC with in-band parameter sets keeps them in-band.
5. **Init segment** (MP4 input): `ftyp` (`iso5`), then `moov` with the source
   `tkhd` (new track ID, zero duration), the source edit list with its last
   media edit lasting "to the end" (duration 0, as 14496-12 allows for
   fragmented files), and the source `stsd` sample entry copied byte for byte,
   so codec configuration and presentation offsets match the source exactly.
   Consequence: a source edit that trims the end of a track (the test clip's
   audio, by a few milliseconds) is not applied.

Codecs in the first remux: H.264, HEVC, AAC-LC, AC-3, E-AC-3. Other codecs are
refused with a clear "not remuxable" result rather than served for the
receiver to fail on. Dolby Vision, ALAC, FLAC and AV1 can follow later.

MP4 input comes first because the receiver already plays the same file
progressively: comparing both paths isolates remux defects from codec support.
MKV follows on the same writer.

### 4. Growing presentations (phase 4)

Resources added while serving (an `EVENT` playlist that grows, segments that
appear), host-supplied segments from a transcoder such as LibVLC, and
ownership without a finite duration. Designed after phases 1-3 report what the
receiver does with VOD.

### 5. Public interface (phase 3b)

`sap2_cast_options.delivery` selects progressive (the default) or HLS remux
over the same `sap2_media_source`; API version 3, C# `CastOptions.Delivery`.
Details: [public-api.md](public-api.md#hls-remux-delivery-api-version-3-d60).

### 6. Subtitles (phase 3c, approved 2026-10-09)

The user asked whether subtitle tracks can survive the remux and approved
the recommended plan: after 3b, text subtitles from MKV (SubRip, WebVTT, and
ASS as text only) become WebVTT subtitle renditions in a multivariant
playlist (EXT-X-MEDIA TYPE=SUBTITLES with LANGUAGE, a STREAM-INF with CODECS
and RESOLUTION), segmented on the video segment boundaries with
X-TIMESTAMP-MAP, and are checked on the receiver with the user's film.
Bitmap subtitles (PGS, VobSub) are out of scope: Apple HLS takes text
subtitles only. Open: whether the sender can select a subtitle track (MRP)
or only the receiver's own menu; host-supplied sidecar subtitle files.

## Phases and gates

| Phase | Work | Gate |
|---|---|---|
| 1 | Resource sets; development CLI casts a directory of pre-made fMP4 HLS (`cast --hls PLAYLIST`) | **Passed 2026-10-09:** receiver plays, pauses, seeks and ends a VOD playlist with MRP ownership |
| 2 | fMP4 writer and MP4 demux; CLI casts an MP4 through the remux | **Passed 2026-10-09:** receiver plays the remuxed MP4 the same as progressively |
| 3a | MKV demux | **Passed 2026-10-09:** receiver plays MKVs with H.264/HEVC + AAC/AC-3/E-AC-3 |
| 3b | C interface and C# delivery option | **Passed 2026-10-09:** a host casts an MKV through `sap2_cast_*` on the receiver |
| 3c | Text subtitles (user-approved plan) | Subtitle tracks selectable on the receiver |
| 4 | Growing presentations and host-supplied segments | Receiver plays an `EVENT` playlist while it grows |

Phase 1 fixtures are made with ffmpeg on the developer machine
(`-c copy -f hls -hls_segment_type fmp4 -hls_playlist_type vod`) and are not
committed. ffmpeg is a development tool only: it is not linked, distributed or
required to build or use the library.

## Phase 1 result (2026-10-09)

Implemented: `MediaServer::start_resource_set` and `resource_url`
([media-server.md](media-server.md#resource-sets-d60)); the development
`airplay2-cli cast --hls PLAYLIST.m3u8`, which serves exactly the files the
playlist (and any playlist it names) references from its directory
(`src/hls_directory.*`); and the `near_end` end-of-media rule above.

On the recorded Apple TV 4K (tvOS 26.6), an ffmpeg-made fMP4 VOD presentation
of the 131.6-second test clip (H.264 Main 720p30, AAC-LC stereo, 20 segments,
target duration 8 s) was cast twice through `cast --hls`:

- Run 1: playing in 0.86 s, owned through MRP; pause, play and seek to 100 s
  accepted and seen on the TV; the receiver fetched the playlist, init segment
  and each needed segment exactly once with plain GETs (no Range, HEAD or 404),
  and returned Home at the end (user: "Audio and video played normally. All
  the player actions worked as expected and were visible on the Apple TV").
  The end was classified `receiver_stop`, which led to the `near_end` rule.
- Run 2, with the rule: seek to 120 s, natural end as `media_end`, exit 0
  (user: video and audio played after the seek, Home at the end).

Not run: HEVC, AC-3/E-AC-3, separate audio renditions, TS segments, gzip
playlists, `EVENT` playlists, the C interface, the UWP host, a sleeping
receiver. Record: [artifact](validation/native-hls-directory-windows-2026-10-09.json).

## Phase 2 result (2026-10-09)

Implemented (internal C++, compiled into the CLI and tests; it joins the
library with the C interface in phase 3):

- `src/mp4_demux.*`: reads `moov` (at most 64 MiB) through the random-access
  source and builds per-sample tables from `stsz`, `stts`, `ctts`, `stss`,
  `stsc` and `stco`/`co64` without reading sample payloads. Selects the first
  enabled H.264/HEVC track and the first enabled AAC/AC-3/E-AC-3 track.
  Refusals (`RemuxException`): `unsupported` for fragmented input, other video
  or audio codecs (an unremuxable audio track is refused rather than silently
  dropped), more than one sample description, no video, or video not starting
  with a sync sample; `malformed` for inconsistent tables or samples outside
  the file; `too_large` beyond the moov, sample-count or 2 GiB segment bounds.
- `src/fmp4_writer.*`: the init segment and each segment's `moof` and `mdat`
  header, with sizes computable without writing.
- `src/hls_remux.*`: the segment plan, the VOD playlist, and resources whose
  segment `read_at` regenerates the `moof` and copies samples from the source
  on demand, forwarding the request's cancellation and deadline.
- `airplay2-cli cast --file PATH --remux`, and the development
  `airplay2-cli remux --file PATH --out DIR` that writes the presentation's
  files for offline checks.

Offline: the remux of the 131.6 s test clip (H.264 Main with B-frames, AAC-LC,
edit lists on both tracks) gave 20 segments (target 8 s). ffmpeg 9.0.2
`-c copy -f framemd5` found all 9,614 packets identical to the source in
timestamps, duration, size and MD5; the only difference is the end-trim
noted in item 5. `remux_tests` builds MP4 files from literal boxes and checks
sample tables, refusals, segment boundaries, the exact playlist text and an
exact `moof` known answer.

On the recorded Apple TV 4K (tvOS 26.6), `cast --file gas.mp4 --remux`
played in 0.86 s with MRP ownership; pause, play and seek to 100 s were
accepted and seen; the natural end was `media_end` with MRP `at_end` (reported
131.576 of 131.566 s), exit 0. The receiver fetched the playlist, init
segment and each needed segment once (21 GETs, all complete). User: video and
audio played normally, the actions were visible, Home at the end. Record:
[artifact](validation/native-hls-remux-windows-2026-10-09.json).

After the run, review and CI's AddressSanitizer found a use-after-free in the
edit-list lookup (a pointer into a destroyed temporary); the tested MSVC build
happened to read intact memory. With the fix, the remux output and the ffmpeg
packet comparison were unchanged; the receiver run was not repeated.

Not run: HEVC, AC-3/E-AC-3, long files, sources with `moov` at the end on the
receiver (covered offline by the tests), the C interface.

## Phase 3a result: MKV input (2026-10-09)

The user's original question was MKV with natively decodable streams. Phase 3
is split: 3a adds MKV input; 3b adds the C/C# delivery option.

Implemented (internal C++, compiled into the CLI and tests):

- `src/mkv_demux.*`: EBML/Matroska reader (RFC 8794, RFC 9559). It loads Info
  and Tracks (at most 16 MiB) and scans every cluster's block headers,
  without reading frame payloads (except the first AC-3/E-AC-3 frame).
  - SimpleBlock and BlockGroup (keyframe from the flag, or from a missing
    ReferenceBlock); Xiph, EBML and fixed lacing; unknown-size Segment and
    Cluster.
  - Track choice: the first enabled H.264/HEVC video track; for audio, a
    default-flagged AAC/AC-3/E-AC-3 track, else the first one; an audio track
    set without any remuxable codec refuses the file.
  - Timeline. Matroska stores presentation times only:
    - Video decode times are the sorted presentation times. Presentation is
      delayed by the reorder delay so composition offsets stay non-negative,
      and an edit list removes the delay: the same layout as MP4 B-frames,
      which played in phase 2.
    - Audio uses its sample rate as the timescale and one codec frame per
      sample. It follows a block's own timestamp only where that differs by
      more than the TimestampScale rounding plus one sample.
    - `CodecDelay` becomes an audio edit list that skips the priming samples.
  - Refusals: other codecs, ContentEncodings (compression or encryption),
    laced video, negative timestamps, a TimestampScale that does not divide one
    second, and E-AC-3 with dependent substreams (7.1) are `unsupported`;
    invalid EBML or a missing decoder configuration is `malformed`.
- `src/sample_entries.*`: sample entries built from Matroska codec data (ISO/IEC
  14496-1/-3/-12/-15, ETSI TS 102 366 Annex F):
  - `avc1`/`hvc1` with CodecPrivate as `avcC`/`hvcC`, and `pasp` for
    anamorphic video;
  - `mp4a` with an `esds` around the AudioSpecificConfig, built from the codec
    ID for legacy `A_AAC/...` tracks without one;
  - `ac-3` with `dac3` and `ec-3` with `dec3`, parsed from the first sync
    frame;
  - `tkhd` and packed languages.
- `src/box_writer.h`: the box writer shared with `fmp4_writer.cpp`.
- `remux_to_hls` (formerly `remux_mp4_to_hls`) picks MKV or MP4 by the
  file's first bytes, so `cast --file X.mkv --remux` and `remux --file X.mkv`
  work unchanged.

Offline (development tools only, not committed), on three ffmpeg-made MKVs
from the test clip (H.264 + AAC; HEVC + AC-3 5.1 at 48 kHz; H.264 + E-AC-3 at
48 kHz):

- **Payloads:** ffmpeg `framemd5` found every packet's payload identical per
  stream.
- **Timing:** a script computing presentation times from the init edit lists
  and segment `tfdt`/`trun` matched ffprobe's video times exactly, and audio
  times within 0.33 ms (the source's millisecond rounding).
- **Sample entries:** compared with ffmpeg's own MKV-to-MP4 remux:
  - `avcC`/`hvcC`, `dac3` and `dec3` are identical;
  - the `esds` differs only in ES_ID, zero bit rate fields and the descriptor
    length form.
- **Tests:** `mkv_remux_tests` builds Matroska files from literal elements and
  covers lacing, BlockGroups, an unknown-size cluster, B-frame reordering,
  CodecDelay, track choice, the refusals, and known-answer `dac3`, `dec3`,
  `esds` and `pasp` boxes.

On the recorded Apple TV 4K (tvOS 26.6), each MKV was cast through
`cast --file X.mkv --remux`:

- Playing in 0.82-0.88 s with MRP ownership; pause, play and seek to 110 s
  were accepted; the natural end was `media_end`.
- Every media request returned 200 and completed.
- User: video and audio played in all three runs, the actions were visible,
  and the TV returned Home at the end.

Record: [artifact](validation/native-hls-mkv-windows-2026-10-09.json).

A user-provided feature-film MKV was also tested. Title withheld; 6.3 GB,
1 h 54 min, H.264 High 1920x802, E-AC-3 5.1 with Atmos (JOC, one independent
substream).
- **Offline:** all 163,624 video and 213,267 audio packets were identical, and
  presentation times matched ffprobe's exactly.
- **On the receiver:** it played in 1.03 s; seeks to 3600 s and 6780 s were
  accepted and resumed; the natural end was `media_end` at the full 6824.5 s.
  All 35 requests completed.
- **User:** video and audio played after each seek, both seeks landed
  correctly, and the TV went Home at the end. Atmos rendering was not checked,
  for lack of Atmos hardware.

**Known cost (not yet addressed):** startup reads every block header through
16 KiB windows. When frames are smaller than a window, that reads about the
whole file before playback; the test runs read about 76 MB in total for the
54 MB H.264 + AAC file, including the segments served. For the 6.3 GB film
the scan read about 2.8 GB and took 4.9 s with a cold cache (2.0 s warm) on a
local disk, before any network work; slower storage or brokered UWP file
access would stretch it. A possible
remedy, not designed yet: plan segments from Cues and size each segment on
its first request, which needs lazily sized media server resources.

Not run: real-world MKVs from mkvmerge or other muxers (lacing from a muxer,
large files), 7.1 E-AC-3 (refused), the C interface (3b).

## Provenance

The remux is written from the public specifications: ISO/IEC 14496-12 (ISO base
media file format) and 14496-15 (AVC/HEVC storage), ETSI TS 102 366 (AC-3/E-AC-3
in ISO BMFF), Matroska (IETF RFC 9559, EBML RFC 8794) and HLS (RFC 8216 and
Apple's authoring specification). No third-party implementation code is copied.
