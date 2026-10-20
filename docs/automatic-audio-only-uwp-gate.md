# D108: focused packaged automatic audio-only gate

2026-10-20 (Asia/Tokyo). **The focused packaged gate passed.** The user authorized the approach after native
[PR #92](https://github.com/ilyalissoboi/send-airplay2/pull/92) merged as `55f8ac9`.
The native [D107 gate](automatic-audio-only-native-gate.md) remains the complete
format/listening matrix. D108 verifies the new profile's integration with the
packaged C# host, brokered sources and app-owned fragment storage.
[PR #93](https://github.com/ilyalissoboi/send-airplay2/pull/93) and the
[sanitized evidence](validation/automatic-audio-only-uwp-windows-2026-10-20.json)
record exact package/runtime bytes, automated checks and written observations.

## Scope and distinct paths

The development host adds `automatic audio-only` script selection, mapping to
`AudioDeliveryProfile.AppleAudioV1`. Existing form/default/video behavior stays
unchanged. Fixed-field status now includes profile/reason. Two offline commands,
`audio-fixtures` and `audio-checks`, grant the known owned fixture names and run
the public preparation path without credentials or a receiver. The diagnostic
assembly joins the existing friend assemblies solely to release borrowed sources
and verify ownership using the binding's existing internal helpers.

The automated matrix uses the same twelve owned inputs as D107, with literal
profile/action/reason/source/output/identity expectations. Every brokered stream
caps reads at 1021 bytes to exercise short reads. Preparation must borrow its
source; converted stores clear/release once, progressive stores release unused
without Clear, and every child directory disappears while a sibling sentinel
survives. The caller then releases each borrowed source exactly once. A separate
FLAC preserve-original refusal checks unused-store cleanup. Actual granted-file
hashes must match the external owned-fixture provenance before accepting results.

The enabled x64 Native AOT package advances the default manifest to 0.1.36.0
(alternate-server manifest 0.1.37.0). Register in place with identity, app data,
PasswordVault pairings and existing grants preserved. Verify the actual MSIX,
AppContainer token, executable and all six native DLL paths/hashes. The runtime
is merged API 6 with the existing FFmpeg 9.0.2 port-6 profile; no native C++ or
codec/dependency change is included. Network capabilities/quota stay unchanged.
Offline checks use a 16-MiB per-owner test budget; receiver casts retain the
host's existing 512-MiB quota. No cache-limit increase is made.

## Focused receiver checks

After automated preparation/cleanup succeeds, run just one converted native-FLAC
audio-only control/natural-end case on each recorded receiver, plus one separate
explicit Stop on Apple TV. Start near eight seconds, confirm RIGHT pulses,
pause/resume, seek32 higher BOTH, seek8 RIGHT and natural end after seek58. The
Stop cast starts at zero (LEFT) and stops after eight seconds; require no delayed
restart. Written user observations remain separate from sender telemetry.

Earlier UWP progressive AAC/MP3, video-conversion lifecycle and native format
results are reused. No unchanged pending-consent cancellation or complete manual
codec matrix is repeated. Use saved pairings; do not reset consent/network settings.
No Computer Use visual confirmation is used. This is not a Screenbox queue test,
sample-exact/gapless proof, long-input scaling result or publication.

## Recorded results and limits

Implementation `c868572` passes the enabled native UWP build and six-DLL binary
checks, the Native AOT MSIX build with zero warnings/errors, and the existing C#
binding CTest (1/1, 14.41 seconds). Native runtime source is merged `55f8ac9`,
with `native_changes=no`. The merged base's final head `77dca30` passed all 17
CI checks. PR #93 CI is a separate actual-head check; successful local or base
checks do not establish its result.

Development package 0.1.34.0 was used only to grant the owned folder and inspect
preparation. Before running the checks, an incorrect test-only MP3 output-family
expectation was corrected: progressive MP3 retains family 10, not AAC family 1.
Final package 0.1.36.0 updates it in place and retains that grant. All accepted
offline and receiver results belong to the final package; no failed receiver
attempt or library fix is involved in this preflight correction.

The actual final AppContainer token, executable and six loaded native hashes match
the inspected MSIX/native build in three captures: before receivers, after Mac,
and after Apple TV Stop. Identity, app data, pairings and grants are preserved.
The required FFmpeg notice matches its existing source. Generated media stay in
ignored local storage with Apache-2.0 original provenance; no private media is used.

All twelve actual brokered-input hashes match their original fixture provenance.
Thirteen offline checks pass: three progressive sources release unused stores
without Clear; nine converted sources clear/release their stores; the FLAC
preserve-original refusal also releases unused storage without Clear. Every
source is borrowed during preparation and caller-released once afterward. All
children disappear and the unrelated sibling survives. Three invalid script
combinations (audio-only with remux/progressive/forced stereo) refuse before file
access or receiver setup.

| Receiver | User-observed playback/control result | Separate Stop |
| --- | --- | --- |
| MacBook Pro, macOS 26.7.1 | FLAC converted to stereo AAC: RIGHT nonzero start, pause/resume, both seek markers and natural end pass | Reused earlier lifecycle evidence |
| Apple TV, tvOS 26.6 | Same converted-FLAC checks pass | LEFT pulses, eight-second Stop, clean silence and no delayed restart pass |

The user explicitly confirms both versions unchanged and all three cases passed
after completion. Sender telemetry separately reports AppleAudioV1/StereoAac,
FLAC-to-AAC, original ID 1, two channels and the expected MediaEnd/SenderStop.
Each cast seals 11 fragments (1,221,864 bytes), rereads the cache, releases its
source once, clears/releases its store once and removes its child. Natural-end
pre-disposal status can report cleaned=no; the following release/cache records
establish completed cleanup. Explicit Stop reports cleaned=yes. Final live
inspection finds the diagnostic app idle/alive, zero cache children and zero
sentinels. Across offline checks and casts there are 16 source/store releases.

These bounded seek/control checks do not establish full-duration throughput,
gapless/sample-exact playback or arbitrary formats. The initial Playing snapshot
precedes the nonzero start seek settling; later status and audible observations
are retained separately. The startup log's caller MIME default is ignored by
automatic delivery; the resolved profile/content determines audio-only HLS.
No further D108 manual check is pending. Screenbox routing/queues and their
package/pin prerequisites follow review/merge as separate work. Speaker/RAOP,
discrete surround, Chromecast, remote-Stop and long-input/cache-quota deferrals
remain unchanged. No release or publication is started here.
