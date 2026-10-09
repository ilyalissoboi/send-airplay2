// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_MKV_DEMUX_H
#define SEND_AIRPLAY2_MKV_DEMUX_H
#include "mp4_demux.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace send_airplay2::detail {
/// Whether `data` starts with the EBML header ID (Matroska and WebM files).
[[nodiscard]] bool starts_like_matroska(const std::uint8_t* data, std::size_t size);

/** Read the sample tables of a Matroska file (RFC 9559, EBML RFC 8794) of
 * `file_size` bytes through `read`, as the remux's track model (D60).
 *
 * Selects the first enabled video track with V_MPEG4/ISO/AVC or
 * V_MPEGH/ISO/HEVC, and the audio track with A_AAC, A_AC3 or A_EAC3, taking a
 * default-flagged one before the first enabled one. Sample entries, track
 * headers and languages are built from the track's CodecPrivate, video size
 * and first audio frame (sample_entries.h). Other tracks are ignored.
 *
 * Every cluster's block headers are read; frame payloads are not, except the
 * first AC-3/E-AC-3 frame. Video decode times are the sorted presentation
 * times (Matroska stores no decode times); presentation is delayed by the
 * reorder delay so composition offsets stay non-negative, and an edit list
 * removes that delay. The video timescale is 10^9 / TimestampScale. Audio
 * uses its sample rate as the timescale and one codec frame per sample
 * (1024 or 960 for AAC, 1536 for AC-3, 256 x blocks for E-AC-3), following a
 * block's timestamp where it differs from that timeline by more than the
 * TimestampScale rounding (half a tick) plus one sample; a CodecDelay
 * becomes an edit list that skips the priming samples.
 *
 * Refusals (RemuxException): `unsupported` for other codecs (an audio track
 * that cannot be remuxed refuses the file rather than casting it silent),
 * ContentEncodings (compression or encryption), laced video, negative
 * timestamps, a TimestampScale that does not divide 10^9, or E-AC-3 with
 * dependent substreams; `malformed` for invalid EBML, a missing codec
 * configuration or frames outside their blocks; `too_large` beyond 16 MiB of
 * track metadata or 4,000,000 frames per track.
 */
[[nodiscard]] Mp4Movie read_mkv(const RandomReader& read, std::uint64_t file_size);

/** A Matroska file opened from its Cues index (D60), so start-up reads the
 * track metadata, the index and two segments instead of every block header.
 *
 * Segments start at the video track's cue points, on the same 6-second grid
 * as plan_segments(). The first segment fixes the sample entries, the video
 * reorder delay (its edit list) and the audio frame grid; the last one fixes
 * the end time. Each read_segment() scans only the clusters around one
 * segment, from the last cue point at least 2 s before it to the first
 * cluster 2 s past it, so it is bounded by the segment's size, not the
 * file's.
 *
 * Per segment, the video decode times are the sorted presentation times
 * moved to start at the segment's cue time, so consecutive segments' decode
 * timelines join exactly (also for open GOPs); presentation times are the
 * Matroska timestamps, as with read_mkv(). Audio frames belong to the segment
 * whose window holds their block timestamp and stay on one frame grid across
 * segments. Text tracks keep their metadata here; their cues come per
 * segment.
 *
 * Thread-safe: read_segment() is const and reads through the given reader.
 */
class MkvIndex {
public:
    /** Null when the file has no Cues for its video track, the cues would
     * make a segment longer than 20 s, or the first segment has no audio
     * frames while the file has an audio track: read_mkv() then reads the
     * whole file. Refusals are as for read_mkv().
     * @throws RemuxException; exceptions from `read` propagate. */
    [[nodiscard]] static std::unique_ptr<MkvIndex> open(const RandomReader& read,
                                                        std::uint64_t file_size);
    ~MkvIndex();
    MkvIndex(const MkvIndex&) = delete;
    MkvIndex& operator=(const MkvIndex&) = delete;
    MkvIndex(MkvIndex&&) = delete;
    MkvIndex& operator=(MkvIndex&&) = delete;

    /// Tracks with sample entries, headers and edit lists but no samples, and
    /// text tracks with metadata but no cues.
    [[nodiscard]] const Mp4Movie& metadata() const;
    [[nodiscard]] std::size_t segment_count() const;
    /// Presentation start of a segment (its cue time) and its duration, in
    /// microseconds; the last segment ends with the last video frame.
    [[nodiscard]] std::uint64_t segment_start_us(std::size_t segment) const;
    [[nodiscard]] std::uint64_t segment_duration_us(std::size_t segment) const;
    /// File bytes between this segment's cue cluster and the next one's: an
    /// estimate for advertised bandwidth before a segment is read.
    [[nodiscard]] std::uint64_t estimated_segment_bytes(std::size_t segment) const;
    /// Video frames per 1000 s in the first segment, for FRAME-RATE.
    [[nodiscard]] std::uint64_t frame_rate_milli() const;

    /// One segment's samples per media track (metadata() order) and the text
    /// cues per text track that start in its window.
    struct Segment {
        std::vector<std::vector<Mp4Sample>> samples;
        std::vector<std::vector<TextCue>> cues;
    };
    /** @throws RemuxException for blocks that contradict the index;
     *          std::out_of_range for a segment past segment_count(). */
    [[nodiscard]] Segment read_segment(std::size_t segment, const RandomReader& read) const;

private:
    struct Impl;
    explicit MkvIndex(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};
} // namespace send_airplay2::detail
#endif
