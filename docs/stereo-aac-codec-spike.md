# Stereo AAC codec and AppContainer spike (D68)

2026-10-12. The user said **Proceed** after the merged D67 plan, authorizing
the codec/packaging implementation gate. Audio-only AirPlay speakers and
surround-preserving output remain deferred. This development probe is separate
from the existing casting library, playback ABI, C# binding and published
`0.3.0-ci.207` package. It is not a new casting delivery mode.

## Implementation and build profile

`SAP2_AUDIO_CODEC_SPIKE=ON` builds `sap2_codec_probe`, a separate dynamic library,
and an offline desktop test. Its borrowed C callbacks provide positional input,
cancellation and immediate encoded-packet output. No filesystem paths, network,
credential store or receiver session are opened by the native probe. The
AppContainer host supplies StorageFile streams and an app-owned cache folder.

The optional vcpkg `audio-codec-spike` feature uses a restricted FFmpeg 9.0.2
overlay. GPL, nonfree, version-3 additions, programs, protocols/network,
hardware acceleration and unrelated libraries are disabled. Only `avformat`,
`avcodec`, `avutil` and `swresample` are built, as dynamic libraries. Runtime
checks compare their configuration/license strings and require the AAC,
AC-3, E-AC-3 and DTS decoders and AAC encoder. Source/patch/license provenance
is recorded in [dependencies.md](dependencies.md); no FFmpeg runtime source is
vendored into project C++ code. The overlay's narrow finder/wrapper are original;
the other port files/patches derive from the pinned MIT vcpkg port.

UWP linking exposed an upstream WAV dependency on `ff_w64_guid_data` when
Wave64 demuxing is disabled. The original `0053-wav-guid-dependency.patch` links
the constant-data object for WAV; it does not enable a new demuxer. Both desktop
and AppContainer builds must use the final patched profile before attributing
validation to it. A runtime build check alone is not a distribution license audit.

## Probe contract and limits

- Finite input up to 8 MiB, one audio stream, up to eight named native-layout
  channels, and at most 60 seconds of stereo PCM. This deliberately small
  development contract does not implement the production track probe/selection
  policy, movie indexing, seeks or lazy HLS resources.
- Custom AVIO handles short reads and bounded positional seeks; premature zero
  or oversized callback results fail. Cancellation is checked during I/O,
  decoding, resampling, packet verification and writing. RAII closes codec,
  format, frame, packet, FIFO, resampler and current AVIO-buffer owners on failure.
- Decode audio, map to stereo, resample to 48 kHz and encode AAC-LC at
  192 kbit/s. Verify encoded packets through a second AAC decoder. The probe
  reports original/converted sample counts, encoder padding, packet counts and
  pre-encoding stereo peaks/means; it logs no media paths or receiver identifiers.
- Output callbacks receive raw AAC packets, without container timestamps,
  skip metadata or initialization data. A packet cache is a probe artifact,
  **not playable HLS**. Failures may leave partial host output, which the host
  must discard. All callbacks stop before the synchronous call returns.

The explicit matrix sends FL/FR directly to their respective sides, center to
both at `1/sqrt(2)`, side/back left or right to that side at `1/sqrt(2)`, and
LFE to both at `0.5`. Divide every coefficient by the largest row sum (at least
one) for conservative correlated-signal headroom. Mono center maps equally to
both channels. Unknown layout identities are refused; channel count alone is
insufficient. This includes already band-limited LFE in the stereo mix; the
probe does not add an LFE low-pass filter, loudness normalization or DRC.
AC-3/E-AC-3 decoder `drc_scale` is explicitly zero to disable their default
metadata-driven compression.
This is the initial engineering policy, not a user requirement or listening test.

## Offline evidence and encoder-window decision

The desktop test checks each PCM 5.1/7.1 channel independently using literal
expected stereo weights, correlated high-level headroom, 44.1-to-48 kHz drain,
short reads, cancellation before I/O and during encoding, host cache failure,
malformed input and unsupported layout. Owned compressed AAC/AC-3/E-AC-3 and
DTS-core 5.1 fixtures exercise decoding and AAC roundtrip format checks. Their
[generator and provenance](../tools/audio-codec-spike/fixtures/provenance.json)
record the external development tool and hashes. Codec registration alone does
not establish DTS-HD or dependent-substream E-AC-3 support.

Two identical short PCM encoder epochs produce stable packet bytes, but each
introduces 1024 samples of encoder priming. Blind concatenation is unsuitable.
A separate twelve-window experiment uses a global 1024-sample AAC grid and
4096-sample requested windows. With up to 32768 source samples of preroll,
flushing at each requested window end produced **0.005524 RMS** difference
from a continuously encoded reference. Adding bounded lookahead (also up to
32768 samples), retaining only the requested packet range, reduced the measured
RMS to **0.00000565785** while preserving the exact global sample count.

This supports a candidate of fixed AAC-aligned encoding windows with bounded
preroll **and lookahead**, stable range retention and separate initial-priming
handling. It is not proof of every source format or waveform, perceptually
transparent joins, compressed-input random access or Apple HLS signaling.
The test uses synthetic center-channel tones at 48 kHz. Production still needs
decoder seek anchors/preroll, resampler history and media offsets, fMP4 timing/
roll/priming metadata, real segment boundaries, cold forward/backward requests
and receiver proof. Do not replace those gates with this PCM experiment.

## Reproduce

Use the README's pinned vcpkg toolchain. The standalone probe manifest installs
only FFmpeg and its build helpers; the root's optional feature remains available
for integration work with the casting dependencies. Keep each profile's installed
directory separate from baseline builds to avoid manifest pruning. The dedicated
triplets below build Release dependencies only; use the ordinary desktop/UWP
triplets and separate build directories if Debug dependencies are needed.
From a Visual Studio 2026 developer shell:

```powershell
cmake -S tools/audio-codec-spike -B build-codec -G "Visual Studio 18 2026" -A x64 `
  "-DCMAKE_TOOLCHAIN_FILE=$PWD/build-tools/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  "-DVCPKG_OVERLAY_PORTS=$PWD/vcpkg-overlays/ports" `
  "-DVCPKG_OVERLAY_TRIPLETS=$PWD/tools/audio-codec-spike/triplets" `
  "-DVCPKG_TARGET_TRIPLET=x64-windows-codec-release"
cmake --build build-codec --config Release --target sap2_codec_probe_tests
ctest --test-dir build-codec -C Release -R '^audio_codec_probe$' --output-on-failure

cmake -S tools/audio-codec-spike -B build-codec-uwp -G "Visual Studio 18 2026" -A x64 `
  "-DCMAKE_SYSTEM_NAME=WindowsStore" "-DCMAKE_SYSTEM_VERSION=10.0" `
  "-DCMAKE_TOOLCHAIN_FILE=$PWD/build-tools/vcpkg/scripts/buildsystems/vcpkg.cmake" `
  "-DVCPKG_OVERLAY_PORTS=$PWD/vcpkg-overlays/ports" `
  "-DVCPKG_OVERLAY_TRIPLETS=$PWD/tools/audio-codec-spike/triplets" `
  "-DVCPKG_TARGET_TRIPLET=x64-uwp-codec-release"
cmake --build build-codec-uwp --config Release --target sap2_codec_probe
./tools/audio-codec-spike/check_binaries.ps1 `
  -Directory build-codec-uwp/Release
```

The separate `tools/audio-codec-spike/uwp-host` project uses the existing modern
.NET UWP/Native AOT toolchain. Build using VS 2026 MSBuild and package it, then
register the extracted package layout in Developer Mode. It has its own
`SendAirPlay2.CodecSpike` identity, execution alias `sap2-codec-spike.exe`, and
no network capability. It does not replace Screenbox or the playback test host.
Pass `ProbeNativeDir` to package the matching build. It runs automatically,
writes fixed-field `LocalState/codec-probe.txt`, permits brief loaded-module
inspection and exits. Tests include synthesized PCM, compressed fixtures,
mid-encoding cancellation and output failure; each run deletes only its own
newly created cache folder after closing the streams. No visual confirmation
or Computer Use is needed for this offline runtime test.

```powershell
msbuild tools/audio-codec-spike/uwp-host/CodecSpike.csproj /restore `
  /p:Configuration=Release /p:Platform=x64 /p:GenerateAppxPackageOnBuild=true `
  /p:AppxBundle=Never /p:UapAppxPackageBuildMode=SideloadOnly `
  /p:ProbeNativeDir="$PWD/build-codec-uwp/Release" `
  /p:ProbeFfmpegShare="$PWD/build-codec-uwp/vcpkg_installed/x64-uwp-codec-release/share/ffmpeg"
```

`ProbeFfmpegShare` selects the matching installed FFmpeg notice directory;
override its local default as above when using the standalone per-build layout.
The project packages FFmpeg and vcpkg
notices. The recorded private development version is 0.1.0.1; updates must use
a higher version and preserve this test identity's data.

Binary inspection, successful package creation and runtime results are distinct
gates. AppContainer/app CRT/import checks are not WACK/Store certification.
Ship exact source/patches/configuration and notices with any redistributed probe
build; this spike does not authorize publication of a new package prerelease.

## CI failure and runtime follow-up (2026-10-12)

At PR #46 head `6a5777d`, [run 38233668327](https://github.com/ilyalissoboi/send-airplay2/actions/runs/38233668327)
took **66m24s** for the serial probe job. Desktop configure took **45m40s**,
including Botan **26 min**, OpenSSL **9.1 min** and FFmpeg **7.4 min**.
The build/codec test step took **six seconds** and passed. UWP configure then
took **20m01s**, including FFmpeg **7.3 min**, and failed because the unquoted
PowerShell `-DCMAKE_SYSTEM_VERSION=10.0` argument reached CMake as `10`.
All 14 baseline jobs passed. The cold binary cache was not saved because its
post-step was skipped after failure.

The follow-up quotes all configure arguments, uses the standalone codec-only
manifest and dedicated Release-only triplets, and runs desktop and UWP as
independent matrix jobs. Both preserve the existing offline test and AppContainer
inspection gates. Explicit cache restore/save uses run-specific keys with stable
restore prefixes and vcpkg ABI validation, preserving completed packages even if
a later step fails. It does not cache or skip the tests. The earlier packaged
runtime hash evidence describes the recorded package, not these new build profiles.

At implementation head `22f14a0`, [hosted run 38243945234](https://github.com/ilyalissoboi/send-airplay2/actions/runs/38243945234)
passed both codec jobs, including dependency-cache saves:

| Profile | Full job duration, first cache population | Result |
| --- | --- | --- |
| Desktop x64 Release | 5m10s | Codec CTest passed |
| UWP x64 Release | 3m45s | All five DLL inspections passed |

These jobs execute in parallel, reducing the codec gate's elapsed time from
66m24s to approximately **5m11s**, about **92% shorter**. This is the codec gate,
not the whole workflow; the baseline jobs have their own build/test times.
The first updated run was explicitly dispatched on the branch because no Actions
run appeared after pushing. Inspect checks for the actual PR head before merging.

Local validation also passed the standalone Release codec test and all five
AppContainer inspections, with Debug FFmpeg libraries absent in both dedicated
profiles. The root's optional build still passes its codec test. A fresh local
desktop installed directory restored all seven dependencies from the binary
cache, installing them in **622 ms** without compiling FFmpeg. That installation
duration excludes compiler detection/generation and waiting on another local
vcpkg process; it is not an end-to-end hosted-cache runtime claim.

## Remaining gates

Recorded Windows x64 gate: final-profile desktop CTest **1/1 passed**, baseline
static/shared **35/35 and 36/36 passed**, all five AppContainer/app CRT/import
checks passed, and the Native AOT/MSIX host **eight offline cases passed**.
The running probe's five module hashes match the registered package. Exact
hashes and limits are in the [validation artifact](validation/stereo-aac-codec-spike-windows-2026-10-12.json).
No Computer Use, visual confirmation or receiver playback was involved.

Production HLS conversion, explicit source-track inventory/selection, quotas,
whole-movie indexing, integration with the C API/binding/Screenbox and receiver
playback remain work in the [D67 plan](stereo-aac-casting-plan.md). DTS-HD input
and E-AC-3 dependent substreams need suitably licensed fixtures before adding
them to the supported matrix. Audio-only speakers and 5.1/7.1 output preservation
remain deferred; PCM 7.1 downmix tests do not validate discrete surround output.
