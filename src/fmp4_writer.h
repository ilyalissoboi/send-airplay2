// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_FMP4_WRITER_H
#define SEND_AIRPLAY2_FMP4_WRITER_H
#include "mp4_demux.h"
#include <cstddef>
#include <cstdint>
#include <vector>

namespace send_airplay2::detail {
/// Samples [first, end) of one movie track that a media segment carries.
struct SampleRange {
    std::size_t first = 0;
    std::size_t end = 0;
    [[nodiscard]] std::size_t count() const {
        return end - first;
    }
};

/// One fMP4 media segment: a range per movie track (same order as
/// Mp4Movie::tracks; an empty range omits that track's traf) and its
/// one-based moof sequence number.
struct SegmentLayout {
    std::uint32_t sequence = 1;
    std::vector<SampleRange> ranges;
};

/** fMP4 initialization segment (ISO/IEC 14496-12 8.8 and HLS EXT-X-MAP): ftyp,
 * then moov with one trak per movie track (track IDs 1, 2, ... in order) and
 * mvex. Each trak copies the source sample entry byte for byte, and its edit
 * list with the final media edit's duration set to zero ("to the end", as
 * 14496-12 allows for fragmented files), so the presentation timeline
 * matches the source. All durations are zero; segments carry the samples. */
[[nodiscard]] Bytes write_init_segment(const Mp4Movie& movie);

/** Size of the moof box of `segment`, computed from its sample counts only. */
[[nodiscard]] std::uint64_t moof_size(const Mp4Movie& movie, const SegmentLayout& segment);

/** Size of the mdat header of `segment`: 8 bytes, or 16 when the payload
 * needs a 64-bit size. */
[[nodiscard]] std::uint64_t mdat_header_size(const Mp4Movie& movie, const SegmentLayout& segment);

/** Sum of the sample sizes `segment` carries: the mdat payload. */
[[nodiscard]] std::uint64_t mdat_payload_size(const Mp4Movie& movie, const SegmentLayout& segment);

/** The moof box followed by the mdat header of `segment`; the mdat payload is
 * the samples of each range in track order, copied from the source. One traf
 * per non-empty range: tfhd (default-base-is-moof), tfdt with the source decode
 * time of its first sample, and trun with per-sample duration and size, and
 * for video also flags and composition offsets (version 1 when any offset is
 * negative). Audio samples are all sync, set once through the tfhd default.
 * Its size is moof_size() + mdat_header_size(). */
[[nodiscard]] Bytes write_segment_header(const Mp4Movie& movie, const SegmentLayout& segment);
} // namespace send_airplay2::detail
#endif
