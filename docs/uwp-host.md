# Packaged UWP example and test host

[tools/uwp-host](../tools/uwp-host) demonstrates the C# binding inside an
AppContainer app with Native AOT, MSIX tooling and brokered file access. Its
project targets .NET 10 and the Windows SDK version declared in the project.
It is a test host, not the Screenbox player or a certification result.

## Build

Build the [UWP native runtime](uwp-native-build.md) first. Use Visual Studio 2026
MSBuild with the UWP/MSIX and Native AOT components available, from a developer
shell where `msbuild` and `vswhere` resolve:

```powershell
msbuild tools/uwp-host/SendAirPlay2.UwpHost.csproj /restore /t:Publish `
  /p:Configuration=Release /p:Platform=x64 /p:RuntimeIdentifier=win-x64 `
  /p:SelfContained=true /p:GenerateAppxPackageOnBuild=true `
  /p:AppxBundle=Never /p:UapAppxPackageBuildMode=SideloadOnly `
  /p:Sap2NativeDir="$PWD/build-uwp/Release"
```

Use VS MSBuild for this UWP project. For enabled conversion additionally pass
`/p:Sap2StereoAac=true`, the enabled UWP runtime directory, and
`/p:Sap2FfmpegShare=<matching-installed-share/ffmpeg>`. Required DLLs/notices are
validated explicitly; unrelated libraries are not picked up by a codec wildcard.

The unsigned MSIX is produced under the project's AppPackages directory. Install
using an appropriate sideloading/developer registration workflow, with required
framework dependencies. Preserve package identity and increment version for
in-place updates when you want to retain vault records and file grants. Do not
uninstall/reset application data as an incidental test step.

## Host boundaries exercised

- `PasswordVaultStore` holds opaque Base64 credential records per profile.
- `StorageFileMediaSource` reads picker-granted files; a pathname does not bypass
  the AppContainer file-access boundary.
- Picked files can be remembered through FutureAccessList using short labels.
- Discovery, pairing, playback and controls use the public binding/native API.
- The fixed-field host log omits receiver addresses/names, private paths, PINs
  and media URLs. Sender telemetry remains distinct from audible/visible results.

The default manifest has `internetClient` and `privateNetworkClientServer`.
The receiver must connect back to fetch media, so Windows network classification
and app server capability matter. `/p:InternetServerCapability=true` selects the
manifest adding `internetClientServer`. Use the correct declared capabilities;
do not silently modify network policy to make a test pass.

## Script mode

The execution alias `sap2-uwp-host NAME` reads `LocalState/scripts/NAME.txt`.
Names/labels use 1..32 lowercase letters, digits or `-`. One script runs at a
time; blank lines/comments are ignored, and the first failing command stops it
and any cast it left active.

| Command | Effect |
| --- | --- |
| `discover [NAME]`, `discover-model MODEL` | Select a discovered endpoint; model selection requires exactly one match |
| `profile NAME` | Select the host-store profile |
| `cast vault\|builtin LABEL [remux\|progressive\|automatic\|stereo] [audio-only] [start=SECONDS] [preserve] [type=...]` | Cast a remembered grant; remux is default |
| `wait SECONDS`, `wait-state STATE SECONDS`, `wait-end SECONDS` | Bounded coordination |
| `pause`, `play`, `seek SECONDS`, `status`, `stop`, `exit` | Controls, teardown and exit |
| `fixtures`, `audio-fixtures` | Manually grant the known owned video/audio fixture folders |
| `audio-checks` | Offline audio-only decisions and callback/store cleanup checks over owned grants |

`audio-only` requires automatic mode; `preserve` also requires automatic mode.
Explicit progressive MIME is limited to `audio/mp4`, `audio/mpeg` or `video/mp4`.
The form's progressive M4A/MP3 handling is separate from script-only automatic
profile selection. Check the actual selected route instead of assuming the UI
has chosen it from a file extension.

Automatic/stereo scripts use a per-cast TemporaryFolder fragment store and a
512-MiB quota. Disposal logs fixed decision/read/cache counters. Offline folder
checks verify decisions, borrowing, cleanup and sibling preservation; they do
not replace listening tests. Source/parser coverage can be tested broadly offline
while representative changed paths get focused receiver observation.

## Offline runtime harness

`tools/uwp-host/stereo-validation` runs private in-process conversion checks on
bundled and manually granted owned files. It requires the explicitly enabled
runtime-validation export. It verifies package loading, brokered callbacks,
TemporaryFolder access, bounds and cleanup without a receiver. A successful
desktop test cannot substitute for this AppContainer boundary, and a successful
offline AppContainer run cannot establish receiver playback.
