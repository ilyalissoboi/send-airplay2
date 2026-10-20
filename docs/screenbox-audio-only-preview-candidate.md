# D113: Screenbox audio-only preview candidate

2026-10-20 (Asia/Tokyo). The user authorized preparing an updated candidate after
Screenbox #17 merged as `b2b987d68b844ef0e3f577cc0622d223a329adf9` and native
#97 merged as `65dd17f508220860bdaff4b73eca23f4b82be538`. All four/17 checks
passed at final heads `2a001be1`/`6effcf1` respectively.

**Historical preparation record.** The candidate was explicitly approved and
[published on 2026-10-21](screenbox-audio-only-preview-publication.md); all five
public downloads match these retained bytes. The preparation-time JSON remains unchanged.
[Fork PR #18](https://github.com/ilyalissoboi/Screenbox/pull/18) contains the
reviewable candidate details and proposed publication-ready release notes.
The [sanitized record](validation/screenbox-audio-only-preview-candidate-windows-2026-10-20.json)
identifies the exact source, package, five assets and scoped checks.

## Source and package scope

The candidate is an isolated export of exact merged fork main `b2b987d6`, built
with Visual Studio 2026 MSBuild as unsigned Release/x64 with AirPlayStereoAac=true.
Package version 0.2.0.0 retains the existing preview identity/publisher so it can
upgrade 0.1.0.0 without resetting that preview's data. Store/debug identities
remain separate. Only project version/copyright and manifest version/identity/
display metadata differ from the export, plus BUILDING-PREVIEW.md. The source
archive excludes generated/downloaded files; original GPL-3.0 LICENSE, NOTICE.md
and empty Sentry DSN are preserved.

The exact fork SourceRevisionId is explicit and source-control discovery disabled,
so the export cannot inherit a parent checkout's revision. Assembly informational
version verifies the original source commit. Binary reproducibility is not claimed.
The library pin stays public SendAirPlay2.StereoAac 0.6.0-ci.330, SHA-256
`9567655c5c4f0183fddeaea9f497d869d28b79036d599cbf5d87f241e74eb3ad`.

## Completed preparation checks

All 176 isolated release-source logic tests pass with zero failed/skipped. The
actual Release MSIX, preview/unsigned manifest, all 40 pinned native/source/notice
files, both x64 framework identities/versions/signature entries and complete
source/installer/asset checksums verify. Source comparison admits only the two
documented metadata changes and build instructions. No PDB, signing key or
certificate is included in the installer. Existing translation, legacy migration
and third-party trim warnings remain.

Clean-main [workflow 39430175011](https://github.com/ilyalissoboi/Screenbox/actions/runs/39430175011)
uses the exact merged commit and passes all four checks, including opt-in tests/
packaging. The candidate itself is the separately inspected local preview build;
CI success is not receiver interoperability or a claim that these are CI artifacts.

The exact installer ZIP contents pass elevated in-place deployment from preview
0.1.0.0 to 0.2.0.0. Its identity is preserved; no app-data reset is performed,
and Store/debug registrations remain unchanged. Compatible shared Microsoft
frameworks are reused. Windows activation, actual package/AppContainer identity,
five-second process survival and the running executable hash match the inspected
MSIX. The preview remains installed/open. These are installation/process checks;
there is no new visual or receiver observation.

## Retained assets and publication boundary

| Asset | Bytes | SHA-256 |
| --- | ---: | --- |
| Screenbox-AirPlay-Preview-0.2.0-preview.1-x64.zip | 81,425,296 | `63c3f25948520e5555f91121930391e697a230cf8de1dcc2020451c277e646a0` |
| Screenbox-AirPlay-Preview-0.2.0-preview.1-source.zip | 13,990,405 | `aed7985b140a9e47e930906129d7cdc6bbd30e1d1dd9b8a337a2e9c728aee06e` |
| BUILD-INFO.json | 2,092 | `714187a3b2cfc16454a527bcf5c2e202a630112c653385de9cc0398cfd7f2a61` |
| INSTALL.md | 3,135 | `8b65bb28f9e3e5eb4c4bd57b5e965ec667de1bfb351ccd8adff0a91a0de5f53d` |
| SHA256SUMS.txt | 398 | `db30bc2ae3c84ca2415cfad6cbbce9464a7340e11b0e9461f2dac9e909e8f825` |

These original bytes were retained under ignored validation storage and are now
published with explicit approval. Tag `airplay-preview-0.2.0-preview.1` targets
`b2b987d68b844ef0e3f577cc0622d223a329adf9`, not a later documentation commit.
The [D114 publication record](screenbox-audio-only-preview-publication.md) verifies
the exact five assets, reviewed notes and fresh unauthenticated downloads.
Do not regenerate bytes under this version or replace earlier ci.330/preview
assets. No Store, winget or nuget.org publication is included.

[D112 receiver evidence and its review follow-up](screenbox-automatic-audio-only.md)
retain their original development-runtime hashes. This candidate adds no new
receiver/visual result, whole-film, gapless or sample-exact claim; no unchanged
cancellation/full codec listening matrix is repeated. Speakers/RAOP, discrete
converted surround, Chromecast hardware, remote-Stop classification and
long-input/cache-quota evaluation remain deferred. The quota stays 512 MiB.
