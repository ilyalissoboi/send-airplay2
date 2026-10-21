# C API

Current source implements experimental playback API 6. The installed headers
are the authoritative reference:

| Header | Contract |
| --- | --- |
| [playback.h](../include/send_airplay2/playback.h) | Casts, media callbacks, audio preparation and track inventory |
| [pairing.h](../include/send_airplay2/pairing.h) | PIN enrollment and local profile removal |
| [credentials.h](../include/send_airplay2/credentials.h) | Host credential callbacks |
| [receivers.h](../include/send_airplay2/receivers.h) | Bounded discovery snapshots |
| [media_server.h](../include/send_airplay2/media_server.h) | C++ HTTP resource server |
| [http_range.h](../include/send_airplay2/http_range.h) | Single byte-range resolution |

## Version and initialization

Compare `sap2_playback_api_version()` with the API expected by the binding.
Extensible structures start with `struct_size`. Use
`sap2_cast_options_init_sized(&options, sizeof options)` and the corresponding
sized initializers; initialize source/status structures as their headers require.
Zero reserved fields. Older supported prefixes retain their defaults; appended
fields do not become available merely by increasing a version number.

Initialized delivery defaults to progressive. Automatic options default to the
video profile and the allow-stereo permission value; this supplies neither an
enabled backend nor a fragment store/quota. Hosts that require preservation
should set `SAP2_AUDIO_PRESERVE_ORIGINAL` explicitly.

API evolution: 1 introduced playback; 2 host stores and pairing; 3 HLS remux;
4 explicit stereo conversion and inventory; 5 automatic video audio policy;
6 explicit audio-only policy and progressive decisions. Existing releases must
be consumed with their matching binding and native assets.

## Cast lifecycle

1. Provide a numeric receiver address, saved profile and immutable
   `sap2_media_source`. Select a delivery policy and optional host store.
2. Call `sap2_cast_create`. Only `SAP2_OK` transfers the source and any supplied
   fragment-store contexts to the handle. Failed create leaves ownership with you.
3. Call `sap2_cast_start` on a thread where blocking is acceptable. It loads
   credentials, prepares media and starts authenticated receiver playback.
4. Poll `sap2_cast_get_status` or use `sap2_cast_wait_for_change`. Issue controls
   only while their state/ownership preconditions hold.
5. Stop when finished, then destroy after every concurrent call has returned.
   Destroy releases transferred contexts exactly once after callbacks drain.

`sap2_cast_stop` from another thread cancels a pending start and waits for it.
It is local teardown; the STOP control command asks the receiver to stop its
item. An app should handle teardown even if a receiver command fails.

## Delivery

| Value | Behavior |
| --- | --- |
| `SAP2_DELIVERY_PROGRESSIVE` | Default: original bytes and `content_type` (`video/mp4` when unset) |
| `SAP2_DELIVERY_HLS_REMUX` | Native video remux; ignores caller MIME |
| `SAP2_DELIVERY_STEREO_AAC_HLS` | Explicit selected audio conversion with copied video |
| `SAP2_DELIVERY_AUTOMATIC_AUDIO_HLS` | Explicit profile selects video-only, selected copy, stereo conversion or original audio-only progressive delivery |

Automatic delivery defaults to `SAP2_AUDIO_PROFILE_APPLE_HLS_V1`; choose
`SAP2_AUDIO_PROFILE_APPLE_AUDIO_V1` for audio-only input. The delivery constant's
HLS suffix also covers progressive results. Selection is content-based, not
extension-based or a fallback after receiver failure. Read the
[video policy](audio-transcoding.md) and [audio-only table](automatic-audio-only.md).

## Media and storage callbacks

`read_at` supplies positional bytes from an immutable 64-bit-sized source.
Requests are at most 64 KiB; short reads are allowed, but zero before the declared
end fails the read. Callbacks must be thread-safe, contain exceptions, retain
neither buffer nor read-control pointer, and poll `sap2_read_should_stop` during
slow work. Do not re-enter cast operations from callbacks for that cast.

Conversion requires complete `sap2_aac_fragment_store` callbacks, a positive
quota and an enabled backend. `seal` atomically publishes a complete fragment;
`read_at` serves it; `clear` removes only that cast's data after work drains.
For automatic delivery, unused stores receive no seal/read/clear callbacks.
Their transferred contexts are still released at destroy. Accepted conversion
stores clear once, including startup failure. Explicit forced conversion has
its own unconditional cleanup contract; follow the header when implementing it.

`sap2_media_check_audio` prepares a decision offline, without credentials,
listener or receiver. It borrows source/store contexts until return and never
calls their release callbacks. It clears an accepted conversion store, discards
the temporary presentation and clears stale decision output on failure. A later
cast prepares again. A successful check is not a full-file decode or receiver test.

`sap2_media_probe` inventories MP4/Matroska tracks and preserves original IDs.
It is not the standalone-format admission interface. Free its returned list with
`sap2_track_list_free`.

## Controls and status

Commands are Play, Pause, Stop and absolute Seek in seconds. A nonzero start
position is applied after receiver playback begins, so the beginning can appear
briefly. Receiver wake handling precedes URL playback when needed. Ownership
guards prevent controlling an unrelated replacement player; they do not promise
exclusive ownership against another sender with indistinguishable metadata.

Status retains phase, receiver state, first terminal reason, failure category,
cleanup flags and successful automatic audio decision. No receiver labels, URLs
or secrets cross the playback status interface. Discovery deliberately has its
own copied public metadata contract.

End reasons must be handled conservatively. A remote Stop/Home can appear as
connection loss. Pause alone never proves EOF, and near-end classification can
identify a Stop in the final half second as media end. See
[session lifecycle](session-design.md) and [MRP](mrp-controls.md).

Legacy delivery can retain its HTTP listener after session end: call Stop or
destroy. Converted delivery also supervises media failures and drains generated
resources; check `media_cleaned_up` independently from session cleanup.

## Failures and threading

Results are fixed `int32_t` categories: invalid arguments/state, absent or
unavailable credentials, authentication, receiver rejection, timeout/connection,
protocol, unsupported/malformed media, selection, codec, cache, source and limits.
Use `sap2_result_name` for fixed diagnostic text; never relay arbitrary peer text.
No failure silently selects another track, representation or pairing identity.

Media reads can execute on HTTP workers, the preparation caller or a conversion
worker. Store/PIN callbacks execute synchronously on their calling thread.
The callback lifetime of a cast extends through destroy. Never destroy while
other calls are in flight; do not call Stop from a callback that Stop must join.

See [credential callbacks](credential-interface.md), the
[C API example host](../tools/airplay2_api_host.cpp) and the
[manual receiver procedure](manual-validation.md).
