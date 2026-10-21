# Unfinished work and research

Current video remux, stereo audio conversion, finite audio-only delivery and
Screenbox routing are implemented. Their supported tables are in
[audio-transcoding.md](audio-transcoding.md) and
[automatic-audio-only.md](automatic-audio-only.md). Completed implementation
plans are not a queue of work still to do.

The following are separate development choices, not current support claims or
automatic commitments. A contribution should define its scope and acceptance
criteria before expanding a public profile.

## Broader input and resource behavior

Evaluate sustained long-input performance, bounded startup scans and quota
behavior using owned or explicitly permitted media. Short fixtures, sparse-index
tests and sampled first/middle/last fragments do not establish whole-file
throughput. A cache eviction/restart policy would change the immutable-resource
contract and needs an explicit design rather than simply raising a limit.

Additional codec/container forms require content-based admission, identity,
timeline/trim, decoder/profile and negative tests. Gapless/sample-exact audio,
live/growing HLS, general video transcoding, DRM and multiroom are not implemented.

## Audio-only speakers

Apple TV/Mac URL/HLS audio playback does not establish speaker support. Low-level
discovery sees RAOP advertisements, but public URL receiver selection is not an
audio-packet transport. A speaker path needs capability/endpoint handling,
audio-session setup, encrypted media packets, paced sequence/timestamps, timing,
retransmission/buffering and seek/flush/EOF-drain semantics.

Pairing, record crypto, trusted storage and timing are reusable foundations; an
audio playlist or dummy video is not a replacement for that sender. Begin with
an owned PCM/channel-identification fixture and one actual speaker before
generalizing file decoding or exposing a new public mode. Implementation and
hardware acceptance remain deferred until suitable hardware and scope are available.

## DTS 5.1/7.1 preservation

Current conversion outputs stereo and loses discrete surround/object metadata.
A future surround policy would decode DTS to multichannel PCM, explicitly map
channels, encode a supported HLS rendition and preserve copied video timing.
Potential 5.1 AAC/AC-3 or 7.1 AAC routes need separate encoder/layout and receiver
proof. Apple TV decoder capability alone does not prove encoder coverage or an
attached HDMI/audio system's ability to reproduce every channel.

Seven-channel-plus-LFE mapping, especially side/back positions and AAC channel
configuration/PCE signaling, needs independent identification fixtures. The
restricted backend is not a demonstrated E-AC-3 7.1 encoder path. Ordinary lossy
AAC/AC-3 conversion does not preserve DTS-HD losslessness or DTS:X metadata.
Do not silently downmix when a future policy requests channel preservation.
Implementation remains deferred pending an appropriate surround output system.

Research references: [Apple TV specifications](https://www.apple.com/apple-tv-4k/specs/),
[Apple HLS authoring](https://developer.apple.com/streaming/hls-authoring-specification-for-apple-devices.html)
and the pinned FFmpeg source/profile in [dependencies.md](dependencies.md).
These identify candidate directions, not tested surround interoperability.

## Protocol and platform gaps

Remote Stop/Home intent remains ambiguous on some receiver closures; keep the
current conservative end reasons until a discriminating signal or an explicitly
designed API policy exists. Broader receiver/firmware, Linux/macOS sender playback,
Android, IPv6-only discovery and link-local media serving need their own tests.
Mac consent cache details remain unproven. See [compatibility](receiver-validation.md).

Chromecast video verification belongs to the separate player integration and
remains deferred without suitable hardware. It is not an AirPlay library feature.
