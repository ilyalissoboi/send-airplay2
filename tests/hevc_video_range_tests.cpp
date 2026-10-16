// SPDX-License-Identifier: Apache-2.0
#include "hevc_video_range.h"
#include "hevc_video_range_fixture.h"
#include "hls_variant.h"
#include "sample_entries.h"
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace send_airplay2::detail;
int failures = 0;
void check(bool passed, const std::string& scenario) {
    if (!passed) {
        ++failures;
        std::cerr << "FAIL [HEVC video range]: " << scenario << '\n';
    }
}
template <class Action>
void refuses(Action action, RemuxFailure reason, const std::string& scenario) {
    try {
        action();
        check(false, scenario + " accepted");
    } catch (const RemuxException& error) {
        check(error.reason() == reason, scenario + " category");
    }
}

/// Test-only syntax authoring, independent of the production reader. The
/// literal x265 known answer additionally guards against a shared syntax error.
class Syntax {
public:
    void put(std::uint32_t value, unsigned width) {
        for (unsigned bit = width; bit-- > 0;) {
            if (size_ % 8 == 0) {
                bytes_.push_back(0);
            }
            bytes_.back() |= static_cast<std::uint8_t>(((value >> bit) & 1U) << (7 - size_ % 8));
            ++size_;
        }
    }
    void ue(std::uint32_t value) {
        const auto code = value + 1;
        unsigned width = 0;
        for (auto rest = code; rest; rest >>= 1) {
            ++width;
        }
        put(0, width - 1);
        put(code, width);
    }
    Bytes nal() {
        put(1, 1); // rbsp_stop_one_bit; only syntax before VUI transfer is consumed.
        Bytes output{0x42, 0x01};
        unsigned zeros = 0;
        for (auto byte : bytes_) {
            if (zeros == 2 && byte <= 3) {
                output.push_back(3);
                zeros = 0;
            }
            output.push_back(byte);
            zeros = byte == 0 ? zeros + 1 : 0;
        }
        return output;
    }

private:
    Bytes bytes_;
    unsigned size_ = 0;
};

Bytes sps(unsigned transfer, bool complex = false, bool vui = true, bool colour = true,
          unsigned short_term_sets = 0) {
    Syntax out;
    out.put(0, 4);
    out.put(complex ? 2 : 0, 3);
    out.put(1, 1);
    out.put(1, 8);
    out.put(0, 32);
    out.put(0, 32);
    out.put(0, 16);
    out.put(153, 8);
    if (complex) {
        out.put(1, 1);
        out.put(1, 1);
        out.put(0, 1);
        out.put(1, 1);
        out.put(0, 12); // six reserved_zero_2bits.
        out.put(1, 8);
        out.put(0, 32);
        out.put(0, 32);
        out.put(0, 16);
        out.put(120, 8);
        out.put(120, 8);
    }
    out.ue(0);
    out.ue(complex ? 3 : 1);
    if (complex) {
        out.put(0, 1);
    }
    out.ue(640);
    out.ue(360);
    out.put(complex ? 1 : 0, 1);
    if (complex) {
        for (unsigned i = 0; i < 4; ++i) {
            out.ue(0);
        }
    }
    out.ue(2);
    out.ue(2);
    out.ue(4);
    out.put(0, 1);
    out.ue(4);
    out.ue(2);
    out.ue(0);
    for (unsigned i = 0; i < 6; ++i) {
        out.ue(0);
    }
    out.put(complex ? 1 : 0, 1);
    if (complex) {
        out.put(1, 1);
        for (unsigned size = 0; size < 4; ++size) {
            for (unsigned matrix = 0; matrix < 6; matrix += size == 3 ? 3 : 1) {
                const bool explicit_values = matrix == 0;
                out.put(explicit_values ? 1 : 0, 1);
                if (!explicit_values) {
                    out.ue(0);
                } else {
                    if (size > 1) {
                        out.ue(0);
                    }
                    for (unsigned coefficient = 0; coefficient < (size == 0 ? 16U : 64U);
                         ++coefficient) {
                        out.ue(0);
                    }
                }
            }
        }
    }
    out.put(0, 2);
    out.put(complex ? 1 : 0, 1);
    if (complex) {
        out.put(0x99, 8);
        out.ue(0);
        out.ue(0);
        out.put(0, 1);
    }
    out.ue(short_term_sets);
    if (short_term_sets == 3) {
        out.ue(1);
        out.ue(1);
        out.ue(0);
        out.put(1, 1);
        out.ue(0);
        out.put(1, 1); // {-1,+1}
        out.put(1, 1);
        out.put(0, 1);
        out.ue(0);     // predicted, delta +1.
        out.put(1, 3); // Drop the zero candidate, then select +2.
        // The preceding three bits are 001: used[0]=0,use_delta[0]=0,used[1]=1.
        out.put(1, 1); // used[2]=1. Result {+1,+2}.
        out.put(1, 1);
        out.put(1, 1);
        out.ue(0); // delta -1.
        out.put(1, 1);
        out.put(1, 1);
        out.put(0, 1);
        out.put(1, 1); // {+1,-1}, zero excluded.
    }
    out.put(complex ? 1 : 0, 1);
    if (complex) {
        out.ue(2);
        out.put(3, 8);
        out.put(1, 1);
        out.put(4, 8);
        out.put(0, 1);
    }
    out.put(0, 2);
    out.put(vui ? 1 : 0, 1);
    if (vui) {
        out.put(complex ? 1 : 0, 1);
        if (complex) {
            out.put(255, 8);
            out.put(1, 16);
            out.put(1, 16);
        }
        out.put(complex ? 1 : 0, 1);
        if (complex) {
            out.put(0, 1);
        }
        out.put(1, 1);
        out.put(5, 3);
        out.put(0, 1);
        out.put(colour ? 1 : 0, 1);
        if (colour) {
            out.put(9, 8);
            out.put(transfer, 8);
            out.put(9, 8);
        }
    }
    return out.nal();
}

Bytes configuration(const std::vector<Bytes>& units) {
    Bytes output(23, 0);
    output[0] = 1;
    output[22] = 1;
    output.push_back(0xa1);
    output.push_back(0);
    output.push_back(static_cast<std::uint8_t>(units.size()));
    for (const auto& unit : units) {
        output.push_back(static_cast<std::uint8_t>(unit.size() >> 8));
        output.push_back(static_cast<std::uint8_t>(unit.size()));
        output.insert(output.end(), unit.begin(), unit.end());
    }
    return output;
}

void known_answer_and_ranges() {
    const auto known = sap2_test::hdr10_configuration();
    check(hevc_video_range(known) == VideoRange::pq, "independent x265 HDR10 SPS");
    for (const auto complex : {false, true}) {
        for (const auto transfer : {1U, 2U, 16U, 18U, 255U}) {
            const auto expected = transfer == 16   ? VideoRange::pq
                                  : transfer == 18 ? VideoRange::hlg
                                  : transfer == 1  ? VideoRange::sdr
                                                   : VideoRange::unspecified;
            check(hevc_video_range(configuration(
                      {sps(transfer, complex, true, true, complex ? 3 : 0)})) == expected,
                  "VUI transfer=" + std::to_string(transfer) +
                      " optional_syntax=" + std::to_string(complex));
        }
    }
    check(hevc_video_range(configuration({sps(16, false, false)})) == VideoRange::unspecified,
          "absent VUI");
    check(hevc_video_range(configuration({sps(16, false, true, false)})) == VideoRange::unspecified,
          "absent colour description");
    check(hevc_video_range(configuration({})) == VideoRange::unspecified, "no SPS");
    check(hevc_video_range(configuration({sps(16), sps(16)})) == VideoRange::pq, "agreeing SPS");
    check(hevc_video_range(configuration(std::vector<Bytes>(16, sps(16)))) == VideoRange::pq,
          "literal SPS count boundary 16");
    refuses([&] { (void)hevc_video_range(configuration({sps(16), sps(18)})); },
            RemuxFailure::unsupported, "conflicting SPS");
    refuses([&] { (void)hevc_video_range(configuration({sps(1), sps(16)})); },
            RemuxFailure::unsupported, "SDR/PQ SPS conflict");
}

void malformed_bounds() {
    const auto value = configuration({sps(16, true, true, true, 3)});
    // Stop before the final syntax byte: parsing deliberately ends at transfer metadata.
    for (std::size_t split = 0; split < value.size(); ++split) {
        const Bytes truncated(value.begin(), value.begin() + static_cast<std::ptrdiff_t>(split));
        refuses([&] { (void)hevc_video_range(truncated); }, RemuxFailure::malformed,
                "truncated hvcC at " + std::to_string(split));
    }
    const auto unit = sps(16, true, true, true, 3);
    // Reframe each partial NAL with a correct outer length so these cases
    // exercise the RBSP bit bounds, not only hvcC's representation bounds.
    // The final byte may contain only rbsp_stop_one_bit, outside this reader.
    for (std::size_t split = 2; split + 1 < unit.size(); ++split) {
        const Bytes partial(unit.begin(), unit.begin() + static_cast<std::ptrdiff_t>(split));
        refuses([&] { (void)hevc_video_range(configuration({partial})); }, RemuxFailure::malformed,
                "truncated SPS at " + std::to_string(split));
    }
    auto trailing = value;
    trailing.push_back(0);
    refuses([&] { (void)hevc_video_range(trailing); }, RemuxFailure::malformed,
            "trailing hvcC bytes");
    auto invalid_nal = sps(16);
    invalid_nal[0] |= 0x80;
    refuses([&] { (void)hevc_video_range(configuration({invalid_nal})); }, RemuxFailure::malformed,
            "forbidden NAL bit");
    refuses([&] { (void)hevc_video_range(configuration(std::vector<Bytes>(17, sps(16)))); },
            RemuxFailure::malformed, "SPS count 17");
    refuses([&] { (void)hevc_video_range(configuration({sps(16, false, true, true, 65)})); },
            RemuxFailure::malformed, "short-term set count 65");
    auto invalid_escape = sps(16);
    invalid_escape.insert(invalid_escape.end(), {0, 0, 3, 4});
    refuses([&] { (void)hevc_video_range(configuration({invalid_escape})); },
            RemuxFailure::malformed, "invalid emulation prevention");
}

void master_signaling() {
    Mp4Track track;
    track.codec = "hvc1";
    track.sample_entry =
        visual_sample_entry("hvc1", 640, 360, 640, 360, sap2_test::hdr10_configuration());
    check(video_range(track) == VideoRange::pq, "sample entry extraction");
    VariantStream variant;
    variant.codecs = "hvc1.2.4.H153.90,mp4a.40.2";
    variant.uri = "index.m3u8";
    variant.peak_bits_per_second = 1000000;
    variant.video_range = video_range(track);
    AudioRendition audio{"audio.m3u8", "en"};
    check(multivariant_playlist(variant, {}, &audio).find(",VIDEO-RANGE=PQ") != std::string::npos,
          "converted HDR10 master");
    variant.video_range = VideoRange::hlg;
    check(multivariant_playlist(variant, {}).find(",VIDEO-RANGE=HLG") != std::string::npos,
          "HLG master");
    variant.video_range = VideoRange::sdr;
    check(multivariant_playlist(variant, {}).find("VIDEO-RANGE") == std::string::npos,
          "SDR master unchanged");
}
} // namespace
int main() {
    try {
        known_answer_and_ranges();
        malformed_bounds();
        master_signaling();
    } catch (const std::exception& error) {
        std::cerr << "HEVC video range infrastructure: " << error.what() << '\n';
        return 1;
    }
    return failures ? 1 : 0;
}
