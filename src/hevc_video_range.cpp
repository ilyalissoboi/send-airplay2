// SPDX-License-Identifier: Apache-2.0
// Original metadata reader from ITU-T H.265 (01/2026), 7.3.2.2.1, 7.3.3,
// 7.3.4, 7.3.7 and E.2.1; hvcC framing from ISO/IEC 14496-15.
// No third-party implementation or additional codec dependency.
#include "hevc_video_range.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace send_airplay2::detail {
namespace {
constexpr std::size_t configuration_header_bytes = 23;
constexpr std::uint8_t sps_nal_type = 33;
constexpr std::size_t nal_header_bytes = 2;
constexpr std::uint32_t maximum_sps_count = 16;
constexpr std::uint32_t maximum_short_term_sets = 64;
constexpr std::uint32_t maximum_reference_pictures = 16;
constexpr std::uint32_t maximum_long_term_pictures = 32;

[[noreturn]] void malformed() {
    throw RemuxException(RemuxFailure::malformed, "HEVC video range: invalid metadata");
}

class Bits {
public:
    explicit Bits(const Bytes& bytes) : bytes_(bytes) {}
    std::uint32_t read(unsigned count) {
        if (count > 32 || count > bytes_.size() * 8 - position_) {
            malformed();
        }
        std::uint32_t value = 0;
        for (unsigned index = 0; index < count; ++index, ++position_) {
            value = (value << 1) | ((bytes_[position_ / 8] >> (7 - position_ % 8)) & 1U);
        }
        return value;
    }
    void skip(std::size_t count) {
        if (count > bytes_.size() * 8 - position_) {
            malformed();
        }
        position_ += count;
    }
    std::uint32_t ue(std::uint32_t maximum = std::numeric_limits<std::uint32_t>::max()) {
        unsigned zeros = 0;
        while (!read(1)) {
            if (++zeros > 31) {
                malformed();
            }
        }
        const auto value = ((std::uint64_t{1} << zeros) - 1) + read(zeros);
        if (value > maximum) {
            malformed();
        }
        return static_cast<std::uint32_t>(value);
    }

private:
    const Bytes& bytes_;
    std::size_t position_ = 0;
};

/// Remove only valid emulation-prevention bytes, after the two-byte NAL header.
Bytes sps_rbsp(const Bytes& config, std::size_t begin, std::size_t end) {
    Bytes bytes;
    bytes.reserve(end - begin);
    unsigned zero_bytes = 0;
    for (auto position = begin; position < end; ++position) {
        const auto value = config[position];
        if (zero_bytes == 2 && value == 3) {
            if (position + 1 == end || config[position + 1] > 3) {
                malformed();
            }
            zero_bytes = 0;
            continue;
        }
        bytes.push_back(value);
        zero_bytes = value == 0 ? std::min(zero_bytes + 1, 2U) : 0;
    }
    return bytes;
}

void skip_profile_tier_level(Bits& bits, unsigned sub_layers) {
    // General profile/constraints occupy 88 bits regardless of profile, plus
    // eight level bits. Optional sub-layer profiles have the same fixed width.
    bits.skip(96);
    std::array<bool, 6> profile{}, level{};
    for (unsigned layer = 0; layer < sub_layers; ++layer) {
        profile[layer] = bits.read(1) != 0;
        level[layer] = bits.read(1) != 0;
    }
    if (sub_layers) {
        bits.skip((8 - sub_layers) * 2);
    }
    for (unsigned layer = 0; layer < sub_layers; ++layer) {
        if (profile[layer]) {
            bits.skip(88);
        }
        if (level[layer]) {
            bits.skip(8);
        }
    }
}

void skip_scaling_lists(Bits& bits) {
    for (unsigned size = 0; size < 4; ++size) {
        for (unsigned matrix = 0; matrix < 6; matrix += size == 3 ? 3 : 1) {
            if (!bits.read(1)) {
                (void)bits.ue();
            } else {
                if (size > 1) {
                    (void)bits.ue(); // se(v) has the same codeword as ue(v).
                }
                const auto coefficients = size == 0 ? 16U : 64U;
                for (unsigned index = 0; index < coefficients; ++index) {
                    (void)bits.ue();
                }
            }
        }
    }
}

void skip_short_term_sets(Bits& bits) {
    const auto count = bits.ue(maximum_short_term_sets);
    std::vector<std::int64_t> previous;
    for (std::uint32_t index = 0; index < count; ++index) {
        std::vector<std::int64_t> current;
        if (index && bits.read(1)) {
            const auto negative = bits.read(1) != 0;
            const auto magnitude = std::int64_t{bits.ue()} + 1;
            const auto delta = negative ? -magnitude : magnitude;
            for (std::size_t picture = 0; picture <= previous.size(); ++picture) {
                const bool used = bits.read(1) != 0;
                const bool keep = used || bits.read(1) != 0;
                const auto poc = delta + (picture < previous.size() ? previous[picture] : 0);
                if (keep && poc != 0) {
                    current.push_back(poc);
                }
            }
            if (current.size() > maximum_reference_pictures) {
                malformed();
            }
            // RefRpsIdx is the preceding SPS set. Its picture indices are
            // negative POCs nearest zero first, then positive POCs ascending.
            std::sort(current.begin(), current.end(), [](auto left, auto right) {
                if ((left < 0) != (right < 0)) {
                    return left < 0;
                }
                return left < 0 ? left > right : left < right;
            });
        } else {
            const auto negative = bits.ue(maximum_reference_pictures);
            const auto positive = bits.ue(maximum_reference_pictures - negative);
            std::int64_t poc = 0;
            for (std::uint32_t picture = 0; picture < negative; ++picture) {
                poc -= std::int64_t{bits.ue()} + 1;
                bits.skip(1);
                current.push_back(poc);
            }
            poc = 0;
            for (std::uint32_t picture = 0; picture < positive; ++picture) {
                poc += std::int64_t{bits.ue()} + 1;
                bits.skip(1);
                current.push_back(poc);
            }
        }
        previous = std::move(current);
    }
}

VideoRange read_vui_range(Bits& bits) {
    if (bits.read(1) && bits.read(8) == 255) {
        bits.skip(32); // Extended sample aspect ratio.
    }
    if (bits.read(1)) {
        bits.skip(1); // overscan_appropriate_flag.
    }
    if (!bits.read(1)) {
        return VideoRange::unspecified;
    }
    bits.skip(4); // video_format and video_full_range_flag are not dynamic range.
    if (!bits.read(1)) {
        return VideoRange::unspecified;
    }
    bits.skip(8); // colour_primaries alone cannot identify PQ/HLG.
    const auto transfer = bits.read(8);
    bits.skip(8); // matrix_coeffs.
    if (transfer == 16) {
        return VideoRange::pq;
    }
    if (transfer == 18) {
        return VideoRange::hlg;
    }
    if (transfer == 2 || transfer == 0 || transfer == 3 || transfer > 18) {
        return VideoRange::unspecified;
    }
    return VideoRange::sdr;
}

VideoRange read_sps_range(const Bytes& rbsp) {
    Bits bits(rbsp);
    bits.skip(4); // sps_video_parameter_set_id.
    const auto sub_layers = bits.read(3);
    if (sub_layers > 6) {
        malformed();
    }
    bits.skip(1); // sps_temporal_id_nesting_flag.
    skip_profile_tier_level(bits, sub_layers);
    (void)bits.ue(15); // sps_seq_parameter_set_id.
    if (bits.ue(3) == 3) {
        bits.skip(1); // separate_colour_plane_flag.
    }
    (void)bits.ue(); // pic_width_in_luma_samples.
    (void)bits.ue(); // pic_height_in_luma_samples.
    if (bits.read(1)) {
        for (unsigned index = 0; index < 4; ++index) {
            (void)bits.ue();
        }
    }
    (void)bits.ue(8); // bit_depth_luma_minus8.
    (void)bits.ue(8); // bit_depth_chroma_minus8.
    const auto poc_bits = bits.ue(12) + 4;
    const auto first_ordering_layer = bits.read(1) ? 0 : sub_layers;
    for (auto layer = first_ordering_layer; layer <= sub_layers; ++layer) {
        (void)bits.ue();
        (void)bits.ue();
        (void)bits.ue();
    }
    // Six coding/transform block size and hierarchy fields; none drives a loop.
    for (unsigned index = 0; index < 6; ++index) {
        (void)bits.ue();
    }
    if (bits.read(1) && bits.read(1)) {
        skip_scaling_lists(bits);
    }
    bits.skip(2); // amp_enabled_flag, sample_adaptive_offset_enabled_flag.
    if (bits.read(1)) {
        bits.skip(8); // PCM bit depths.
        (void)bits.ue();
        (void)bits.ue();
        bits.skip(1); // pcm_loop_filter_disabled_flag.
    }
    skip_short_term_sets(bits);
    if (bits.read(1)) {
        const auto pictures = bits.ue(maximum_long_term_pictures);
        bits.skip(pictures * (poc_bits + 1));
    }
    bits.skip(2); // Temporal MVP and intra smoothing.
    return bits.read(1) ? read_vui_range(bits) : VideoRange::unspecified;
}

std::uint16_t read_u16(const Bytes& bytes, std::size_t& position) {
    if (position > bytes.size() || bytes.size() - position < 2) {
        malformed();
    }
    const auto value = static_cast<std::uint16_t>((bytes[position] << 8) | bytes[position + 1]);
    position += 2;
    return value;
}
} // namespace

VideoRange hevc_video_range(const Bytes& configuration) {
    if (configuration.size() < configuration_header_bytes || configuration[0] != 1) {
        malformed();
    }
    std::size_t position = configuration_header_bytes;
    unsigned sps_count = 0;
    auto result = VideoRange::unspecified;
    for (unsigned array = 0; array < configuration[22]; ++array) {
        if (position >= configuration.size()) {
            malformed();
        }
        const auto type = configuration[position++] & 0x3f;
        const auto count = read_u16(configuration, position);
        for (unsigned nal = 0; nal < count; ++nal) {
            const auto length = read_u16(configuration, position);
            if (length < nal_header_bytes || length > configuration.size() - position) {
                malformed();
            }
            if (type == sps_nal_type) {
                if (++sps_count > maximum_sps_count) {
                    malformed();
                }
                const auto header = (configuration[position] << 8) | configuration[position + 1];
                if ((header & 0x8000) || ((header >> 9) & 0x3f) != sps_nal_type || !(header & 7)) {
                    malformed();
                }
                if (((header >> 3) & 0x3f) == 0) {
                    const auto range = read_sps_range(
                        sps_rbsp(configuration, position + nal_header_bytes, position + length));
                    if (range != VideoRange::unspecified) {
                        if (result != VideoRange::unspecified && result != range) {
                            throw RemuxException(RemuxFailure::unsupported,
                                                 "HEVC SPS video ranges disagree");
                        }
                        result = range;
                    }
                }
            }
            position += length;
        }
    }
    if (position != configuration.size()) {
        malformed();
    }
    return result;
}
} // namespace send_airplay2::detail
