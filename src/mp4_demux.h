// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_MP4_DEMUX_H
#define SEND_AIRPLAY2_MP4_DEMUX_H
#include "text_tracks.h"
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

namespace send_airplay2::detail {
using Bytes = std::vector<std::uint8_t>;

/// Why an input cannot be remuxed. Messages never contain paths or media data.
enum class RemuxFailure {
    malformed,   ///< The container structure is invalid or inconsistent.
    unsupported, ///< Valid, but not remuxable (codec, fragmented input, layout).
    too_large    ///< Beyond a fixed resource bound (moov size, sample count).
};

/// Fixed short description of a failure category, for diagnostics.
[[nodiscard]] const char* remux_failure_name(RemuxFailure reason) noexcept;

class RemuxException : public std::runtime_error {
public:
    RemuxException(RemuxFailure reason, const std::string& message)
        : std::runtime_error(message), reason_(reason) {}
    [[nodiscard]] RemuxFailure reason() const noexcept {
        return reason_;
    }

private:
    RemuxFailure reason_;
};

/// Reads exactly `length` bytes at `offset` or throws. Used only while the
/// sample tables are built; sample payloads are never read by the demuxer.
using RandomReader =
    std::function<void(std::uint64_t offset, std::uint8_t* output, std::size_t length)>;

enum class TrackKind { video, audio };

/// One sample, in its track's timescale. offset/size locate its bytes in the
/// source file; decode_time is the sum of the preceding stts durations.
struct Mp4Sample {
    std::uint64_t offset = 0;
    std::uint64_t decode_time = 0;
    std::uint32_t size = 0;
    std::uint32_t duration = 0;
    std::int32_t composition_offset = 0; ///< Presentation minus decode time.
    bool sync = false;
};

/// One elst entry. segment_duration is in the movie timescale, media_time in
/// the track timescale; media_time -1 is an empty edit.
struct Mp4Edit {
    std::uint64_t segment_duration = 0;
    std::int64_t media_time = 0;
    std::int16_t rate_integer = 1;
    std::int16_t rate_fraction = 0;
};

struct Mp4Track {
    TrackKind kind = TrackKind::video;
    std::uint32_t timescale = 0;
    std::uint16_t language = 0; ///< mdhd packed ISO-639-2/T code.
    std::string codec;          ///< Sample entry four-character code, e.g. "avc1".
    Bytes sample_entry;         ///< The complete stsd entry box, copied verbatim.
    Bytes track_header;         ///< The complete source tkhd box.
    std::vector<Mp4Edit> edits;
    std::vector<Mp4Sample> samples;
};

/// The tracks selected for remuxing: the first video track and, if present,
/// the first audio track, in that order.
struct Mp4Movie {
    std::uint32_t timescale = 0; ///< mvhd timescale; elst durations use it.
    std::vector<Mp4Track> tracks;
    /// Text subtitles served as WebVTT renditions (phase 3c); MKV input only.
    std::vector<TextTrack> text_tracks;
};

/// Video sample entries the remux writes (Apple HLS 1.1, 1.10).
[[nodiscard]] bool remuxable_video_codec(const std::string& codec);
/// Audio sample entries the remux writes: AAC in mp4a, AC-3 and E-AC-3.
[[nodiscard]] bool remuxable_audio_codec(const std::string& codec);

/** Read the sample tables of a progressive (non-fragmented) MP4/MOV file of
 * `file_size` bytes through `read`.
 *
 * Text subtitle tracks (handler sbtl, text or subt) with a 3GPP Timed Text
 * ('tx3g', the mov_text of ffmpeg and HandBrake) or ISO/IEC 14496-30 WebVTT
 * ('wvtt') sample entry become Mp4Movie::text_tracks (tx3g bold, italic and
 * underline style runs become WebVTT tags): their samples are read
 * (at most 64 KiB each, 16 MiB per track, else the track is left out), timed
 * on the presentation timeline by the track's edit list (an initial empty edit
 * and one media edit), with the language from mdhd, DEFAULT from the tkhd
 * enabled flag and FORCED from the tx3g "all samples are forced" display
 * flag. Other text formats are left out, never refused.
 *
 * Selects the first video track with an H.264 or HEVC sample entry and the
 * first audio track with an AAC, AC-3 or E-AC-3 entry. Other tracks (more
 * audio, subtitles, timed metadata) are ignored. A video track whose codec is
 * not remuxable, a selected-kind audio track with only unremuxable codecs,
 * encrypted entries, more than one sample description, fragmented input or a
 * file without video is `unsupported`. Sample tables that disagree, samples
 * outside the file or a missing box are `malformed`. moov above 64 MiB or more
 * than 4,000,000 samples per track is `too_large`.
 *
 * @throws RemuxException as above; exceptions from `read` propagate.
 */
[[nodiscard]] Mp4Movie read_mp4(const RandomReader& read, std::uint64_t file_size);
} // namespace send_airplay2::detail
#endif
