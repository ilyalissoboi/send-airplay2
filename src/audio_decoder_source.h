// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_AUDIO_DECODER_SOURCE_H
#define SEND_AIRPLAY2_AUDIO_DECODER_SOURCE_H
#include "send_airplay2/media_server.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <vector>

namespace send_airplay2::detail {
enum class AudioDecodeFailure {
    invalid_selection,
    selection_required,
    unsupported,
    malformed,
    too_large,
    stopped,
    source_error,
    codec_error
};
class AudioDecodeException : public std::runtime_error {
public:
    AudioDecodeException(AudioDecodeFailure reason, const char* message)
        : std::runtime_error(message), reason_(reason) {}
    [[nodiscard]] AudioDecodeFailure reason() const noexcept {
        return reason_;
    }

private:
    AudioDecodeFailure reason_;
};
/// Refuse a backend outside the pinned four-library, network-disabled LGPL profile.
void validate_audio_codec_backend();

/// Owned interleaved float PCM, preserving the selected source channel layout.
/// first_sample and frame count use the edited, audible presentation clock at 48 kHz.
/// A coarse container clock limits seek alignment by timestamp_precision_samples.
struct DecodedAudioWindow {
    std::uint64_t source_track_id = 0;
    std::uint64_t first_sample = 0;
    std::uint32_t channels = 0;
    std::uint64_t channel_mask = 0;
    std::uint32_t timestamp_precision_samples = 0;
    bool eof = false;
    std::vector<float> samples;
};

/** Optional private codec adapter, not a playback ABI or casting delivery mode.
 * Retains copied host callbacks and borrows their external storage/context for its
 * lifetime. The source must remain immutable. No native file/network access.
 * Calls on one object must be serialized; callbacks must not reenter it.
 * Open calls the host size once, probes unfiltered native identities, selects before
 * codec filtering, and matches original IDs in the decoder (never names/ordinals).
 * Each operation borrows its absolute deadline/cancellation context until return.
 * No background work/callback after return. Callback exceptions propagate outside
 * FFmpeg's C callbacks; parser errors retain RemuxException categories.
 * Bounds: 1 TiB source, 128 streams, 256 MiB requested bytes/1,048,576 callbacks per
 * decoder operation, windows <=10 s in a <=7-day presentation, <=20 s input PCM
 * per window, channels 1..8. Resampling phase periods above 8192 input samples
 * are refused. Both cancellation pointers in MediaReadContext must be valid.
 * A failed window invalidates the object; discard it and any tentative output.
 * Standalone FLAC/WAVE/Ogg requires allow_standalone_audio=true and native header
 * validation; the default source probe remains MP4/Matroska-only.
 * Known native layouts and the documented AAC/Dolby/DTS-core/FLAC/Opus/Vorbis/
 * PCM/TrueHD source matrix only. Layout/rate/format changes,
 * discontinuities, protected/unknown inputs and complex edits are refused.
 * canonical_matroska_clock indexes validated unlaced PCM/FLAC/Opus/Vorbis/TrueHD
 * using exact packed counts or bounded pinned-parser duration/restart analysis,
 * with <=65536 sparse restart anchors (at least one source second apart).
 * Open scans headers (complete bounded Opus packets) without a full packet table;
 * windows binary-search anchors and reconstruct nearby exact clocks with fresh
 * parser/decoder state. Two bounded header pages share the I/O budgets. Compressed
 * packets are <=1 MiB. TrueHD requires one access unit per block and an initial
 * major sync; seeks start at the preceding major sync. Vorbis first delay/skip
 * cannot exceed intrinsic overlap-frame priming; smaller/absent delay leaves
 * corresponding initial presentation silence.
 * Raw packet clocks apply CodecDelay once and decoder-owned skip/discard once;
 * the source window budget includes both header and payload reads. The source
 * clock must be no coarser than one second before exact-clock substitution.
 * The initial absolute origin remains container-limited; epoch phase does not
 * change with frame size or seek order. Sparse coordinates remove the decoder's
 * old 262144-packet cap, not its byte/callback/deadline/input bounds. Long restart
 * gaps or large source scans can still fail those unchanged resource budgets.
 * Legacy AAC/Dolby/DTS coarser clocks require a fixed-frame grid for HLS:
 * first read must start at zero; frames must remain constant and exceed twice
 * the clock quantum. Rounded timestamps may snap only within one tick of that grid.
 * This stabilizes relative seek phase; initial absolute clock precision remains
 * container-limited. Default decoding retains its original clock policy.
 * Canonical DTS lace continuation frames count decoded samples within one
 * known native block position (at most 256 frames). Each new block keeps the
 * original timestamp/grid/continuity checks; unknown positions stay on the
 * strict generic clock. This avoids double rounding of demuxer-derived lace PTS.
 * Canonical DTS can use a unique video's already parsed, bracketing keyframe
 * index as a container seek coordinate when it precedes the target more closely
 * than the selected audio index. Audio identity/discard settings never change.
 * The first decoded frame must cover the requested preroll within the reported
 * clock precision; a late landing refuses rather than synthesizing silence.
 * Missing, ambiguous or uncovered coordinates keep the original bounded seek.
 */
class AudioDecoderSource {
public:
    struct IndexStatistics {
        std::uint64_t packets;
        std::size_t anchors;
        std::size_t retained_bytes;
    };
    /// Offline scaling diagnostics; excludes backend owners and fixed read pages.
    [[nodiscard]] IndexStatistics index_statistics() const noexcept;
    static constexpr std::uint32_t output_rate = 48000;
    static constexpr std::uint64_t maximum_start_rebuild_samples = 10ULL * output_rate;
    /// True when canonical decoding must rebuild from the beginning. The HLS
    /// owner must refuse longer presentations before registering resources.
    [[nodiscard]] bool requires_start_rebuild() const noexcept;
    [[nodiscard]] static std::unique_ptr<AudioDecoderSource>
    open(const MediaSource& source, const MediaReadContext& context,
         std::optional<std::uint64_t> explicit_track_id = {}, bool canonical_matroska_clock = false,
         bool allow_standalone_audio = false);
    /** Selected audio-only duration on the 48-kHz clock. Standalone FLAC/WAVE/Ogg
     * uses its native probe metadata; MP4/MOV uses the unedited packet index.
     * Reads the native packet index under the caller's operation budgets; no PCM
     * decoding or FFmpeg duration estimate. Requires zero-based contiguous samples,
     * no edit list, positive duration <=7 days. Standalone duration is <7 days.
     * Matroska and edited inputs refuse; FLAC end/coverage needs decoding checks.
     * Serialized like read_window; callbacks end on return and exceptions propagate.
     */
    [[nodiscard]] std::uint64_t audio_only_duration_samples(const MediaReadContext& context);
    ~AudioDecoderSource();
    AudioDecoderSource(const AudioDecoderSource&) = delete;
    AudioDecoderSource& operator=(const AudioDecoderSource&) = delete;
    AudioDecoderSource(AudioDecoderSource&&) = delete;
    AudioDecoderSource& operator=(AudioDecoderSource&&) = delete;

    /** Seek with at least one second of decoder/resampler preroll, flush both,
     * discard decoded history and return up to count presentation samples.
     * Initial edit silence is zero-filled; samples after actual EOF are omitted.
     * FFmpeg MOV edits and non-indexed Matroska CodecDelay are already in frame timestamps:
     * never apply the raw-container planners' corrections a second time.
     * Packet skip/discard side data remains intact for decoder consumption.
     * Indexed audio constructs its packet clock/trim from native container fields,
     * avoiding generic demuxer overwrite of first-block skip with CodecDelay.
     * MOV Opus frame duration bounds the audible packet tail before resampling.
     * This does not encode AAC or establish independent HLS segment priming.
     */
    [[nodiscard]] DecodedAudioWindow read_window(std::uint64_t first_sample, std::uint32_t count,
                                                 const MediaReadContext& context);

private:
    struct Impl;
    explicit AudioDecoderSource(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
} // namespace send_airplay2::detail
#endif
