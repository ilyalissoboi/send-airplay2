# Packaged UWP test host (D53)

Status: **receiver-tested on one Apple TV / Windows host, 2026-10-08** (D53; discovery
D54). D49 step 2b:
the [C# binding](csharp-binding.md) inside a packaged UWP app built the way
Screenbox is, to measure what Screenbox would meet. Sideloaded locally; not part
of CI. Record: [receiver-validation.md](receiver-validation.md#packaged-uwp-host-d53-2026-10-08)
and its [artifact](validation/native-uwp-host-windows-2026-10-08.json).

## What it is

`tools/uwp-host` is a modern .NET UWP app (`net10.0-windows10.0.26100.0`,
`UseUwp`, Native AOT, `DisableRuntimeMarshalling`, MSIX tooling, CsWinRT 2.3.1,
x64), matching the toolchain settings of Screenbox's project. It packages the
shared native library (`send_airplay2.dll`, `libcrypto-3-x64.dll`, `botan-3.dll`)
and provides:

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
  receiver name), so a run can be read back from outside the app.

Two manifests share one identity (and so one PasswordVault locker):
`Package.appxmanifest` with Screenbox's network capabilities (`internetClient`,
`privateNetworkClientServer`), and `Package.InternetServer.appxmanifest`, which
adds `internetClientServer` (build with `/p:InternetServerCapability=true`).
Switching variants keeps the app's data only as an in-place update, which
Windows allows only to a higher version. Before building the other variant,
raise its `Version` above the installed one (the committed manifests are
0.1.4.0 and 0.1.5.0; the D53 runs used 0.1.0.0, 0.1.1.0 and 0.1.2.0, and D54
used 0.1.4.0).
Alternatively, `Add-AppxPackage -Register` with `-ForceUpdateFromAnyVersion`
registers a lower version over a higher one (untested here).

## Build and install (Windows, Developer Mode)

Build the shared native library first (`build-shared`, Release). Then, from a
shell where `vswhere.exe` is on `PATH` (the Native AOT linker step calls it by
name):

```powershell
$env:PATH += ';C:\Program Files (x86)\Microsoft Visual Studio\Installer'
& 'C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe' `
  tools/uwp-host/SendAirPlay2.UwpHost.csproj /restore /t:Publish /p:Configuration=Release `
  /p:Platform=x64 /p:RuntimeIdentifier=win-x64 /p:SelfContained=true `
  /p:GenerateAppxPackageOnBuild=true /p:AppxBundle=Never /p:UapAppxPackageBuildMode=SideloadOnly
```

The unsigned `.msix` lands in `tools/uwp-host/AppPackages`. Extract it to a folder
and register that layout with `Add-AppxPackage -Register <folder>\AppxManifest.xml`.
Remove it afterwards with `Get-AppxPackage SendAirPlay2.UwpHost | Remove-AppxPackage`
(this also deletes the app's data, including its PasswordVault locker entries).

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

## What this means for Screenbox

- Screenbox's current `privateNetworkClientServer` is enough on networks Windows
  classifies as Private, and not on Public ones, where the receiver's connections
  back to the app (media and timing) are blocked. Supporting Public networks would
  need `internetClientServer`, a capability decision for the Screenbox fork.
- A UWP host cannot use the library's Credential Manager store at all, so it
  needs a host store such as `PasswordVaultStore`, as D49 decided.
- The native library was built for the desktop C runtime and loaded only because
  that runtime is installed system-wide. A UWP-correct build (D49 step 3) must
  target the app C runtime and leave out the Credential Manager adapter.

- Multicast discovery works inside the AppContainer with
  `privateNetworkClientServer` on a Private network (D54). Public networks were
  not measured for discovery.

Not covered: discovery on a Public network, a cast to the discovered address
inside the app, PasswordVault roaming, desktop provisioning of the app's locker,
Store certification (WACK), and x86/ARM64.
