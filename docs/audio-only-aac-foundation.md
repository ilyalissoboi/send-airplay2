# D100: private audio-only stereo AAC foundation

2026-10-18 (Asia/Tokyo). User-authorized next slice after native #84 merged as
`b9af0119ef06399640cef5b4f6f9f281aba0fa00`. All 17 checks passed at its final
head `64c3595` in [run 39124033437](https://github.com/ilyalissoboi/send-airplay2/actions/runs/39124033437).
D99's completed Mac layout/queue gate remains separate historical evidence.

## Implemented boundary

D105 later extends this private foundation with checked finite MP4 edits and
indexed Matroska FLAC/PCM audible ends. See [the current input subset and offline
evidence](audio-only-container-timelines.md); D100's original unedited scope below
remains historical. Public automatic routing still requires video.

The existing conversion owner borrowed video duration, segment cuts, resources
and master-playlist shape. A private `StereoAacHls::open_audio_only` now uses
its own selected audio packet clock with the same decoder, 48-kHz stereo AAC
encoder, lazy worker, bounded host-owned cache and Stop/cleanup ownership.
This is an internal foundation for the requested automatic audio-file feature;
public `PreparedAutomaticMedia` routing still uses the existing video path.
There is no new C ABI, CLI/binding switch, package, host or Screenbox change.

Admission is deliberately limited to the decoder's covered FLAC/PCM subset in
progressive MP4/MOV, no edit list, zero-based contiguous positive audio packet
durations, and no video track (including disabled video or artwork carried as
video). A selected enabled track is required: ambiguous enabled MP4 audio
requires an explicit source ID, and invalid IDs never fall back. The source
inventory and decoder still enforce existing format/layout/protection bounds.
The factory refuses AAC/Dolby/DTS/Opus/Vorbis/TrueHD for this slice even where
other decoder routes support them; their audio-only trims/clocks are separate
admission work. This does not claim those formats are unsupported by Apple TV.

`AudioDecoderSource::unedited_mp4_duration_samples` reads the native selected
packet index under the existing operation byte/callback/deadline limits.
It refuses edits and non-MP4 input, checks the seven-day bound before scaling,
and rounds the total once to 48 kHz using whole seconds plus a remainder to
avoid overflow at large track timescales. It does not trust a host duration or
FFmpeg's format-duration estimate. Existing six-second-grid/AAC-frame planning
splits long gaps, keeps spans within seven seconds and preserves an exact final
output-sample boundary. Existing admission/sealed-byte quota limits still apply.

The output contains an AAC init, lazy audio fragments, a finite audio media
playlist and a master pointing directly to that playlist. It declares AAC only,
with no fabricated resolution/frame rate/HDR attributes, video resource or
alternate-audio group. It omits the independence claim because cold AAC synthesis
uses overlap history. Video composition retains its previous behavior. This
playlist design follows [RFC 8216 sections 4.3.4.2 and 4.3.5.1](https://www.rfc-editor.org/rfc/rfc8216#section-4.3.4.2);
[Apple's HLS authoring specification](https://developer.apple.com/documentation/http-live-streaming/hls-authoring-specification-for-apple-devices)
remains an authoring reference, not proof of receiver interoperability.

## Validation and provenance

| Check | Recorded scope |
| --- | --- |
| Native static/shared CMake and CTest | 40/40 and 41/41 pass |
| Optional desktop codec CMake and CTest | 15/15 pass, including new audio-only output tests |
| Private UWP codec library | Compiles with the existing AppContainer toolchain; no new package/runtime gate |
| C++ style | Touched files formatted; dry-run/Werror and diff check required before PR |
| Receivers and Screenbox | No new cast, application installation or visual claim |

Owned fixtures contain original repository-generated tones only: 48-kHz stereo
FLAC (13.003 seconds), 44.1-kHz stereo FLAC (573432 source samples), 48-kHz 5.1
PCM (3.003 seconds), plus edited PCM, unsupported-for-this-slice AAC and ambiguous
FLAC negatives. `generate_audio_only_fixtures.py` uses the existing owned tone
helper and external development FFmpeg CLI. The ambiguous fixture explicitly
authors both MP4 track-enabled flags; the initial muxer-disabled second track
was not an ambiguous input and was corrected before the passing test run.
Hashes, sizes, source tone counts/frequencies and CLI version are recorded in
[fixture provenance](../tools/audio-codec-spike/fixtures/audio-only/provenance.json).
No private media, third-party implementation or new linked dependency is added;
Apache-2.0 and the existing pinned, network-disabled LGPL codec profile remain.

The independent MOV/AAC oracle decodes concatenated output to literal expected
48-kHz stereo frame counts, including the fractional last frame. Tests request
the last fragment first, then earlier fragments, and repeat the cached final
read; bytes remain stable with one seal per fragment. Startup calibration seals
nothing. Tests also cover direct audio-only master shape, missing video in the
legacy video route, selection refusal/explicit selection, cancellation, quota
admission, terminal seal failure, exact-once cleanup and inert resources after
Stop. These are offline checks, not a listening or sustained long-file gate.
See [the machine-readable checkpoint](validation/audio-only-aac-foundation-windows-2026-10-18.json).

## Ordered follow-ups

1. Validate additional input timelines and standalone containers. Add native
   FLAC and WAV/RF64 inventory/duration/seek rules first, then Ogg Opus/Vorbis
   stream identity, granule trim, restart and chaining rules. Edited MP4 and
   audio-only Matroska require their own validated audible-end calculation.
   Demuxers outside the currently pinned MOV/Matroska backend require an explicit
   codec-profile/package rebuild and source/license record. Decoder availability
   inside a video container is not proof of standalone file support. Include the
   MP3 work track below as explicitly requested by the user.
2. Connect automatic delivery to an explicit audio-only profile: copy only a
   validated receiver-delivery subset, otherwise convert covered streams when
   stereo permission is enabled. Preserve-original, absent backend, unknown
   codec/container, protected input and resource failure must refuse clearly.
   Use stream/container metadata rather than extensions alone; this is a finite
   library profile, not a query of all Apple TV native capabilities.
3. Add public reporting/CLI/binding/offline checks, then native Mac/Apple TV
   audio-only HLS listening, start-position, controls, natural-end, Stop and
   cancellation gates. Earlier progressive AAC/M4A success does not prove this
   HLS composition or its generated bytes.
4. Repeat brokered-source/lifetime/storage gates in a packaged UWP host. Only
   then extend Screenbox routing/settings/queue behavior and provide written
   manual instructions. Preserve current opt-in stereo permission and defaults.

### MP3 work track added by the user (2026-10-18)

**Current checkpoint (D104, 2026-10-19):** private MP3 framing/timeline inventory
and native/packaged progressive CBR/VBR gates pass on the recorded Mac/Apple TV.
The direct-delivery decision is complete for those stereo fixtures: no AAC
fallback is demonstrated. Public/default admission and Screenbox routing remain
separate; see [the scoped packaged record](mp3-uwp-gate.md). The D100-era planning
text below is historical where this checkpoint supersedes its pending status.

MP3 is now explicitly in the work plan. This adds planning scope only: D100's
private factory remains FLAC/PCM-only, with no MP3 implementation, backend
profile change or receiver observation.

1. **Input support and offline evidence.** Define bounded MP3 stream inventory,
   frame validation, duration and seek coordinates, including CBR/VBR, mono/stereo,
   sample-rate admission, leading/trailing metadata and gapless trim metadata.
   Use owned tone fixtures with independently recorded frame counts and seek
   markers. Validate short reads, cancellation/deadlines, malformed/truncated
   frames and metadata, changing stream parameters and long-file resource bounds.
   Inspect the pinned backend and record source/license provenance before any
   required demuxer/decoder/profile expansion; file extension alone is not proof
   of stream eligibility.
2. **Direct playback first.** Probe progressive MP3 delivery with the appropriate
   explicit MIME on both recorded MacBook Pro and Apple TV, using owned CBR/VBR
   fixtures. Record receiver/OS and exact binary bytes. Verify audible start and
   nonzero starting position, duration/end, pause/resume, forward/backward seek,
   separate Stop/no delayed restart and applicable consent cancellation. Keep
   native, brokered packaged UWP and later Screenbox queue gates separate; use
   written user observations. Current AAC/M4A or audio-in-video evidence does not
   establish MP3 playback. Audio-only speakers remain outside these observations.
3. **Choose delivery from evidence.** If validated direct delivery covers an MP3
   subset, keep that subset on a copy/direct path without requiring conversion
   permission. Decide whether an AAC fallback is needed from the receiver results
   and document its trigger and admitted stream subset. Add conversion only when
   justified, with explicit stereo permission, validated decoder/timeline support,
   the existing bounded cache/cleanup contract and independent AAC-output checks.
   Unknown or unvalidated streams must not silently become eligible. A fallback
   decision remains pending; this plan does not presume MP3 requires transcoding.

HomePod/RAOP speakers, discrete converted surround, Chromecast hardware checks,
user-deferred remote-Stop classification and separate long-input/cache-quota
work remain deferred. The existing quota is unchanged. No release is published.
