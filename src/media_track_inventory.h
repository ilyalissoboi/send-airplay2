// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_MEDIA_TRACK_INVENTORY_H
#define SEND_AIRPLAY2_MEDIA_TRACK_INVENTORY_H
#include "mp4_demux.h"
#include "send_airplay2/media_server.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace send_airplay2::detail {
enum class SourceContainer { mp4, matroska, wave, flac, ogg };
enum class SourceTrackKind { video, audio, other };

/// Container identity and metadata, before decoder/remux capability filtering.
struct SourceTrack {
    std::uint64_t id =
        0; ///< MP4 tkhd ID, Matroska TrackNumber or standalone identity 1; never an ordinal.
    SourceTrackKind kind = SourceTrackKind::other;
    bool enabled = true;
    bool standard_timeline = true; ///< False for Matroska TrackTimestampScale/TrackOffset changes.
    bool default_track = false;    ///< Only Matroska defines the default-selection flag here.
    bool encoded = false;          ///< MP4 encrypted entry or Matroska ContentEncodings.
    std::string codec; ///< Original fourcc/CodecID or standalone name; no support guarantee.
    std::string language;
    std::string name;    ///< Available container label, without inferred role or commentary status.
    Bytes configuration; ///< MP4 sample entry, Matroska CodecPrivate or Ogg identification packet.
    std::uint32_t sample_description_count = 0; ///< MP4 stsd count; zero when absent or Matroska.
    std::uint32_t timescale = 0; ///< MP4 mdhd or standalone sample clock; zero for Matroska.
    std::vector<Mp4Edit> edits;  ///< MP4 media offsets retained in their original timebases.
    std::uint64_t codec_delay_ns = 0;
    std::uint64_t seek_preroll_ns = 0;
    std::uint64_t default_duration_ns = 0;
    double audio_sample_rate = 0; ///< Matroska/standalone metadata; not inferred from a codec name.
    double audio_output_sample_rate = 0; ///< Matroska output rate; zero means SamplingFrequency.
    std::uint64_t audio_channels = 0;    ///< Original declared channel count, not a layout.
    std::uint64_t standalone_sample_frames = 0; ///< Validated standalone duration in source frames.
    std::uint64_t audio_channel_mask =
        0;                             ///< Explicit/default standalone speaker map; zero elsewhere.
    std::uint64_t audio_bit_depth = 0; ///< Required to identify Matroska PCM packing.
};

inline constexpr std::size_t max_source_tracks = 128;
inline constexpr std::size_t max_source_metadata_elements = 4096;
struct MediaTrackInventory {
    SourceContainer container = SourceContainer::mp4;
    std::uint32_t movie_timescale = 0;    ///< MP4 edit durations use this clock.
    std::uint64_t timestamp_scale_ns = 0; ///< Matroska nanoseconds per container tick.
    /// Matroska offsets are locator metadata, not validated decoder entry points.
    std::optional<std::uint64_t> first_cluster_offset;
    std::optional<std::uint64_t> cues_offset;
    std::vector<SourceTrack> tracks; ///< Includes disabled, unsupported and non-audio identities.
};

enum class AudioSelectionStatus { selected, video_only, selection_required, invalid_selection };
struct AudioSelection {
    AudioSelectionStatus status = AudioSelectionStatus::video_only;
    std::size_t track_index =
        0; ///< Valid only when status is selected, in the unfiltered inventory.
};

/** Select before checking codec support. Explicit IDs must name enabled audio;
 * absent/disabled/non-audio IDs never fall back. Automatic MP4 selection requires
 * sole enabled audio; Matroska also accepts a unique enabled default track.
 * No enabled audio gives video_only. Selected unsupported/encoded tracks stay
 * selected: the later backend must refuse them rather than choose another track.
 * @throws RemuxException for zero/duplicate identities or more than 128 tracks.
 */
[[nodiscard]] AudioSelection select_source_audio(const MediaTrackInventory& inventory,
                                                 std::optional<std::uint64_t> explicit_id = {});

/** Synchronous borrowed-source metadata probe. Calls size once, reads in chunks
 * <=64 KiB and handles short reads under the caller's absolute deadline/cancel
 * context. It retains no callbacks and does not decode frames or open files.
 * Bounds: 128 tracks, existing MP4/MKV metadata limits, 128 MiB total requested
 * source bytes and 1,048,576 host read callbacks. Matroska's 16-KiB read window
 * may fetch adjacent payload bytes without parsing frames.
 * @throws RemuxException for malformed/unsupported/oversized metadata;
 * std::invalid_argument for missing callbacks; std::runtime_error for cancelled,
 * expired or invalid reads. Source exceptions propagate; callbacks end on return.
 */
[[nodiscard]] MediaTrackInventory probe_media_tracks(const MediaSource& input,
                                                     const MediaReadContext& context);

/// Validate source identities without changing legacy remux parsing behavior.
void validate_source_tracks(const MediaTrackInventory& inventory);

struct Mp4AudioSeek {
    std::size_t sample_index = 0;           ///< samples.size() means past the indexed media end.
    std::uint64_t discard_before_ticks = 0; ///< Media clock target, rounded down to whole ticks.
    bool initial_silence = false; ///< Requested presentation time lies in an initial empty edit.
};
/// Validate once before seek planning; read_mp4_source_audio() calls this.
/// Requires nonempty audio, positive durations and contiguous increasing decode times.
void validate_mp4_audio_index(const Mp4Track& audio);
/** Translate presentation nanoseconds through zero or one unit-rate media edit,
 * optionally preceded by one empty edit, and choose the packet at/before the
 * target minus caller-provided decoder preroll (in track ticks). Preserves packet
 * offsets and durations. Requires an index from read_mp4_source_audio() or checked
 * by validate_mp4_audio_index(); packet lookup is logarithmic. Refuses complex edits instead
 * of guessing. A packet anchor is not proof of decoder/resampler seek accuracy.
 * @throws RemuxException for unsupported timeline or arithmetic overflow;
 * std::invalid_argument for a non-audio/empty sample index.
 */
[[nodiscard]] Mp4AudioSeek plan_mp4_audio_seek(const Mp4Track& audio, std::uint32_t movie_timescale,
                                               std::uint64_t presentation_ns,
                                               std::uint64_t decoder_preroll_ticks);

struct MkvAudioSeekTime {
    std::uint64_t seek_timestamp_ns = 0;
    std::uint64_t discard_before_timestamp_ns = 0;
};
/** Matroska stored timestamps include CodecDelay. Add it to the requested audible
 * presentation time, then subtract max(SeekPreRoll, decoder_preroll_ns), saturating
 * at zero. Retains nanosecond precision; choosing/validating a cue/cluster and
 * decoder/resampler history are later backend responsibilities. These are raw
 * container clocks; never apply CodecDelay again to already-adjusted decoder PTS.
 */
[[nodiscard]] MkvAudioSeekTime plan_mkv_audio_seek_time(const SourceTrack& audio,
                                                        std::uint64_t presentation_ns,
                                                        std::uint64_t decoder_preroll_ns);
} // namespace send_airplay2::detail
#endif
