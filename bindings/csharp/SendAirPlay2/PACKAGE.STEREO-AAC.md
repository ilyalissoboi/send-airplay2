# SendAirPlay2.StereoAac

Opt-in experimental API 6 Windows x64 UWP runtime for video and finite audio-only
casting. `AudioDeliveryProfile.AppleHlsV1` copies video and eligible selected
AAC-LC/AC-3/E-AC-3, or converts the validated video-track
DTS-core/FLAC/Opus/Vorbis/PCM/TrueHD subset to 48-kHz stereo AAC.
`AudioDeliveryProfile.AppleAudioV1` selects original progressive AAC/M4A or MP3,
or converts covered audio-only inputs to stereo AAC HLS. Profiles are explicit;
they are not selected from receiver discovery or a failed cast. Speaker/RAOP
transport and discrete converted surround are not included.

Reference this package **instead of** `SendAirPlay2`. Both contain the same
`SendAirPlay2` C# namespace/assembly. The original codec-free package and its
published versions remain separate. Dependencies must also select the same profile;
mixed direct or transitive profiles are rejected during asset resolution. This first
profile includes only `win-x64`.

The host selects `CastDelivery.AutomaticAudioHls` and the matching `AudioProfile`,
and supplies brokered media callbacks and its `ICredentialStore` in UWP.
Conversion requires `AudioConversionPermission.AllowStereo`, a dedicated
`AacFragmentStore` and a finite positive quota. Preserve-original refuses required
conversion; it does not try another representation or track. Progressive
AAC/MP3 requires no encoder/store/quota and works with either permission. Delivery
still defaults to progressive, and the automatic profile defaults to the video
profile. Use a
RID-specific UWP build, the app C runtime framework, and the network capabilities
documented in the repository. Do not infer support for arbitrary files from a
decoder name or the finite Apple HLS delivery table.

The initial AppleAudioV1 table admits validated single-track, unprotected stereo
44.1/48-kHz AAC-LC in MP4/M4A and stereo 44.1/48-kHz CBR/VBR MP3 progressively.
Conversion covers the finite native FLAC/PCM WAVE, FLAC/PCM MP4/MOV or canonical
indexed Matroska, and single-stream Ogg Opus/Vorbis subsets. Checked presentation
edits/trim are required; Ogg has a 64-MiB scan limit. Video tracks, raw ADTS,
protected/unvalidated inputs and audio-only DTS/AC-3/E-AC-3/TrueHD refuse under
this initial profile. Read `docs/automatic-audio-only.md` for exact bounds.
`AudioDelivery.Check(..., profile: AudioDeliveryProfile.AppleAudioV1)` prepares
and reports the decision without a receiver; successful preparation alone is not
full-file decoding or interoperability proof.

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

See `docs/receiver-validation.md` for the recorded host/receiver scope and
`docs/audio-transcoding.md` for finite video-track input and resource limits.
Preparation or package-build success does not establish full-file throughput,
gapless playback or compatibility with an additional receiver.
