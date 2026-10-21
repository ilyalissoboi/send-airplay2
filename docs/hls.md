# HLS remux and media resources

The native remux turns supported MP4/MOV or Matroska tracks into finite VOD
HLS resources without re-encoding. Sources use positional callbacks, so brokered
files do not need a native filesystem path. Select `SAP2_DELIVERY_HLS_REMUX`
through the [C API](public-api.md), or `cast --remux` in the CLI.

## Input and output

Supported video is H.264 or HEVC; ordinary remux audio is AAC, AC-3 or E-AC-3
within the implementation's container/configuration bounds. Unsupported or
protected inputs fail before receiver startup. To copy or convert a specifically
selected audio track under an explicit policy, use
[automatic audio delivery](audio-transcoding.md).

MP4 input uses native sample tables. Matroska uses a full scan or a validated
Cues-based index where available. Index entries retain decode/composition time,
duration, size, file offset, sync flag and codec configuration. Metadata and
frame-count limits are implementation resource limits, not receiver capabilities.
Fragmented input MP4 and arbitrary codec/configuration changes are not generally
admitted by the finite input parsers.

The presentation contains media/master playlists, an fMP4 init segment and
numbered `moof`/`mdat` segments. Segments start at video sync samples on a
six-second target grid; a long GOP can make a segment longer. Playlists derive
target duration from actual finalized segments. Continuous `tfdt` values retain
decode timing, including composition offsets. This does not add keyframes or
re-encode video.

Resource sizes are computed from validated tables or deferred until a lazy
resource is generated. Payload reads copy only requested source ranges; ordinary
remux does not duplicate the movie into temporary files. The HTTP server exposes
only registered resource names below a fresh bearer path, never a directory.

## Subtitles

Matroska SubRip/plain text/WebVTT/ASS/SSA and MP4 tx3g/WebVTT become selectable
WebVTT renditions. Cue text/timing and supported language/default/forced/hearing
flags populate HLS subtitle metadata. ASS conversion is text-only: override
blocks and positioning are not rendered. Bitmap subtitles such as PGS/VobSub are
omitted rather than burned into video.

Subtitle segments follow video windows, retaining complete overlapping cues and
timestamp mapping. A master playlist declares the subtitle group and video codec,
resolution/frame rate and bandwidth attributes. Receiver menus choose tracks;
sender-side subtitle selection and arbitrary sidecars are separate work.

## HEVC HDR signaling

The bounded HEVC SPS VUI reader inspects transfer characteristics from `hvcC`:
transfer 16 produces `VIDEO-RANGE=PQ`, transfer 18 `HLG`. HDR receives a master
playlist even without subtitles. The declaration propagates through indexed
and full-scan remux and copied-video/stereo-audio composition.

The reader validates relevant syntax/counts, rejects truncated/conflicting SPS
metadata and does not decode video. Detection comes from SPS VUI, not resolution,
bit depth or container-only colour tags. This adds no tone mapping, Dolby Vision
support or in-band configuration-change handling. HLG has automated signaling
coverage; it must not be inferred to have receiver coverage from a PQ pass.

## Delivery boundaries

The implementation serves plaintext LAN HTTP and uncompressed playlists. It is
not a claim of meeting every Apple internet/App Store HLS requirement. Growing
EVENT/live playlists, arbitrary external HLS references and adaptive multi-bitrate
generation are outside the finite built-in VOD path. The CLI also has a bounded
pre-authored HLS-directory mode; it does not make the native remux a general HLS
proxy.

For receiver end-state behavior see [session-design.md](session-design.md).
For immutable resources and range requests see [media-server.md](media-server.md).

Reference specifications: [RFC 8216](https://www.rfc-editor.org/rfc/rfc8216),
[Apple HLS authoring](https://developer.apple.com/streaming/hls-authoring-specification-for-apple-devices.html),
[Matroska RFC 9559](https://www.rfc-editor.org/rfc/rfc9559),
[ITU-T H.265](https://www.itu.int/rec/T-REC-H.265).
