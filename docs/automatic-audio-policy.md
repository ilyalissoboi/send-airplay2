# Private selected audio copy/conversion policy (D82)

The user's Proceed on 2026-10-14 authorizes this slice from merged
[PR #62](https://github.com/ilyalissoboi/send-airplay2/pull/62), `c69543a`.
All 16 checks passed at `cc3bb2c` in
[run 38560520412](https://github.com/ilyalissoboi/send-airplay2/actions/runs/38560520412).
The active branch is `codex/automatic-audio-policy`; inspect its actual PR head.
This implements the private policy/selected-remux step of
[the user-approved plan](automatic-audio-transcoding-plan.md).
It does not expose automatic delivery in API 4, its bindings or CLI.

## Resolution and ownership

`plan_audio_delivery` resolves the original enabled audio identity in the
unfiltered inventory. Explicit absent/disabled/non-audio IDs are invalid; ambiguous
MP4 or Matroska selection requires a choice. A unique enabled Matroska default
can win among multiple enabled tracks. No enabled audio yields video-only.
Action/identity fields are meaningful only after selection succeeds; the metadata
plan is not decoder or receiver proof.

The private `PreparedAutomaticMedia` owner finalizes the plan before resources are
registered: copy eligible selected audio, convert selected validated decoder input
to stereo AAC, or copy video only. It reports original identity, source/output
family, output channels and a fixed reason. `apple_hls_v1` is an explicit delivery
profile, not a capability inferred from model/discovery/consent responses.

Copy/video-only never invoke or take ownership of the offered conversion store,
work without storage/quota and work in codec-free builds. Conversion requires the
enabled backend, complete per-cast store callbacks and positive quota after its
action is resolved. Store ownership transfers only upon entry into existing
stereo preparation; accepted-owner startup failure/Stop/destruction cleanup stays
unchanged. `preserve_original` permits copy and refuses a required stereo conversion.
The source size callback is sampled once and reused immutably across preparation.

Failures never select another track or retry another route. Source/configuration/
protection/resource/cache/codec/timeout/cancellation refusals propagate. A lazy
conversion seal failure remains terminal through the existing conversion owner.
The policy is entirely offline and cannot retry receiver/network/consent failures.
Copied resources retain the existing native remux read-failure behavior; no encoder
or conversion worker monitors the copy route. Stop follows server/resource drain;
retained resource copies must finish before host callback storage is released.

## Executable first copy table

This is a finite library profile, not a list of everything Apple TV can decode.
It supplies one selected copied track, without additional fallback renditions or
a universal Apple HLS compatibility claim.

| Family | Admitted configuration |
|---|---|
| AAC-LC | MPEG-4 Audio OTI `0x40`, object type 2, 44.1/48 kHz, channelConfiguration 1/2/6 (mono/stereo/5.1), 1024 samples. Exact two-byte GASpecificConfig or that form plus the literal SBR-absent sync extension `56 e5 00`. |
| AC-3 | 44.1/48 kHz, bsid 8, bsmod 0, 1536 samples; mono/stereo without LFE or 3/2 with LFE. `dac3` must agree with every complete selected sync frame. |
| E-AC-3 | Same rates/layouts, bsid 16, one independent substream ID 0, six blocks/1536 samples per complete packet. Exact five-byte `dec3`, no dependent/extra substreams, compression, mixing/informational metadata or additional BSI. |

Copy uses ISO version-zero audio sample entries. Matroska metadata agreement
uses OutputSamplingFrequency when present, preserving D77's distinction between
coded and effective output rate. AAC descriptor lengths are bounded
by their parent ES/DecoderConfig descriptors; missing/duplicate/truncated private
configuration is malformed. AAC configuration alone cannot prove arbitrary raw
compressed payload integrity, and the copy path does not decode AAC packets.
HE/xHE-AAC, PCE layouts, 960-sample frames, explicit/unvalidated extensions and other
AAC configurations are refused in this first table, without automatically enabling
unvalidated conversion variants. A generic decoder's existence is insufficient.

Copied sample clocks must contain one complete coded frame per interior packet,
with only the final packet duration allowed to be shorter. All copied Dolby
headers are checked before preparation returns. Complete unit
length, family, rate/layout, stream form and private configuration must agree.
The integer `dec3` bitrate allows one kbit/s of rounding for 44.1-kHz frame packing
and MOV/native-writer rounding; remaining fields match exactly. The restricted
E-AC-3 BSI guard refuses optional-field presence, including additional BSI that may
carry JOC. It does not parse/render Atmos or advertise its receiver compatibility.
Other valid Dolby forms remain unsupported in this slice.

## Selected native remux and conversion boundaries

MP4 selected copy indexes video/text independently, then the requested original
audio ID, validating the simple audio edit/index contract. Finite MP4 audio media
edits must cover the entire remaining sample span after the media start; shorter
edits are refused as unsupported, because the shared copy planner includes all
packets and the init writer changes the final media duration to zero. The duration
comparison is exact across the movie/audio timescales, including fractional
boundaries and an optional leading empty edit. Zero-duration media edits retain
the existing copy-to-end behavior. Matroska selects only
the requested original TrackNumber before codec filtering, retains signed block
padding and rejects selected lacing/nonzero DiscardPadding instead of dropping it.
Selected encoded/nonstandard tracks and metadata changes are refused. Earlier
unsupported audio can be skipped without replacing the chosen identity.

The selected copy path currently uses the existing full native frame tables and
shared payload/playlist helpers. It does not use the Cues fast path. Existing
metadata/four-million-frame limits remain, with a 1-TiB automatic source bound,
seven-day presentation check and 256-MiB requested-byte/1048576 actual host-callback
startup budget for selected copy. Every short-read retry is charged. One caller
deadline/cancellation context is borrowed synchronously; it is not retained by
prepared resource callbacks. Inventory retains its separate existing probe budget.
This is bounded preparation, not a real-film startup-performance guarantee.

Conversion routes reuse D77-D81's finite decoder matrix and D74's explicit stereo
owner: DTS-core; FLAC and Opus in the recorded MP4/MKV forms; Vorbis/TrueHD in MKV;
documented packed PCM mappings. Actual native/backend identity, configuration,
layout, timeline/trim and resource checks still precede successful preparation.
The metadata family plan does not expand decoder coverage. FLAC converts because
its remux is outside this profile's copy table, not because a receiver rejected it.
TrueHD conversion loses lossless/object information and provides stereo AAC-LC,
not discrete surround/Atmos. Video/text payload associations remain copied.

The existing `remux_to_hls`, forced stereo delivery, C API prefixes/enums/defaults,
binding/CLI and optional codec profile remain unchanged. There is no public API 5,
automatic CLI flag, package publication or Screenbox pin change here.

## Owned offline evidence and next gate

[Twenty owned fixtures](../tools/audio-codec-spike/fixtures/audio-policy/provenance.json)
are generated by [the native-encoder control generator](../tools/audio-codec-spike/generate_policy_fixtures.py):
the three copied families at two rates and three layouts, video-only and selected
AAC with retained subtitles. Inputs, reused video/subtitles, encoder commands,
tool version and output hashes are recorded. No downloaded samples or tool are
shipped; dependency/source provenance is in [dependencies.md](dependencies.md).

[The common default/enabled test](../tests/automatic_audio_tests.cpp) checks exact
selected video/audio payloads, subtitle association, no copy-store calls, no
storage requirement, single host size sampling and idempotent lifecycle. Existing
owned main-DTS/earlier-AAC controls check independent main-tone presence and absent
commentary after conversion; every new input family exercises generated AAC,
literal 6.5-second video end, cached fragment reuse and one-time owned cleanup.
Refusals cover ambiguity, invalid identity, preservation, protection, copied format
change, source failure, missing backend/storage, malformed parent descriptor,
unvalidated E-AC-3 BSI, finite MP4 audio trims in both elst versions,
cancellation/timeout and lazy cache-seal failure without
fallback. Unknown earlier audio does not change selected-copy payloads.

PR #63's finite-edit review fix passes full local static **37/37** (45.40 s),
shared **38/38** (51.36 s), enabled shared **39/39** (63.95 s) and standalone
codec policy **1/1** (3.85 s). Final fractional-boundary tests then pass in all
three native builds. Finite trims refuse preparation before resource return,
without conversion/store ownership; full-span and copy-to-end controls still pass.
Four review-touched C++ files pass formatting and whitespace checks. New-head CI
must be inspected separately; these checks add no receiver evidence.

Initial local Windows x64 Release gates pass: full codec **11/11** (59.25 s), static
**37/37** (45.27 s), shared **38/38** (50.23 s) and enabled shared **39/39**
(61.94 s). After adding final fixed-duration and effective-output-rate metadata
controls, focused policy checks pass in standalone codec and default/enabled
builds. Standalone/integrated x64 UWP compilation and five/six-DLL import checks
pass. Twelve touched C++ files pass clang-format dry-run/Werror; whitespace,
101 D77-D82 owned fixture hashes and 515 relative documentation targets pass.
The new automatic policy test is explicitly built/selected in codec CI; no
backend/profile/cache-key change is made. Inspect actual-head CI before merge.
These are offline results, not receiver interoperability or expanded packaged
runtime proof.

Next is versioned public preparation/decision reporting and opt-in delivery,
binding/CLI integration and compatibility/lifecycle tests. Then repeat expanded
AppContainer storage/module-hash gates, profile real-film/brokered-host performance
(including selected full-scan copy), and validate copied/converted playback with
an available observed receiver. Speakers/audio-only transport and discrete
converted surround remain hardware-deferred; Screenbox stays on `0.3.0-ci.207`.
