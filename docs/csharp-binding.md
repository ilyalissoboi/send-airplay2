# C# binding

`bindings/csharp/SendAirPlay2` targets `netstandard2.0` and wraps API 6 without
NuGet dependencies. It supports modern .NET and UWP hosts; deployment must
provide a matching native library and its dependencies. Released older bindings
retain their own API requirement.

| Native API | Managed entry point |
| --- | --- |
| `sap2_cast_*` | `Cast`: Create, Start, GetStatus, WaitForChange, Play, Pause, Seek, StopPlayback, Stop, Dispose |
| `sap2_cast_options` | `CastOptions` |
| `sap2_media_source` | `MediaSource`, `FileMediaSource` |
| `sap2_credential_store` | `ICredentialStore` |
| Pairing / removal | `Pairing.Pair`, `Pairing.ForgetProfile` |
| Discovery | `Receivers.Discover` returning independent `Receiver` copies |
| Track inventory | `MediaTracks.Probe` |
| Offline audio preparation | `AudioDelivery.Check` |
| Conversion store | `AacFragmentStore`, `FileAacFragmentStore` |

The library resolves `send_airplay2` using platform loading rules. A modern .NET
host can use `NativeLibrary.SetDllImportResolver`; the shared-library tests show
how. UWP deployment needs [AppContainer binaries](uwp-native-build.md), a
RID-specific build and a host credential store.

## Delivery options

Select `CastDelivery.AutomaticAudioHls` and an explicit `AudioProfile`:
`AudioDeliveryProfile.AppleHlsV1` for video or `AppleAudioV1` for audio-only input.
`CastStatus.AudioDecision` reports the successful route. The audio-only profile
can deliver original AAC/MP3 progressively despite the delivery enum's name.

Use `AudioConversionPermission.PreserveOriginal` to refuse required conversion,
or `AllowStereo` with an enabled codec runtime, per-cast fragment store and
positive quota. Conversion permission does not choose the audio/video profile.
See [video policy](audio-transcoding.md) and [audio-only policy](automatic-audio-only.md).

## Ownership and callbacks

`Cast` owns a SafeHandle. Callback delegates and their contexts stay rooted until
native destroy returns, including finalizer cleanup. P/Invoke SafeHandle reference
counting delays destroy while another managed call is in flight. Dispose promptly
instead of relying on finalization.

The binding releases a media source once, both when native ownership transfers
and later ends, and when managed/native create fails before transfer. Exceptions
never cross native callbacks: a throwing read fails that request, a throwing
store reports unavailable, and a throwing PIN reader cancels. Use the native
[threading and lifetime contracts](public-api.md) when implementing adapters.

Offline `AudioDelivery.Check` borrows callbacks, whereas successful `Cast.Create`
transfers their native lifetime. Unused automatic-delivery stores receive no
clear callbacks but still release at cast disposal. Host adapters should remove
their own empty reservations without touching another cast's storage.

PIN readers receive a mutable `char[]`; temporary PIN/record arrays are cleared.
Managed strings and host-held copies cannot be reliably erased by the binding.
Never log credentials or persist them in ordinary application settings.

## Tests and example

The CMake shared build runs `csharp_binding_tests` when .NET SDK 8+ is available
and `SAP2_DOTNET_TESTS` is enabled. It checks ABI layout, version/default mapping,
callbacks across garbage collections, exactly-once release, refusal paths and
automatic delivery. These are offline tests.

The [packaged UWP host](uwp-host.md) demonstrates `PasswordVault`, brokered
`StorageFile` input, discovery and controls. The [package guide](nuget-package.md)
describes native asset copying and the two mutually exclusive package profiles.
