# Packaged UWP test host (D53)

Status: **receiver-tested on one Apple TV / Windows host, 2026-10-08** (D53; discovery
D54; UWP-built native library D55); HLS remux, start positions and script mode
2026-10-10 (D61). D49 step 2b:
the [C# binding](csharp-binding.md) inside a packaged UWP app built the way
Screenbox is, to measure what Screenbox would meet. Sideloaded locally; not part
of CI. Record: [receiver-validation.md](receiver-validation.md#packaged-uwp-host-d53-2026-10-08)
and its [artifact](validation/native-uwp-host-windows-2026-10-08.json).

## What it is

`tools/uwp-host` is a modern .NET UWP app (`net10.0-windows10.0.26100.0`,
`UseUwp`, Native AOT, `DisableRuntimeMarshalling`, MSIX tooling, CsWinRT 2.3.1,
x64), matching the toolchain settings of Screenbox's project. It packages the
UWP-built native library (`send_airplay2.dll` and `libcrypto-3-x64.dll` from
`build-uwp\Release`, [uwp-native-build.md](uwp-native-build.md)); pass
`/p:Sap2NativeDir=<dir>` to package another build, such as the desktop shared one
(which also needs `botan-3.dll`). It provides:

- a **PasswordVault host store** (`PasswordVaultStore`): one credential per
  profile under the resource `send-airplay2`, the record as Base64;
- a **StorageFile media source** (`StorageFileMediaSource`) over a file chosen in
  the file picker;
- pairing with a PIN dialog, casts with the PasswordVault store or the built-in
  store, MRP controls, and a probe of the built-in store;
- a **Discover** button (D54): one 5-second `Receivers.Discover` scan; the
  receiver whose name equals "Receiver name to find" fills the address box.
  Names and addresses are compared, never logged;
- a fixed-field log in `LocalState\host-log.txt` (no address, PIN, path, URL or
  receiver name), so a run can be read back from outside the app;
- a **native abort trace** (D55, `NativeAbortTrace`): a `SIGABRT` handler that
  logs the native stack as module+offset frames when anything calls `abort()`
  (including `std::terminate`), since a packaged app gets no crash dump without
  machine-wide settings. Resolve the offsets with a linker map of the same build;
- a cast that ends by itself (end of media, receiver Stop/Home, connection loss)
  frees the app's single cast slot, so the next Cast works without Stop;
- **HLS remux and start position** (D61): the "HLS remux" checkbox (on by
  default, as Screenbox will cast) selects `CastDelivery.HlsRemux`, and "Start
  at (seconds)" sets `StartPositionSeconds`. The picker accepts `.mkv`. Cast
  start times are logged in milliseconds, including opening the file and, for
  HLS, reading its index;
- **remembered files**: a picked file is also kept in the app's
  `FutureAccessList` under the label in "Remember picked file as" (1-32 of
  a-z, 0-9 and `-`), so scripts can open it later without the picker;
- a **script mode** for automated runs (D61), below.

Two manifests share one identity (and so one PasswordVault locker):
`Package.appxmanifest` with Screenbox's network capabilities (`internetClient`,
`privateNetworkClientServer`), and `Package.InternetServer.appxmanifest`, which
adds `internetClientServer` (build with `/p:InternetServerCapability=true`).
Switching variants keeps the app's data only as an in-place update, which
Windows allows only to a higher version. Before building the other variant,
raise its `Version` above the installed one (the committed manifests are
0.1.22.0 and 0.1.23.0; the D53 runs used 0.1.0.0, 0.1.1.0 and 0.1.2.0, D54 used
0.1.4.0, D55 used 0.1.6.0, 0.1.8.0, 0.1.10.0 and 0.1.12.0, D57 0.1.14.0 and D61
0.1.16.0, 0.1.18.0, 0.1.20.0 and 0.1.22.0).
Alternatively, `Add-AppxPackage -Register` with `-ForceUpdateFromAnyVersion`
registers a lower version over a higher one (untested here).

## Script mode (D61)

`sap2-uwp-host NAME` (an app execution alias declared in both manifests) starts
or activates the app and runs `LocalState\scripts\NAME.txt`. NAME is 1-32 of
a-z, 0-9 and `-`. A running instance receives later activations, so scripts can
follow one another; one runs at a time. Each line is one command; blank lines
and `#` comments are skipped:

| Command | Effect |
| --- | --- |
| `discover [NAME]` | One scan; fills the address (default: the form's receiver name) |
| `discover-model MODEL` | One scan; fills the address only when exactly one receiver matches the model |
| `profile NAME` | Sets the PasswordVault profile; NAME uses the same 1-32 character syntax as script names |
| `cast vault\|builtin LABEL [remux\|progressive] [start=SECONDS]` | Casts the file remembered as LABEL; remux is the default |
| `wait SECONDS` | Waits |
| `pause`, `play`, `seek SECONDS`, `status`, `stop` | The button actions |
| `wait-state STATE SECONDS` | Until the receiver reports that `PlaybackState` |
| `wait-end SECONDS` | Until the cast ends by itself or is stopped |
| `exit` | Stops any cast and closes the app |

The log records the script name, line numbers and verbs, never arguments (a
receiver name). The first failing command ends the script and stops a cast it
left running, so a run can be judged from `Script NAME: done` or
`failed at line N`. Pick each file once with its label before the first
script; the grant survives in-place updates. Observing the TV stays manual.

Found while adding it: under Native AOT, `args is CommandLineActivatedEventArgs`
was false for a command-line activation; testing `ActivationKind` and the
`ICommandLineActivatedEventArgs` interface works. The alias's `Executable` must
name the real file (`SendAirPlay2.UwpHost.exe`); MakeAppx does not expand
`$targetnametoken$` inside an extension.

## Build and install (Windows, Developer Mode)

The project builds for `x64`, `x86` and `ARM64` (`/p:Platform=x86
/p:RuntimeIdentifier=win-x86`, and so on); each packages the UWP native build for
that platform (`build-uwp`, `build-uwp-x86`, `build-uwp-arm64`). An x86 package
runs on x64 Windows; ARM64 packaging needs the ARM64 build tools.

Build the UWP native library first (`build-uwp`, Release; see
[uwp-native-build.md](uwp-native-build.md)). Then, from a shell where
`vswhere.exe` is on `PATH` (the Native AOT linker step calls it by name):

```powershell
$env:PATH += ';C:\Program Files (x86)\Microsoft Visual Studio\Installer'
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  tools/uwp-host/SendAirPlay2.UwpHost.csproj /restore /t:Publish /p:Configuration=Release `
  /p:Platform=x64 /p:RuntimeIdentifier=win-x64 /p:SelfContained=true `
  /p:GenerateAppxPackageOnBuild=true /p:AppxBundle=Never /p:UapAppxPackageBuildMode=SideloadOnly
```

The unsigned `.msix` lands in `tools/uwp-host/AppPackages`. Extract it to a folder
and register that layout with `Add-AppxPackage -Register <folder>\AppxManifest.xml`.
Remove it afterwards with `Get-AppxPackage SendAirPlay2.UwpHost | Remove-AppxPackage`.
That deletes `LocalState`, but the D55 session found the D53 PasswordVault
credential still present after D54's registration had started with an empty
`LocalState`, so removal apparently leaves the app's locker entries. Remove them
with `Pairing.ForgetProfile` before uninstalling if they should go.

Toolchain notes found while building: an AOT publish needs `SelfContained=true`;
the ILCompiler link step needs `vswhere.exe` on `PATH`; the MSIX tooling declares a
`Microsoft.VCLibs.140.00` dependency; the AOT build produced no trimming or AOT
warnings for the binding.

## Results (2026-10-08)

| Run | Network | Capabilities | Result |
| --- | --- | --- | --- |
| Native load | Public | Screenbox's set | AOT app loads the native DLLs inside the AppContainer |
| Built-in store probe | Public and Private | either | Fails: `credential_store` for removing an absent profile and for loading an existing one |
| Pair into PasswordVault | Public | Screenbox's set | **Pass**; the credential survived two in-place updates |
| Cast, PasswordVault | Public | Screenbox's set | **Fails**: `connection` after about 5 s; the TV played nothing (inbound blocked) |
| Cast, PasswordVault | Public | plus `internetClientServer` | **Pass**: normal video and audio |
| Cast and controls, PasswordVault | Private | Screenbox's set | **Pass**: video and audio, pause, both seeks, Home after Stop |
| Discover (D54, 0.1.4.0, fresh registration) | Private | Screenbox's set | **Pass**: one receiver, the expected name, IPv4 address filled in |
| UWP-built library (D55, 0.1.6.0) | Private | Screenbox's set | Loads with the app C runtime from VCLibs; built-in store `unsupported`; discover and cast pass, but the app **crashed at every Stop** (Asio `winapp_thread`) |
| UWP-built library with the reader-pool fix (D55, 0.1.10.0, 0.1.12.0) | Private | Screenbox's set | **Pass**: cast, pause, play, both seeks, Stop and a second cast in one process; a cast ended by remote Home frees the slot; Home after Stop |
| x86 package with the x86 UWP-built library (D57, 0.1.14.0) | Private | Screenbox's set | **Pass**: x86 app runtime from VCLibs; discover, cast, pause, play, both seeks, Stop; app stayed open |
| HLS remux through scripts (D61, 0.1.20.0, 2026-10-10) | Private | Screenbox's set | MKV film and tx3g MP4 started in about 2.5 s from brokered files; controls, subtitles and a natural end passed; **start positions ignored** (playback from 0) |
| Start position fix (D61, 0.1.22.0) | Private | Screenbox's set | **Pass**: MKV at 1:00:00, MP4 at 5:00 and 9:40 (natural end, Home), direct MP4 at 2:00; subtitles shown |

## What this means for Screenbox

- Screenbox's current `privateNetworkClientServer` is enough on networks Windows
  classifies as Private, and not on Public ones, where the receiver's connections
  back to the app (media and timing) are blocked. Supporting Public networks would
  need `internetClientServer`, a capability decision for the Screenbox fork.
- A UWP host cannot use the library's Credential Manager store at all, so it
  needs a host store such as `PasswordVaultStore`, as D49 decided.
- The UWP-built library (D55) links the app C runtime, carries the AppContainer
  flag and has no Credential Manager; D53's desktop-built DLLs had loaded only
  because the desktop C runtime is installed system-wide.

- Multicast discovery works inside the AppContainer with
  `privateNetworkClientServer` on a Private network (D54). Public networks were
  not measured for discovery.

Record for D61: [receiver-validation.md](receiver-validation.md#uwp-host-hls-remux-and-start-position-d61-2026-10-10).

Not covered: discovery on a Public network, pairing through the UWP-built
library on the Apple TV, HLS remux in the x86 or ARM64 package, MKVs without Cues through
brokered access, PasswordVault roaming, desktop provisioning of the app's locker,
an ARM64 run, and Store certification of a signed package (the D57 kit run
on the sideloaded package, repeated on 0.1.22.0 for D61, failed only Supported
APIs, for the .NET Native AOT runtime in the host executable; see
[uwp-native-build.md](uwp-native-build.md#certification-kit-d57)).
