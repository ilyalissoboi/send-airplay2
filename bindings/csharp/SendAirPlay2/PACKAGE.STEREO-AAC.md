# SendAirPlay2.StereoAac

Opt-in experimental Windows x64 UWP runtime for video casting with automatic
audio selection: eligible AAC-LC/AC-3/E-AC-3 is copied, and the validated
DTS-core/FLAC/Opus/Vorbis/PCM/TrueHD subset converts to 48-kHz stereo AAC.
Video is copied. This does not add audio-only AirPlay transport or discrete
converted surround. The API is experimental (version 5).

Reference this package **instead of** `SendAirPlay2`. Both contain the same
`SendAirPlay2` C# namespace/assembly. The original codec-free package and its
published versions remain separate. Dependencies must also select the same profile;
mixed direct or transitive profiles are rejected during asset resolution. This first
profile includes only `win-x64`.

The host must use `CastDelivery.AutomaticAudioHls`, provide an
`AacFragmentStore` and quota, and supply brokered media callbacks and its
`ICredentialStore` in UWP. Delivery still defaults to progressive. Use a
RID-specific UWP build, the app C runtime framework, and the network capabilities
documented in the repository. Do not infer support for arbitrary files from a
decoder name or the finite Apple HLS delivery table.

`AUDIO-PROFILE.json` records exact codec hashes/configuration and source archive
checksum. `BUILD-INFO.txt` identifies core/binding source and all native hashes.
The core/binding remain Apache-2.0. Dynamic FFmpeg runtime libraries use the
recorded LGPL-2.1-or-later profile; keep `THIRD-PARTY-NOTICES.txt` and
`source/ffmpeg` when redistributing. That directory contains the pinned original
source archive, exact overlay/patches, codec manifest/triplet and port records.
Use the pinned vcpkg revision and configuration in `AUDIO-PROFILE.json` to rebuild;
the runtime uses no external codec libraries, GPL/nonfree/version-3 additions,
network protocols or command-line programs. Runtime DLLs remain separate for
replacement/relinking. This package is not a Store certification result.

Recorded short-media receiver checks cover one MacBook Pro/macOS 26.7.1 and
Apple TV/tvOS 26.6 from Windows x64. They do not establish whole-film throughput
or additional receiver/architecture support. See `docs/automatic-audio-receiver-gate.md`
and `docs/nuget-package.md` in the repository for exact results and limits.
