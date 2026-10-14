# Expanded private audio decoder foundation (D77)

Authorized by the user's Proceed on 2026-10-14 (Asia/Tokyo), from merged
PR #57 / main `faee858`. The [D76 plan](automatic-audio-transcoding-plan.md)
defines the broader automatic-delivery work; this increment implements its
decoder/profile foundation in the explicit stereo backend. It adds no automatic
policy, new ABI, package publication or receiver observation.
[PR #58](https://github.com/ilyalissoboi/send-airplay2/pull/58) merged as `820e89c`;
all 16 checks passed at final head `129ae6e` in run `38504706633`, and the review
thread is resolved. [D78's bounded coarse-clock path](coarse-matroska-audio.md)
extends this checkpoint for short presentations; long-file indexing remains next.

## Input configuration and identity

The optional FFmpeg 9.0.2 overlay now enables native FLAC, Opus, Vorbis, TrueHD
and signed 16/24/32-bit / float 32/64-bit PCM decoders, with their required native
parsers. No external codec library is added. Retain the four-library LGPL profile,
network/GPL/nonfree disabling, source archive/hash and notices in
[dependencies.md](dependencies.md). Default builds remain codec-free.

Original MP4 track_ID / Matroska TrackNumber selection and exact backend codec
agreement still precede decoding. There is no fallback to another audio track.
New mappings are:

| Container | Accepted source forms | Configuration checks |
|---|---|---|
| MP4 | FLAC `fLaC`, Opus `Opus` | Version-zero audio sample entry; bounded `dfLa` STREAMINFO / `dOps` channel map |
| MOV | PCM `sowt`, `twos` | Version zero, signed 16-bit |
| MOV | PCM `in24`, `in32`, `fl32`, `fl64` | Version-one packet packing, matching `wave/frma`, explicit `enda`; integer and float little/big endian |
| MKV | `A_FLAC`, `A_OPUS`, `A_VORBIS`, `A_TRUEHD` | STREAMINFO, Opus identification/mapping, bounded three-header Xiph lacing, or empty TrueHD private data respectively |
| MKV | `A_PCM/INT/LIT`, `A_PCM/INT/BIG`, `A_PCM/FLOAT/IEEE` | Declared bit depth selects exact packing: signed 16/24/32-bit or little-endian float 32/64-bit |

Matroska inventory now retains coded/output audio rates, channels and bit depth.
The effective output rate and channel metadata must agree with the selected
decoder stream. `OutputSamplingFrequency` is distinct from the coded rate and
defaults to `SamplingFrequency` when omitted
([RFC 9559 section 5.1.4.1.29.2](https://www.rfc-editor.org/rfc/rfc9559.html#section-5.1.4.1.29.2)).
This preserves the AAC/SBR output-rate contract; an explicitly nonpositive
output frequency is malformed. PCM channel count
does not establish a surround layout: only unspecified mono/stereo get their
ordinary speaker identities; unspecified multichannel layouts remain refused.
MOV version-two `lpcm`, unsigned/companded/packed disc PCM, MP4 Vorbis/TrueHD,
DTS-HD and dependent E-AC-3 substreams remain outside this validated increment.
Opus mappings zero and one are bounded; other mapping families are refused.
Known native channel layouts and existing rate/resource/format-change limits remain.

## Timing and trimming

The adapter preserves FFmpeg packet skip/discard side data for decoder consumption.
It validates the ten-byte representation and bounds each skip/discard count to
65536 source samples before sending the packet to the decoder. It does not apply
that padding again. These are bounds on the demuxer's sample-count result, not a
new native parser of raw signed Matroska BlockGroup fields.

MOV Opus can expose a complete decoded final frame while retaining a shorter
packet duration. Bound that frame by its presentation duration before resampling,
preventing encoded tail samples from leaking beyond the original audio end.

The canonical HLS clock now uses sample-accurate timestamps directly, allowing
variable block lengths and trimmed first/final frames. Existing AAC/Dolby/DTS
coarse fixed-frame behavior is retained. **At the D77 merge, expanded Matroska HLS
required a clock quantum of at most one source sample.** D78 adds bounded
start reconstruction for coarse clocks under a ten-second presentation horizon;
see its separate evidence and remaining long-file gate. Default standalone
decoding retains its reported container-clock precision; that is not a gap-free
HLS join claim.

## Owned fixtures and offline validation

[The generator](../tools/audio-codec-spike/generate_expanded_decoder_fixtures.py)
authors original video/audio, original identities, cues and padding with
sample-clock timestamps. [Provenance](../tools/audio-codec-spike/fixtures/expanded-decoder/provenance.json)
records every hash, input tone/channel/sample count, command and development
FFmpeg version/configuration. CLI libopus/libvorbis encode the owned 5.1 fixtures
only; they are not adopted or distributed runtime dependencies.

There are 34 owned fixtures. Twenty-eight positive format/container variants
cover stereo and FLAC/Opus/Vorbis/TrueHD 5.1 inputs. Decoder checks retain the
literal 288144-sample audible length at 48 kHz, independent lossless tone/channel
samples and spectral channel identities for lossy inputs, repeated forward/backward
windows, cold 144-sample tails and EOF reuse.
Other cases cover positive Opus final-block padding, negative PCM/Opus leading
padding with delay applied once, unsupported PCM depth, malformed configuration,
65537-sample trim refusal and the coarse-clock preparation gate.
The review regression uses owned AAC core data with explicit synthetic HE/SBR
signaling and distinct 24/48-kHz container rates. It checks inventory, output
decoding/channel tones and an invalid zero output-frequency value; it adds no
full HE-AAC profile or receiver validation claim.

Seventeen representative private HLS cases request the last AAC window cold,
then the first, decode the combined fMP4 with the independent MOV/AAC oracle,
and check the literal 312000-sample video presentation end, signal, retained
sealed bytes and cache cleanup. The shorter source audio ends at 288144 samples;
silence extends to the owned video's end. Local audio-resource exhaustion is
not receiver natural EOF.

The generator retains actual TrueHD sync markers for cues. Its native Vorbis
stereo fixture adds an independently authored 48-sample final discard to undo
the development encoder's rounded Ogg granule; TrueHD's own tail is not trimmed
a second time. These fixture corrections are not changes to a receiver policy.

Run the README's static/shared CMake/CTest checks and the standalone codec
configuration in [the spike runbook](stereo-aac-codec-spike.md). Desktop codec
CTest now includes `expanded_audio_decoder` and `expanded_audio_hls`; CI builds
both explicitly. The x64 UWP decoder/core compilation and codec import checks
must use the same expanded profile. Actual PR-head CI remains the merge gate.

Recorded local gates: baseline static/shared 36/36 and 37/37, enabled shared
38/38, standalone codec checks 6/6, and x64 UWP compilation/import inspection.
Recheck changed enabled targets after any follow-up. These results are offline,
not an expanded AppContainer packaged runtime or receiver compatibility claim.

## Remaining work

Complete bounded long-file Matroska variable-frame indexing and seek/phase tests;
retain D78's short-presentation horizon until that scaling gate passes.
Then implement selected-audio remux and deterministic copy/convert policy,
followed by versioned public reporting. Additional layouts/rates, real-file
performance, expanded packaged-runtime checks and recorded converted-receiver
checks remain required before release. Screenbox remains on `0.3.0-ci.207`.
Speaker transport and discrete converted 5.1/7.1 output remain hardware-deferred.
