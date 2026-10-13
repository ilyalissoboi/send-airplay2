# Explicit stereo AAC playback integration (D74)

Snapshot: 2026-10-13 (Asia/Tokyo). Authorized by the user's Proceed after
PR #54 merged as `6923332`. This slice connects the private D70-D73 conversion
path to playback. It adds no receiver result, published package, or Screenbox change.
Audio-only speakers and discrete 5.1/7.1 output remain deferred for unavailable hardware.

## Delivery and selection

Experimental API version 4 adds `SAP2_DELIVERY_STEREO_AAC_HLS` (2), exposed as
`CastDelivery.StereoAacHls` in C#. Progressive and remux defaults are preserved.
The optional build flag `SAP2_STEREO_AAC` defaults OFF. A binary without that
backend refuses a well-formed request at create with `UNSUPPORTED`, before
credential/source/cache callbacks or ownership transfer. Enabling the build links
the pinned FFmpeg profile; such a binary requires its codec DLLs at load time.
The existing codec-free package and Screenbox pin `0.3.0-ci.207` are unchanged.

`sap2_media_probe` / `MediaTracks.Probe` borrows an immutable source synchronously,
with a finite 1..120000 ms deadline and optional cancellation predicate/token.
It never calls source release, retains no callbacks, decodes no frames, and
returns original MP4 track IDs or Matroska track numbers, including disabled,
unsupported and non-audio identities. Metadata limits remain those of D69.
Text is bounded untrusted file data, possibly truncated; the CLI prints only IDs
and flags. A successful probe is not evidence of decoder/receiver support.

Selection happens before codec filtering. An explicit nonzero `audio_track_id`
must identify enabled audio; otherwise startup fails `AUDIO_TRACK_INVALID` without
fallback. Auto (zero) selects sole enabled audio, or a unique enabled Matroska
default. Ambiguity fails `AUDIO_SELECTION_REQUIRED`. No enabled audio produces
copied video only; no silent audio track is invented. Supported video is copied,
and selected source audio is decoded/downmixed to 48 kHz stereo AAC-LC at the
existing fixed settings. D70-D73 codec/profile, timeline, GOP, seek-history and
bandwidth-estimate limitations remain; see [the builder](stereo-aac-hls.md) and
[authoring audit](stereo-aac-authoring.md), including its three known failed audits.

## Host storage and lifetime

Stereo delivery requires an explicit nonzero sealed-byte quota and a per-cast
`sap2_aac_fragment_store` callback table. Seal publishes a whole fragment atomically;
read supports immutable concurrent positional short reads. Both poll the borrowed
read control, combining generation/request cancellation, cast stop and deadline.
The native library chooses no filesystem path and performs no cache eviction.
Conversion bounds and admission estimate remain D71/D72's; exhausted quota is
a terminal resource-limit failure, not an eviction/retry policy.

Ownership of the source and cache transfers only on successful native create.
Clear runs once after workers/readers drain, including failed start and destroy
without start. Release runs once at destroy after clear and all callback owners.
Failed create invokes neither callback. Callback code must contain exceptions,
remain valid until destroy returns, cooperate with cancellation and never reenter
cast functions. C# Create additionally owns failure cleanup, matching its existing
source ownership rule; keep delegate owners on the SafeHandle through native destroy.

The desktop CLI/API host's `HostFragmentFiles` and C# `FileAacFragmentStore` use
an existing root explicitly chosen by the host, reserve a random child for this
cast, publish temporary files by rename, and delete only known files and the empty
child. They never recursively delete the root. Filesystem clear is best effort;
the native cleanup flag means callbacks drained and clear returned, not proof
that an OS denied deletion succeeded. AppContainer storage permission and brokered
source access remain a packaged runtime gate; the desktop adapters do not establish it.

## Supervision and ABI

Startup loads credentials first, then prepares under a separate 120-second
deadline, starts HTTP, and authenticates playback with the existing receiver
timeout/wake behavior. Stop cancels preparation and waits for it. A supervisor
starts before HTTP can request conversion and observes terminal conversion
failure during start and active playback. It cancels startup or stops playback,
drains HTTP, stops/joins conversion, then clears storage. A confirmed session
interrupted just before publication follows the same synchronous cleanup: failed
Start returns only after cleanup and records StartFailed. Natural receiver end
also drains stereo delivery automatically. Legacy delivery keeps its existing
listener lifetime until host Stop/destroy.

New startup results distinguish selection, source, codec, cache, resource-limit
and preparation/generation timeout failures. Status appends `media_failure`,
`media_cleaned_up`, estimate provenance flags and original selected audio ID.
`MEDIA_FAILURE` is an end reason when conversion was observed before a user Stop
or receiver/session end; a later conversion category does not overwrite an
already-observed terminal reason. These observations are concurrent snapshots,
not a total ordering across every receiver and worker event. Receiver channel
failure fields retain their existing meaning; cleanup-generated cancellation
does not replace the conversion category.

Options retain their v1/v2/v3 prefixes. A reserved v4 field keeps the audio ID
past v3 x86 tail padding. Windows sizes are x64 104 / x86 80 bytes for options, and 104
bytes for status; the old 80-byte status prefix remains accepted. Extension
fields are written only when the complete group fits. New C# bindings require
API 4; an old API 3 host can still use its old layouts with the new library.
Non-stereo requests reject nonzero conversion-only fields instead of ignoring them.

## Development use

Build with `-DSAP2_STEREO_AAC=ON -DVCPKG_MANIFEST_FEATURES=stereo-aac` and the
pinned toolchain/FFmpeg overlay described in [dependencies](dependencies.md).
Default builds do not install or link FFmpeg. The existing Release-only codec
CI jobs reuse their validated package cache to build the enabled desktop shared
core and x64 UWP core; no codec NuGet artifact is published.

```text
airplay2-cli cast --probe-tracks --file PATH
airplay2-cli cast --address IP --profile NAME --file PATH --stereo-aac \
  --audio-track ID --cache-directory EXISTING_DIRECTORY --cache-quota-bytes 1073741824
```

`airplay2-api-host` takes the same flags without `cast`; it also retains its
cancellation/cycle checks. Omit `--audio-track` only for deterministic auto
selection. Interactive status/pause/play/seek/stop uses the public C interface.
Diagnostic/minimal-remote/pre-made-HLS flags from legacy `cast` are rejected in
this mode. CLI probe performs no receiver or credential operation.

In C#, probe a fresh `MediaSource`, choose its original ID, then create a cast
with `Delivery = CastDelivery.StereoAacHls`, `AudioTrackId`, `CacheQuotaBytes` and
a per-cast `FragmentStore`. The host may implement storage callbacks for its
own authorized backing store. Never probe a source after transferring it to a cast.

## Validation gate and remaining work

Offline coverage includes old-layout canaries, literal ABI sizes, borrowed probe
ownership and cancellation inside host reads, codec-free refusal, managed delegate
lifetimes, cache clear/release once, preparation failure/cancellation, natural
end, and terminal conversion failure during receiver startup and active playback.
The enabled controller uses owned compressed DTS MP4/MKV and AAC MP4 fixtures,
serves lazy AAC over loopback HTTP, then injects a sealed-cache read failure to
check ordered teardown. This establishes implementation behavior with a scripted
receiver, not AirPlay interoperability.

Windows Release local gates pass: baseline static/shared 36/36 and 37/37, with
final changed-controller/host/binding rechecks; enabled shared 37/37; integrated
x64 UWP core compilation and six-DLL AppContainer/app-CRT/import inspection.
The selected compressed AAC-commentary/DTS MP4 passes explicit ID 42 startup
in both physical audio-track orders, and automatic selection refuses both.
Formatting/diff checks pass. Actual PR-head CI remains a merge gate.
After this PR's merge gate,
the next slice should validate a host-authorized AppContainer source/cache adapter
with the integrated DLL, record hashes/notices and bounded real-file performance.
Then validate stereo output/controls/seeks/EOF on available Apple TV/Mac receivers
before an approved package and Screenbox integration. Other audio speakers and
surround-preserving output stay deferred. Visual changes require written manual
user confirmation, without Computer Use.
