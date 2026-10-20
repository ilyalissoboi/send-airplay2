# D108: focused packaged automatic audio-only gate

2026-10-20 (Asia/Tokyo). The user authorized the focused approach after native
[PR #92](https://github.com/ilyalissoboi/send-airplay2/pull/92) merged as `55f8ac9`.
The native [D107 gate](automatic-audio-only-native-gate.md) remains the complete
format/listening matrix. D108 verifies the new profile's integration with the
packaged C# host, brokered sources and app-owned fragment storage.

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

## Current checkpoint

Implementation and preparation are in progress on `codex/audio-only-uwp-focused-gate`.
The enabled native UWP build/binary checks, Native AOT package build (zero warnings)
and existing C# binding CTest pass. Actual package registration, automated brokered
checks and the three receiver casts remain pending; do not infer their success.
All generated media remain in ignored local storage with Apache-2.0 original
provenance. No private media is used. Screenbox routing/queues follow this gate
and review/merge; speaker/RAOP, discrete surround, Chromecast, remote-Stop and
long-input/cache-quota deferrals remain unchanged.
