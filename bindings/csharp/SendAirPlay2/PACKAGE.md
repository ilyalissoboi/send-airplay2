# SendAirPlay2

Experimental native AirPlay 2 video sender for UWP and .NET hosts on Windows:
receiver discovery, PIN pairing into a host credential store, casting a local
file (directly, or remuxed to HLS from MP4/MOV or MKV without transcoding) and
playback control.

The API is not frozen. Receiver testing covers one Apple TV 4K (tvOS 26.6) from
Windows 11 hosts, including a packaged UWP app with Native AOT.

## Contents

- `lib/netstandard2.0/SendAirPlay2.dll`: the C# binding.
- `runtimes/win-<arch>/native/`: `send_airplay2.dll` and OpenSSL's `libcrypto`,
  built for UWP (AppContainer, app C runtime from the `Microsoft.VCLibs.140.00`
  framework; Botan is linked in). An app built for an architecture this
  package does not carry cannot cast.
- `BUILD-INFO.txt`: the package version, source commit, architectures and
  SHA-256 of each native library.
- `THIRD-PARTY-NOTICES.txt`: OpenSSL (Apache-2.0), Botan (BSD-2-Clause) and
  Boost (BSL-1.0). Keep it with redistributed apps.

## Requirements in an app

- A RID-specific build (`win-x64`, `win-x86`, `win-arm64`), so the native
  libraries are copied next to the app.
- The receiver connects back to the app to fetch media: a packaged app needs
  `privateNetworkClientServer` and a network Windows classifies as Private
  (`internetClientServer` for Public networks).
- A credential store (`ICredentialStore`), such as one over `PasswordVault`;
  the built-in Windows Credential Manager store is not available to UWP apps.

Documentation: https://github.com/ilyalissoboi/send-airplay2 (see
`docs/csharp-binding.md`, `docs/public-api.md` and `docs/nuget-package.md`).
