# HDR-corrected opt-in package candidate (D93)

2026-10-17 (Asia/Tokyo). The user authorizes candidate preparation after merged
PR #75; publication and the Screenbox pin update remain separate steps.
The existing published `SendAirPlay2.StereoAac 0.5.0-ci.282` is unchanged.

## Selected source and changes

Clean merged main: `72e73ea914a7859f5cb900b5fb129d8e92d2d15b`.
[Main run 38993860187](https://github.com/ilyalissoboi/send-airplay2/actions/runs/38993860187)
is a `workflow_dispatch` on that commit, run number 294. Its separate
`nuget-stereo-aac-x64` artifact supplies `SendAirPlay2.StereoAac 0.5.0-ci.294`.
The same-run codec-free package is a consumer-conflict comparison only.

Compared with ci.282, the native correction reads bounded HEVC SPS VUI transfer
characteristics and declares `VIDEO-RANGE=PQ` or `HLG` in an HLS master, including
HDR videos without subtitles and automatic stereo AAC composition. Video bytes,
audio policy/permission, cache limits, API version, decoder coverage and
dependencies remain unchanged. Read [the D92 investigation](hevc-hdr-range.md).

## Artifact and validation

Candidate: `SendAirPlay2.StereoAac.0.5.0-ci.294.nupkg`, 21,314,548 bytes.
SHA-256: `a06216e954e8ef1e2452abe8472e95fbbc04c036e5e1c017e4a92642b20c76b8`.
All 17 main checks pass, including the independent CI package-consumer job.
The UWP audio job passes all 12 packaging controls. Local artifact and fresh
consumer checks also pass; see [the sanitized evidence](validation/hdr-corrected-audio-candidate-windows-2026-10-17.json).

The core DLL differs from ci.282, while the other five dependency DLLs and the
profile/notice hashes match it exactly. This preserves the existing dependency
and redistribution profile while delivering the HDR correction.

The downloaded package stays byte-for-byte unchanged; its adjacent SHA-256
sidecar identifies those exact bytes. Clean source attribution and all runtime
hashes are checked independently of the main CI result. The complete LGPL FFmpeg source,
overlay/patches, configuration, notices and rebuild inputs remain part of the
package. Core and binding remain Apache-2.0; no dependency or license change is
introduced by this preparation.

A fresh local consumer restores this exact version, compiles API-5 automatic
options, and verifies six copied native hashes. Unsupported architecture and
mixed direct/transitive package profiles refuse as expected. No consumer executable
or packaged app is run by these checks.

## Evidence and publication boundary

[D92](hevc-hdr-range.md) records Mac controls and bounded Apple TV retries on
private development builds, with earlier failures retained separately. These
candidate bytes add no coordinated receiver or Screenbox queue observation.
On 2026-10-17 the user reports later testing suggests incorrect HDR signaling
caused the Apple TV symptom, with no recurrence since the correction. That
follow-up provides a likely cause by user assessment, without independently
isolating the earlier attempts or identifying the exact candidate build tested.
Window visibility was not established as a cause. Whole-film
playback, HLG receiver behavior, perceptual HDR calibration, speakers and discrete
converted surround remain outside the recorded validation.

Publication remains pending explicit approval. The proposed new tag is
`nuget-stereo-aac-v0.5.0-ci.294`, targeting the selected main commit, with the
unchanged package and its sidecar as assets. [Prepared release notes](releases/sendairplay2-stereo-aac-0.5.0-ci.294.md)
describe the fix and the evidence limits. Do not overwrite ci.282. After approved publication,
verify the public download independently before updating Screenbox's opt-in pin
in a separate slice. Its current pinned package and installation are unchanged.
