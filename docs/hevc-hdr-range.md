# D92 HEVC HDR range signaling

2026-10-16 (Asia/Tokyo). Native PR #73 and Screenbox fork PR #13 are merged as
`d9cd2c2` and `b52c58e`. The user subsequently reports that a long local
HEVC/DTS file fails to start on the MacBook Pro with stereo conversion enabled:
the handshake succeeds, the Mac shows a grey screen/inactive controls, and
Screenbox resumes the previously playing local item. This is a new real-file
failure, separate from D91's passing short H.264 controls.

## Investigation and causal comparison

Read-only preparation with the host's unchanged 512-MiB quota succeeds, as do
reads of the first two copied video segments and generated audio fragments.
Native startup preparation takes about 160 ms. A native C API cast reproduces
the grey screen and returns `start_timeout`, with no failed source reads or
conversion failure. Temporary local diagnostics show six successful HTTP
responses: master, media playlists and initialization segments. The Mac asks
for no video/audio media fragment.

An original synthetic Main-10 HEVC control reaches playing. Its short DTS tail
then has a separate unsupported-fragment refusal; that attempt is not a
passing end-to-end conversion/EOF control. It only establishes that HEVC at
the declared profile/level/tier is not universally rejected by this receiver.

The reported file has PQ transfer characteristics in its HEVC SPS, with no
Matroska Colour element. The generated master omitted `VIDEO-RANGE`, implying
SDR. An isolated diagnostic change adds only `VIDEO-RANGE=PQ` to that master.
The same receiver then fetches copied video and generated AAC fragments,
reports playing, and the user confirms normal film video/audio. The bounded
sender Stop completes with joined media cleanup and no source/conversion error.
An intervening unchanged retry followed a failed diagnostic build and is not
counted as a PQ experiment. This evidence identifies the missing HDR range
declaration as the cause of the recorded startup failure; file length and DTS
conversion speed do not explain that comparison.

Screenbox's failed-start branch restores the local player's prior playing
state. The reported local continuation is consistent with that recovery, not
evidence that local and receiver players were both successfully started.

## Implemented correction

- An original, bounded native reader extracts the base-layer HEVC SPS VUI
  transfer characteristics from the existing `hvcC` configuration. It handles
  NAL framing/emulation prevention, sub-layer profiles, scaling lists, explicit
  and predicted reference-picture sets, PCM and optional VUI prefixes. No
  video decoder, third-party implementation or dependency is added.
- Transfer 16 yields `VIDEO-RANGE=PQ`; transfer 18 yields `VIDEO-RANGE=HLG`.
  The range travels with the remux variant into automatic stereo composition.
  Explicit conflicting SPS ranges refuse; truncated metadata and bounded-count
  violations refuse as malformed. Each SPS is bounded by its 16-bit NAL size;
  at most 16 SPS, 64 short-term sets, 16 reference pictures per set and 32
  long-term references are inspected. The reader stops after the relevant VUI
  colour description and is not a complete HEVC conformance validator.
- HDR remuxes also receive a multivariant master when they have no subtitles;
  a media playlist alone cannot carry this attribute. Both indexed Matroska
  and full-scan paths retain the declaration. MP4/Matroska use the same HEVC
  configuration reader. Existing SDR master text remains unchanged.
- Video payload, audio policy, permission default, decoder coverage, cache
  quota and API version remain unchanged. This does not add video transcoding,
  tone mapping, Dolby Vision support or a new receiver capability negotiation.

The detection source in this slice is SPS VUI, not container-only colour tags
or a guess from resolution/bit depth. Container-only HDR metadata and in-band
configuration changes are not newly supported. HLG signaling has automated
coverage, without a new HLG receiver observation. No full-film playback,
perceptual HDR/display calibration or speaker result is inferred. The bounded
Apple TV observations below are separate from this implementation coverage.

## Validation and development host

The regression suite has an independently encoded original HDR10 SPS, literal
PQ/HLG/SDR expectations, optional syntax/predicted-RPS cases, count/truncation/
escape/conflict refusals, and indexed/full Matroska master propagation without
subtitles. Existing codec-free static/shared and enabled codec checks are run
separately from receiver checks; the dated evidence record preserves results.
See [the sanitized validation record](validation/hevc-hdr-range-windows-2026-10-16.json).

A private development Screenbox **1.0.0.10** layout reuses the recorded 1.0.0.9
host executable and five pinned dependencies, replacing only `send_airplay2.dll`
with the locally built UWP correction. It is an intentional diagnostic override,
not the published hash-pinned package and not a new Screenbox package pin.
Registration preserves app data. Its actual AppContainer and all six loaded
DLLs are checked before the manual cast. The user is asked to confirm startup,
audio/video, pause/resume, forward/backward seeks and Stop/paused local handoff
using written instructions. The user confirms those Mac controls worked as
expected. Six loaded module hashes are rechecked afterward and no owned cache
directory remains. The separate 1.0.0.9 recovery-only review fix is not being
retested.

The user then reports that the same Screenbox build silently fails on Apple TV
after a few seconds, also starting from zero. A native Apple TV cast of the same
input stays active until its scheduled 20-second Stop; the user confirms normal
video/audio before that Stop. No source/conversion error occurs and cleanup
completes. This native result does not resolve the separate Screenbox attempt.
A private copy of the existing packaged UWP test host, version 0.1.29.0, uses
the same fixed native DLL with AppContainer/six loaded hashes verified. It
reproduces a premature `MediaEnd` near 16 seconds, explaining the silent natural
completion classification. The cause of that early receiver end is not yet
established. At the user's request, a longer retry keeps the Windows host visible
and plays for 90 seconds, confirmed by the user. It remains playing near 91
seconds, then stops explicitly with complete cleanup (24 sealed fragments,
3,489,255 bytes, one Clear/Release, directory removed). All 99 thread samples in
that successful retry have zero suspended threads; there was no equivalent
sample for the failed attempt, so suspension is a hypothesis, not a finding.
A harmless monitor postprocessing parameter error occurs after the completed
Stop and saved samples; it is corrected locally and does not negate playback.
The user also confirms that the written 90-second Screenbox Apple TV retry
works as expected, including normal video/audio, Stop closure and local paused
handoff. Whether the original failed attempt was minimized or covered was not
reported. Keep both failed attempts distinct from these passing retries; no
lifecycle cause had been established in those attempts. The later user follow-up
below updates the observed status and likely cause.
Post-retry AppContainer/six loaded hashes pass. An initial cache snapshot still
contains one growing owned directory; after the user explicitly confirms Stop,
the final independent count is zero directories/zero bytes. This is completed
cleanup, not a cache leak.

### Later user follow-up (2026-10-17)

The user reports that further testing suggests the Apple TV issue was caused by
the incorrect HDR flag and that it has not been observed again after the HDR
correction. Record incorrect HDR signaling as the likely cause by user assessment,
with no reported recurrence since the fix. Retain the earlier Screenbox failure
and packaged UWP early `MediaEnd` as historical attempts; the captured runs did
not independently isolate that Apple TV cause. The follow-up does not specify a
build identity, run count or duration, and adds no verification of the exact D93
candidate bytes. No new coordinated cast is performed for this documentation
update. Window visibility remains an unconfirmed earlier hypothesis, not the
current explanation.

All 17 CI checks pass on implementation head `1646ae3` in
[run 38883104462](https://github.com/ilyalissoboi/send-airplay2/actions/runs/38883104462).
All 17 checks also pass at final evidence head `1e917aa` in
[run 38909207891](https://github.com/ilyalissoboi/send-airplay2/actions/runs/38909207891).
PR #74 merged on 2026-10-17 as `f1598cf`. This status update adds no new
receiver observation.

The published `SendAirPlay2.StereoAac 0.5.0-ci.282` is unchanged and still lacks
this correction. [D93 prepares the corrected ci.294 candidate](hdr-corrected-audio-candidate.md)
from merged main with passing artifact/consumer checks. Publication needs user
approval; the host pin update remains a separate slice.
Do not overwrite or republish the old candidate.

## Standards and provenance

Apple requires `VIDEO-RANGE` when variants/renditions are not all SDR:
[HLS authoring specification](https://developer.apple.com/documentation/http-live-streaming/hls-authoring-specification-for-apple-devices/).
See also [Authoring 4K and HDR HLS Streams](https://developer.apple.com/videos/play/tech-talks/501/).
The new Apache-2.0 reader is written from
[ITU-T H.265 (01/2026)](https://www.itu.int/rec/T-REC-H.265-202601-I/en),
sections 7.3.2.2.1, 7.3.3, 7.3.4, 7.3.7 and E.2.1, and the existing
ISO/IEC 14496-15 hvcC framing. No external parser source was copied.

The literal SPS fixture is derived exclusively from original `testsrc2` pixels
using the existing development-only FFmpeg 9.0.2/libx265 tool, never linked or
redistributed. Relevant command settings: `testsrc2=size=640x360:rate=24000/1001:duration=18`,
`-c:v libx265 -preset ultrafast -pix_fmt yuv420p10le`, and
`-x265-params level-idc=5.1:high-tier=1:keyint=144:min-keyint=144:scenecut=0:pools=2:log-level=error:colorprim=9:transfer=16:colormatrix=9`.
Only its independently extracted SPS/configuration bytes are committed. Owned
hand-authored syntax fixtures cover the other branches; no private media,
paths/names, raw receiver logs, addresses, credentials or bearer URLs enter the
repository.
