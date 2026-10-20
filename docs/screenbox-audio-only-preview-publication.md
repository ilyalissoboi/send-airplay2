# D114: Screenbox audio-only preview publication

2026-10-21 (Asia/Tokyo). After [Screenbox #18](https://github.com/ilyalissoboi/Screenbox/pull/18)
and [native #98](https://github.com/ilyalissoboi/send-airplay2/pull/98) merged as
`200d7bfaf03fc05593b91311e27b70479a06ff47` and
`3089508e0827511d712b471c8efd7fd8d98f582d`, the user explicitly instructed
"Publish release". All 17 checks passed at native head `2f065b4`. The fork
candidate PR is docs-only and has no reported checks; the original clean-main
build [39430175011](https://github.com/ilyalissoboi/Screenbox/actions/runs/39430175011)
passes all four checks. The missing-notes review thread is resolved at `7de326bb`.

## Published release and public verification

[Screenbox AirPlay Preview 0.2.0-preview.1](https://github.com/ilyalissoboi/Screenbox/releases/tag/airplay-preview-0.2.0-preview.1)
is a public GitHub prerelease (release ID `418990083`), with exactly
five assets. The tag `airplay-preview-0.2.0-preview.1` resolves to the original
build commit `b2b987d68b844ef0e3f577cc0622d223a329adf9`, not the later docs merges.
The public body matches the reviewed notes in fork `docs/releases/airplay-preview-0.2.0-preview.1.md`.
No source or asset was regenerated or modified during publication.

| Public asset | Bytes | SHA-256 |
| --- | ---: | --- |
| [BUILD-INFO.json](https://github.com/ilyalissoboi/Screenbox/releases/download/airplay-preview-0.2.0-preview.1/BUILD-INFO.json) | 2,092 | `714187a3b2cfc16454a527bcf5c2e202a630112c653385de9cc0398cfd7f2a61` |
| [INSTALL.md](https://github.com/ilyalissoboi/Screenbox/releases/download/airplay-preview-0.2.0-preview.1/INSTALL.md) | 3,135 | `8b65bb28f9e3e5eb4c4bd57b5e965ec667de1bfb351ccd8adff0a91a0de5f53d` |
| [Screenbox-AirPlay-Preview-0.2.0-preview.1-source.zip](https://github.com/ilyalissoboi/Screenbox/releases/download/airplay-preview-0.2.0-preview.1/Screenbox-AirPlay-Preview-0.2.0-preview.1-source.zip) | 13,990,405 | `aed7985b140a9e47e930906129d7cdc6bbd30e1d1dd9b8a337a2e9c728aee06e` |
| [Screenbox-AirPlay-Preview-0.2.0-preview.1-x64.zip](https://github.com/ilyalissoboi/Screenbox/releases/download/airplay-preview-0.2.0-preview.1/Screenbox-AirPlay-Preview-0.2.0-preview.1-x64.zip) | 81,425,296 | `63c3f25948520e5555f91121930391e697a230cf8de1dcc2020451c277e646a0` |
| [SHA256SUMS.txt](https://github.com/ilyalissoboi/Screenbox/releases/download/airplay-preview-0.2.0-preview.1/SHA256SUMS.txt) | 398 | `db30bc2ae3c84ca2415cfad6cbbce9464a7340e11b0e9461f2dac9e909e8f825` |

Every asset was downloaded afresh through its public URL using Python urllib,
without credentials or Authorization headers. All five return HTTP 200 and match
the retained candidate's sizes, SHA-256 hashes and GitHub asset digests.
Public API metadata independently verifies prerelease status, the exact notes,
asset set and resolved source tag. The [sanitized record](validation/screenbox-audio-only-preview-publication-windows-2026-10-21.json)
contains release/asset IDs and the verification results. The D113 preparation
JSON retains its original preparation-time approval/publication state.

[Screenbox #19](https://github.com/ilyalissoboi/Screenbox/pull/19) updates the fork's
candidate document to record the completed publication; it changes no release asset.

## Scope and follow-up

The [D113 build, source, installer upgrade and process checks](screenbox-audio-only-preview-candidate.md)
retain their original scope. This publication adds no new build, installation,
visual or receiver observation. D112 development-runtime receiver observations
and the later opening-metadata regression evidence remain distinct from the
published preview binary. No unchanged cancellation or codec matrix was repeated.

The release is unsigned Windows 11 x64, package version `0.2.0.0`, using the
existing preview identity and public `SendAirPlay2.StereoAac 0.6.0-ci.330`.
Follow its `INSTALL.md`; previous preview data is preserved on upgrade, and
Store/debug identities remain separate. Existing release assets are unchanged.
No Store, winget, nuget.org or additional library publication is included.

Approval, publication and public-download verification are complete. Do not
republish or replace these assets. Later development needs its own selected
scope; no further receiver check is required merely to record publication.
Speaker/RAOP transport, discrete converted surround, Chromecast hardware,
user-deferred remote-Stop classification and long-input/cache-quota evaluation
remain deferred. The quota stays 512 MiB and fresh-install conversion defaults OFF.
