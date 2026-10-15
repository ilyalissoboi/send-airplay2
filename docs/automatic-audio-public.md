# Public automatic audio delivery (D83 / API 5)

The user's Proceed on 2026-10-15 (Asia/Tokyo) starts this slice from merged
[PR #63](https://github.com/ilyalissoboi/send-airplay2/pull/63), `6788583`.
All 16 checks passed at final head `4f2494b` in
[run 38584885677](https://github.com/ilyalissoboi/send-airplay2/actions/runs/38584885677).
The branch is `codex/automatic-audio-public`. This implements ordered step 3 of
[the approved expansion plan](automatic-audio-transcoding-plan.md), using the
existing [finite selected copy/conversion policy](automatic-audio-policy.md).
It adds no codecs, dependencies, package publication or Screenbox pin change.

## Opt-in contract

API 5 adds `SAP2_DELIVERY_AUTOMATIC_AUDIO_HLS`, the explicit
`SAP2_AUDIO_PROFILE_APPLE_HLS_V1` profile and an allow-stereo/preserve-original
choice. The default remains progressive. Legacy remux and forced stereo keep
their contracts. Original source IDs and selection-before-filtering rules remain.
Eligible selected AAC-LC/AC-3/E-AC-3 is copied; validated DTS-core/FLAC/Opus/Vorbis/
PCM/TrueHD input outside that copy table becomes 48-kHz stereo AAC. No enabled
audio yields video-only. Preservation refuses required conversion as media-unsupported.

This profile is a finite library delivery table, not negotiated receiver support.
Its configuration, timing, size and work limits remain D82's. Finite MP4 audio
edits that trim the remaining sample span, unvalidated AAC/Dolby forms, MKV lacing/
nonzero block padding in selected copy, and unsupported/protected inputs refuse
preparation. Conversion cannot be retried after a copy, source, cache, codec,
timeout, cancellation, network or receiver failure. TrueHD conversion does not
preserve lossless/object audio, Atmos or discrete surround output.

Copy/video-only work without the codec backend, store or quota. A supplied store
must have a structurally valid callback table, but copy never invokes seal/read/
clear. Conversion checks backend availability, then requires complete per-cast
storage and positive quota after selecting its route. In a codec-free build its
existing backend-unavailable result is `SAP2_ERROR_UNSUPPORTED`; query
`sap2_stereo_aac_available()` to determine build availability. Missing conversion
storage reports media-cache. Selection/source/timeline failures keep their categories.

`sap2_cast_start()` still loads trusted credentials before source/network work.
Automatic preparation then shares the existing 120-second preparation deadline,
before the receiver timeout. It publishes a successful `sap2_audio_decision` into
status before listener/receiver session startup. It records profile, original
selected ID, action, source/output families, output channels and a fixed reason.
No raw source labels, device identifiers, addresses or secrets are added. The
decision remains available after subsequent network failure, natural EOF or Stop;
it is absent until preparation succeeds and for other delivery modes.

Both automatic routes drain the session, HTTP reads and prepared resources on
Stop/failure/EOF. Only conversion owns an encoder/cache and generates terminal
media failures. Its supervisor retains existing first-cause handling; exhaustion
is never receiver EOF. Automatic copy also drains on natural session end, without
calling unused storage. Caller source/cache contexts transfer at successful
native create and release once at destroy. Accepted conversion clears storage
once after readers/workers drain, including preparation failure; pre-routing
refusal, unstarted automatic handles and copy/video-only release an offered
cache context without Clear. Forced stereo retains its unconditional cleanup.

## Offline preparation check

`sap2_media_check_audio()` takes a media source, sized `sap2_audio_options`,
optional cancellation predicate/context and a sized output decision. Its
initializer sets the named profile, auto selection, stereo permission and a
120-second deadline; the allowed deadline is 1..120000 ms.

The check prepares a temporary automatic presentation, returns the successful
decision, drains it and discards the resources before returning. It needs no
credentials, receiver address or listener. This is stronger than the metadata-only
`sap2_media_probe()` and uses the actual preparation route; it is not full-file
decode, a playback promise or a reusable prepared handle. A later cast prepares
again against an immutable source. Output fields are cleared on failure except
the caller size; too-short outputs are rejected without a write. Future bytes
beyond the known decision/options remain untouched.

Native source/cache contexts are borrowed until return, with no release callback.
Copy/video-only do not touch cache callbacks. Accepted conversion drains and
clears only its dedicated per-check store on success or preparation failure.
Keep callbacks/predicate alive and nonthrowing; controls carry cancellation into
slow source/cache callbacks. Use fresh per-check storage, and do not check a
source already transferred to a live cast.

## Binding and CLI

The development C# binding requires API 5 and exposes
`CastDelivery.AutomaticAudioHls`, `CastOptions.AudioProfile`,
`CastOptions.ConversionPermission`, `CastStatus.AudioDecision` and
`AudioDelivery.Check()`. The latter borrows the source and consumes its offered
managed store on every path: accepted conversion clears it through native cleanup,
and unused stores release backing resources without Clear. Delegate owners stay
rooted through native callbacks; native release no longer implicitly clears an
unused managed store. Managed forced-stereo/create-failure cleanup stays intact.

The development CLI forwards these modes through the same public C API:

```sh
airplay2-cli cast --check-audio --file movie.mp4 --audio-track 42 --preserve-audio
airplay2-cli cast --check-audio --file movie.mkv --audio-track 42 --cache-directory cache --cache-quota-bytes 16777216
airplay2-cli cast --automatic-audio --address 192.0.2.10 --profile living-room --file movie.mkv --audio-track 42 --cache-directory cache --cache-quota-bytes 16777216
```

Copy needs no cache arguments. Cache directory/quota must be provided together
when offered. `--preserve-audio` applies only to automatic/check mode; automatic,
forced-stereo and legacy remux modes are exclusive. Offline check rejects receiver/
pairing/discovery/control options. It prints fixed decision fields and result
categories, never paths, credentials or URLs. The numeric action values are 1
video-only, 2 copy and 3 stereo AAC; reason values are 1 no-enabled-audio, 2
profile-copy and 3 profile-requires-stereo. Family values are in the public header.

## ABI and validation boundary

All v1-v4 enum values, option/status prefixes and defaults are preserved. The
new option fields start beyond the full v4 size including x86 tail padding.
Status writes the v4 prefix when it fits, and the decision only when the entire
v5 extension fits. Literal ABI sizes are options 120 bytes on x64 / 96 on Windows
x86, status 144, decision 40 and offline options 48. Existing v4 sizes are still
104 / 80 for options and 104 for status. Older consumers remain valid; the new
binding requires a matching API 5 development library. Published `ci.207` remains
unchanged. The offline UWP harness checks the binding's version rather than a
stale API-4 literal; this does not add a runtime observation.

Owned public-boundary tests cover copied AAC/Dolby, video-only, all six conversion
families, preservation, missing backend/storage, ambiguity/invalid selection,
callback cancellation, stale output, prefix canaries and unused/accepted store
ownership. C# checks layouts, decision mapping and lifetime. Scripted controller
tests cover automatic copy Stop/EOF/network failure and actual converted HTTP
cache failure without route changes. CLI checks exercise offline copy without
credentials/backend/cache and refuse incompatible options.

Local Release suites pass: static **38/38** (45.84 s), shared **39/39** (49.35 s)
and enabled shared **40/40** (66.05 s). Final public output-canary, C# ownership
and controller preparation-cancellation checks pass in their affected builds.
x64 UWP core compilation, six-DLL import/AppContainer checks and the offline
UWP harness/binding build pass. The harness was not run or installed.
Eleven touched native C/C++ files pass formatting; whitespace, 135 owned fixture
hashes and 530 relative documentation targets pass. Inspect actual PR-head CI.
Unit/CI and x64 UWP
compilation/import success are offline evidence. Expanded AppContainer storage/
module-hash checks, real-file/brokered performance, and copied/converted receiver
playback remain the next gates. Audio-only speaker transport and discrete converted
surround remain hardware-deferred. Publication and Screenbox integration follow
separate approval; visual confirmation uses written manual instructions.

**Subsequent D84 result:** [expanded AppContainer validation](automatic-audio-uwp-runtime.md)
passes bundled and hash-verified folder-granted automatic matrices with actual
module/token and storage checks. The authorized private video refuses at the
zero-origin-video guard; conversion performance and receiver gates remain open.
