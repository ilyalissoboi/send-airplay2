// SPDX-License-Identifier: Apache-2.0
#include "hls_authoring.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace send_airplay2::detail {
namespace {
constexpr std::uint64_t microseconds_per_second = 1'000'000;
constexpr std::uint64_t bits_microseconds_per_byte = 8 * microseconds_per_second;
constexpr std::uint64_t maximum_bytes = 1ULL << 40;
constexpr std::uint64_t maximum_duration_us = 7ULL * 24 * 3600 * microseconds_per_second;
constexpr std::size_t maximum_segments = 201602;
constexpr std::size_t maximum_inspected_spans = 2'000'000;
void require(bool condition, RemuxFailure failure, const char* message) {
    if (!condition) {
        throw RemuxException(failure, message);
    }
}
std::uint64_t rate(std::uint64_t bytes, std::uint64_t duration_us) {
    // The aggregate byte bound makes multiplication fit uint64_t on every host.
    const auto numerator = bytes * bits_microseconds_per_byte;
    return numerator / duration_us + (numerator % duration_us != 0);
}
} // namespace
HlsBitRates add_hls_bit_rates(HlsBitRates left, HlsBitRates right) {
    constexpr auto maximum_rate = std::numeric_limits<std::uint64_t>::max();
    require(right.peak <= maximum_rate - left.peak && right.average <= maximum_rate - left.average,
            RemuxFailure::too_large, "HLS rendition rate overflow");
    return {left.peak + right.peak, left.average + right.average};
}
void validate_stereo_video_spans(const std::vector<std::uint64_t>& durations_us) {
    require(!durations_us.empty() && durations_us.size() <= maximum_segments,
            RemuxFailure::too_large, "stereo HLS video segment count bound");
    for (const auto duration : durations_us) {
        require(duration > 0, RemuxFailure::malformed, "stereo HLS empty video segment");
        require(duration <= maximum_stereo_video_segment_us, RemuxFailure::unsupported,
                "stereo HLS copied video segment exceeds twenty seconds");
    }
}
HlsBitRates hls_bit_rates(const std::vector<std::uint64_t>& durations,
                          const std::vector<std::uint64_t>& sizes, std::uint32_t target_seconds) {
    require(!durations.empty() && durations.size() == sizes.size(), RemuxFailure::malformed,
            "HLS rate table mismatch");
    require(durations.size() <= maximum_segments && target_seconds >= 1 && target_seconds <= 20,
            RemuxFailure::too_large, "HLS rate table bounds");
    std::uint64_t total_bytes = 0, total_us = 0;
    for (std::size_t index = 0; index < durations.size(); ++index) {
        require(durations[index] > 0, RemuxFailure::malformed, "HLS zero segment duration");
        require(sizes[index] <= maximum_bytes - total_bytes &&
                    durations[index] <= maximum_duration_us - total_us,
                RemuxFailure::too_large, "HLS aggregate rate bounds");
        total_bytes += sizes[index];
        total_us += durations[index];
    }
    const auto target_us = static_cast<std::uint64_t>(target_seconds) * microseconds_per_second;
    const auto minimum_span_us = target_us / 2;
    const auto maximum_span_us = target_us + target_us / 2;
    const auto average = rate(total_bytes, total_us);
    if (total_us < minimum_span_us) {
        return {average, average}; // No qualifying span can exist.
    }
    std::uint64_t peak = 0;
    bool found = false;
    std::size_t inspected = 0;
    for (std::size_t first = 0; first < durations.size(); ++first) {
        std::uint64_t bytes = 0, span_us = 0;
        for (auto end = first; end < durations.size(); ++end) {
            require(++inspected <= maximum_inspected_spans, RemuxFailure::too_large,
                    "HLS rate inspection budget");
            span_us += durations[end];
            if (span_us > maximum_span_us) {
                break;
            }
            bytes += sizes[end];
            if (span_us >= minimum_span_us) {
                found = true;
                peak = std::max(peak, rate(bytes, span_us));
            }
        }
    }
    return {found ? peak : average, average};
}
} // namespace send_airplay2::detail
