# Screenbox AirPlay Preview release

2026-10-17 (Asia/Tokyo). After fork #15 merges as `7cea7023`, the user explicitly
instructs creating a Release build and GitHub release. All four final-head fork
checks pass at `7f80fa25` in
[run 39009750840](https://github.com/ilyalissoboi/Screenbox/actions/runs/39009750840).
The prior written settings-layout/persistence confirmation is complete.

The [published prerelease](https://github.com/ilyalissoboi/Screenbox/releases/tag/airplay-preview-0.1.0-preview.1)
contains the x64 installer ZIP, corresponding source ZIP, INSTALL.md,
BUILD-INFO.json and SHA256SUMS.txt. Its tag resolves to merged
`7cea7023aeedf5fd35605ba71c051f036bd9d1c7`. The tag deliberately avoids the
upstream `v**` workflow; no Store upload, Sentry DSN or winget submission is used.
User approval is complete; do not republish/replace these assets.

## Build and package scope

An isolated export of that exact merged source is built with Visual Studio 2026
MSBuild, Release/x64, `AirPlayStereoAac=true`, `AppxBundle=Never`, signing disabled
and `UapAppxPackageBuildMode=SideloadOnly`. The version is 0.1.0.0 and the app
uses `ilyalissoboi.Screenbox.AirPlayPreview`, an unsigned preview publisher and
the display name Screenbox AirPlay Preview. Only the project version/copyright
metadata and manifest identity/display metadata differ from the merged export;
the complete resulting source and BUILDING-PREVIEW.md are supplied. The unsigned
OID prevents identity collision with signed apps. The preview has separate app
data; existing Store/debug app data is not migrated or reset.

The pin remains SendAirPlay2.StereoAac 0.5.0-ci.294, SHA-256
`a06216e954e8ef1e2452abe8472e95fbbc04c036e5e1c017e4a92642b20c76b8`.
Screenbox retains GPL-3.0 and its existing notices. The native library/binding
retain Apache-2.0; all 40 pinned DLL/source/notice files, including the existing
dynamic FFmpeg LGPL redistribution material, match the published package.
No third-party implementation/dependency is added. The source archive preserves
the original LICENSE and empty Sentry DSN. BUILDING-PREVIEW.md gives the exact
profile/build commands; binary reproducibility is not claimed.

## Validation and installation

All 127 release-source logic checks pass with zero failed/skipped. The actual
MSIX builds, all 40 pinned files match, and both bundled x64 framework manifests
meet the app's name/publisher/minimum-version requirements and contain signature
entries. All files in the installer ZIP have checksum coverage. The source ZIP
is compared to the merged export: only the two documented metadata files differ,
plus the added build instructions. No PDB, signing key or certificate is bundled.

The unsigned Windows 11 test package requires an elevated install using
`Add-AppxPackage -AllowUnsigned`, as described by
[Microsoft](https://learn.microsoft.com/en-us/windows/msix/package/unsigned-package).
The initial normal-token attempt is refused. The first elevated attempt requests
redundant shared-framework installation and is refused while unrelated apps use
those frameworks. The corrected installer reuses compatible installed Microsoft
x64 frameworks and passes only missing/outdated dependencies to deployment.
It does not force-close unrelated apps. The exact corrected installer contents
pass elevated installation; Windows activation succeeds and the running
executable hash matches the inspected MSIX after five seconds. This is an
installation/process smoke check, without a visual or receiver claim. The preview
is left installed; Store/debug app identities and data remain intact.

After draft upload, all five GitHub asset sizes/digests match local originals.
The authorized prerelease is then published. All five assets download without
authentication and match SHA-256/size; the tag commit is checked separately.
See [machine-readable evidence](validation/screenbox-airplay-preview-release-windows-2026-10-17.json).

The new Casting group/icon and default-off permission are included. Unsupported
audio conversion remains stereo; the 512-MiB cache budget is unchanged. D94's
bounded Mac/Apple TV observations apply to the recorded development build,
without establishing a new receiver run for this preview identity. No whole-film,
HLG receiver, audio-only speaker or discrete converted-surround validation is
added. Chromecast and hardware/user-deferred work remain deferred. A next runtime
implementation is not started by publication; sustained long-input/cache-quota
behavior remains a suggested evaluation.
