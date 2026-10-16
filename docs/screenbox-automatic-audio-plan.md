# Screenbox automatic audio integration plan (D90 proposal)

2026-10-16. This is an engineering proposal for the next host slice, not an
implemented or user-validated Screenbox feature. Candidate publication and host
implementation remain separate actions. Read the fork's current instructions
again before editing it. The inspected remote main is
[`ce782356376d7991f23219abd25b3ada118bd3cc`](https://github.com/ilyalissoboi/Screenbox/tree/ce782356376d7991f23219abd25b3ada118bd3cc).
The older local checkout was not used as evidence of current behavior.

## Current host and scope

At that commit, `Screenbox.Core.csproj` references codec-free
`SendAirPlay2 0.3.0-ci.207`. `scripts/Get-AirPlayPackage.ps1` assumes that package
identity and the `nuget-v<version>` tag scheme. Both test and app CI invoke the
script. `AirPlayCastCoordinator.StartCastAsync` supplies brokered
`StorageFileMediaSource`, a PasswordVault store and `CastDelivery.HlsRemux`, with
no AAC fragment store. Natural item changes retire a cast and start another;
the cast's disposal happens in the background. Those paths must own independent
cache contexts during overlap.

The library's [finite automatic profile](automatic-audio-public.md) selects one
original audio stream, copies eligible AAC-LC/AC-3/E-AC-3, or converts the covered
DTS-core/FLAC/Opus/Vorbis/PCM/TrueHD subset to 48-kHz stereo AAC. It is not receiver
capability negotiation or support for every stream with those codec names. Video
is copied. Audio-only speakers and discrete converted 5.1/7.1 remain deferred.

## Ordered implementation slices

1. **Package consumption and x64 build profile.** After candidate publication and
   public-download verification, add an explicit x64 experimental build option
   that selects `SendAirPlay2.StereoAac` instead of `SendAirPlay2`. Keep ordinary
   builds on the existing profile initially. Reject that option for x86/ARM64;
   do not silently package an x64 DLL into another architecture. Both identities
   supply the same assembly/native paths and must never coexist in one resolved
   graph. Generate a compilation symbol for the opt-in code: the older baseline
   binding does not expose API-5 types, so a runtime-only conditional is insufficient.

   Make the download script choose an explicit identity/version/tag/hash record
   from the selected profile, and pass the same property through restore, tests
   and app packaging. Verify pre-existing local files as well as new downloads;
   a cached filename alone is insufficient evidence. Include matching
   source/notices/rebuild material in the eventual app distribution and verify
   their presence in the packaged output. Do not replace the package's dynamic
   DLLs with unrelated VLC libraries. Inspect the app's applicable packaging
   instructions and redistribution requirements before choosing exact content paths.

2. **Per-cast app storage and lifetime.** Implement a Screenbox-owned
   `AacFragmentStore` adapter using an exclusively created child of the app's
   `ApplicationData.Current.TemporaryFolder`. Use the validated test host's
   [adapter contract](../tools/uwp-host/TemporaryAacFragmentStore.cs) as reference,
   removing fault injection and diagnostics. Seal immutable fragments through
   exclusive temporary files and atomic publication, serialize positional I/O,
   and poll borrowed cancellation controls during bounded work.

   Reserve the cache before opening the media source; abandon only that empty
   reservation if source opening fails before `Cast.Create` is called. Once the
   binding is invoked, follow its consumed-context contract on failure too; never
   manually dispose transferred storage. `Clear` removes only that cast's known
   files after workers drain. Automatic copy/refusal may call release without
   `Clear`, so release must also remove an unused empty reservation. Do not remove
   an old cast's cache on the UI thread while background disposal still uses it.
   Queue changes and retries each get a fresh source/cache pair. Any crash-recovery
   scavenger requires its own ownership design; avoid broad temporary-folder cleanup.

   Retain an awaitable cleanup task in the coordinator. Proposed admission rule:
   wait for the retired cast to drain on a worker before admitting another
   converted cache, while the dispatcher continues handling Stop/item requests.
   Recheck request generation after that await. A per-cast quota alone cannot
   bound app-wide disk use if rapid queue changes accumulate retiring casts.

   Proposed initial quota: 512 MiB of sealed fragments per cast, matching the test
   host. This is a bounded engineering default, not proof of whole-film admission
   or a disk reservation. Preserve the native limit/refusal behavior, handle disk
   exhaustion and report storage failures distinctly; do not add eviction or
   unlimited retry. No playlist or library-database changes are needed.

3. **Policy and user permission.** In the opt-in build, explicitly set
   `CastDelivery.AutomaticAudioHls`, `AudioDeliveryProfile.AppleHlsV1`, the per-cast
   store and quota. Proposed user preference: allow stereo audio conversion for
   AirPlay, initially off; explain that surround/lossless audio becomes stereo.
   Set `PreserveOriginal` when permission is off and `AllowStereo` when on. Set
   both values explicitly: the current enum's zero value is `AllowStereo`, so
   leaving the property unset is not a safe way to express the host preference.
   Keep the original selected stream; never switch tracks or retry another route
   after malformed input, copy failure or conversion failure. Leave Chromecast
   and local playback policy unchanged.

   Extend startup/terminal result mapping for selection, source, codec, resource
   and cache failures. Keep localized strings in the View layer, as required by
   Screenbox instructions. A later richer per-track selector/decision display can
   be planned separately; do not imply the existing local-player track selection
   maps to native source IDs without an explicit mapping.

4. **Validation and rollout.** Use Visual Studio 2026 MSBuild for restore/build,
   the fork's logic-test runner for pure logic, and a UWP Unit Test App for
   XAML-dependent tests. Cover profile selection/download hashes, permission
   mapping, pre-transfer failure, unused-copy release, quota/I/O failures,
   cancellation, Stop during cleanup and rapid queue/cache lifetimes. Build the default profile
   and x64 opt-in MSIX independently; inspect actual resolved identities, all six
   native hashes, sources/notices and installed DLL hashes/AppContainer token.

   Before calling host integration validated, run fresh original AAC-copy and
   DTS-conversion controls on the recorded Mac/Apple TV: synchronization, stereo
   pattern, initial position, pause/resume, seeks, Stop, pending-consent
   cancellation/retry and natural queue advance across copy/conversion items.
   Confirm local paused handoff and cache cleanup after background disposal.
   Library D88 results do not establish Screenbox queue behavior. Print manual
   instructions and ask the user to confirm visual changes; do not use Computer
   Use for visual UI confirmation. Chromecast remains hardware-deferred and
   sleeping Apple TV casting already has a separate earlier user observation.

The proposed build switch, preference default and quota should be reviewed with
the implementation PR. They are not additional user requirements or changes
already applied to the fork.
