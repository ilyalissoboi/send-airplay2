# Architecture

The sender exposes host-neutral media and credential callbacks, prepares a
finite media representation, authenticates a receiver, serves that representation
over a bounded HTTP listener and supervises the receiver session. Public C and
C# callers do not need private protocol classes.

## Source map

| Area | Main implementation | Guide |
| --- | --- | --- |
| Discovery | `discovery*`, `receiver_list.cpp`, `discovery_api.cpp` | [Discovery](discovery.md) |
| Authentication | `pair_setup*`, `pair_verify*`, `control_*`, `channel_keys*` | [PIN pairing](pin-pairing.md), [verification](peer-verification.md) |
| Trusted storage | `credential_*`, `host_credentials*`, platform store adapters | [Credentials](credential-interface.md) |
| Receiver I/O | `receiver_*`, `event_channel*`, `ntp_timing*` | [Transport](receiver-transport.md) |
| Playback | `url_playback_session*`, `url_playback_tracker*`, `mrp_*` | [Session](session-design.md), [MRP](mrp-controls.md) |
| HTTP delivery | `media_server.cpp`, `http_range.cpp` | [Media server](media-server.md) |
| Native remux | `mp4_demux*`, `mkv_demux*`, `fmp4_writer*`, `hls_*`, `text_tracks*` | [HLS](hls.md) |
| Audio policy | `media_track_inventory*`, `audio_delivery_policy*`, `prepared_automatic_media*` | [Conversion](audio-transcoding.md) |
| Optional codecs | `audio_decoder_source*`, `stereo_aac_*`, `hls_authoring*` | [Conversion](audio-transcoding.md) |
| Public API | `playback_api.cpp`, `cast_controller*`, `include/send_airplay2/` | [C API](public-api.md) |
| Managed host | `bindings/csharp/`, `tools/uwp-host/` | [C#](csharp-binding.md), [UWP](uwp-host.md) |

## Delivery policies

Progressive delivery serves an immutable source with a caller-supplied MIME
type. HLS remux copies supported compressed tracks into generated fMP4 resources.
Automatic delivery uses an explicit finite profile: Apple HLS v1 for video or
Apple Audio v1 for audio-only input. It selects a permitted copy/progressive
route or bounded stereo-AAC conversion before contacting the receiver.

Profiles are library policy, not negotiated receiver capabilities. There is no
automatic retry of a rejected representation, silent track substitution or
re-pairing. Optional conversion leaves video compressed and requires permission,
an enabled backend, host-owned per-cast storage and a positive finite quota.

## Ownership and concurrency

One cast owns its prepared resources, HTTP listener and session. Host source
ownership transfers only on successful native create. Read callbacks can run
concurrently and must cooperate with cancellation. Stop drains workers before
destroy releases source/store contexts; offline preparation borrows them instead.

Receiver operations use synchronous I/O with absolute deadlines and cancellation
polling. Session readers publish state; a supervisor performs ordered teardown.
The HTTP server uses a bounded executor and source-worker pool. No host state
callback executes on a receiver event thread: callers poll or wait for changes.

## Trust and privacy

Discovery data is untrusted. PIN pairing authenticates a receiver identity;
reconnects verify its pinned key from secure storage. There is no plaintext
credential-file fallback. Record keys and counters belong to one verified
connection and cannot be reused after failure.

The media listener binds the route-selected address, admits the selected
receiver IP and uses an unpredictable bearer path. Media HTTP remains plaintext;
the path/IP restrictions are not encryption or peer authentication. Diagnostics
must avoid credentials, bearer URLs and arbitrary peer metadata.

## Extension boundaries

The public ABI uses fixed-width values, opaque handles and sized structures;
C++ implementation types stay private. C# mirrors ownership through SafeHandle.
UWP hosts provide brokered file access and secure storage without desktop APIs.

Keep protocol validation distinct from receiver interoperability. The
[compatibility summary](receiver-validation.md) records supported evidence and
[roadmap](roadmap.md) identifies unimplemented work.
