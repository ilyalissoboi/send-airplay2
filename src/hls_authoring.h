// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_HLS_AUTHORING_H
#define SEND_AIRPLAY2_HLS_AUTHORING_H
#include "mp4_demux.h"
#include <cstdint>
#include <vector>

namespace send_airplay2::detail {
/// Engineering admission policy, matching the existing indexed-MKV span bound.
/// A longer copied segment is refused, including the final segment. This is not
/// Apple's nominal six-second recommendation or a receiver compatibility claim.
inline constexpr std::uint64_t maximum_stereo_video_segment_us = 20'000'000;
void validate_stereo_video_spans(const std::vector<std::uint64_t>& durations_us);
struct HlsBitRates {
    std::uint64_t peak = 0;
    std::uint64_t average = 0;
};
/// Sum simultaneously playable rendition rates; throws too_large on overflow.
[[nodiscard]] HlsBitRates add_hls_bit_rates(HlsBitRates left, HlsBitRates right);
/** RFC 8216 4.1 rates including segment container bytes, excluding init/HTTP.
 * Peak examines contiguous spans in [0.5,1.5] times target; average uses all bytes
 * over all EXTINF microseconds. Round rates upward to integer bits/second.
 * If a complete short presentation has no qualifying span, use its average as
 * an explicit fallback. Bounds: 201602 segments, seven days, 1 TiB aggregate
 * bytes, target 1..20 s, two million inspected spans. No source I/O.
 * @throws RemuxException for inconsistent/zero durations or exceeded bounds.
 */
[[nodiscard]] HlsBitRates hls_bit_rates(const std::vector<std::uint64_t>& durations_us,
                                        const std::vector<std::uint64_t>& segment_bytes,
                                        std::uint32_t target_seconds);
} // namespace send_airplay2::detail
#endif
