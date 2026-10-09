// SPDX-License-Identifier: Apache-2.0
// Sample entries for the HLS remux of MKV input (D60). Written from ISO/IEC
// 14496-1/-3/-12/-14/-15 and ETSI TS 102 366; no third-party code.
#include "sample_entries.h"
#include "box_writer.h"
#include <algorithm>
#include <array>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>

namespace send_airplay2::detail {
namespace {
constexpr std::uint16_t data_reference_index = 1;
constexpr std::uint32_t screen_resolution_72_dpi = 0x00480000; // 16.16 fixed point.
constexpr std::uint16_t color_depth_24_bit = 0x0018;
constexpr std::uint8_t es_descriptor_tag = 0x03;
constexpr std::uint8_t decoder_config_tag = 0x04;
constexpr std::uint8_t decoder_specific_info_tag = 0x05;
constexpr std::uint8_t sl_config_tag = 0x06;
constexpr std::uint8_t mpeg4_audio_object_type = 0x40;
constexpr std::uint8_t audio_stream_type = 0x15; // streamType 5 (audio) << 2 | upStream 0 | 1.
constexpr std::uint8_t sl_predefined_mp4 = 0x02;
constexpr std::uint16_t dolby_sync_word = 0x0b77;
constexpr std::uint32_t ac3_samples_per_frame = 1536;
constexpr std::uint32_t eac3_samples_per_block = 256;

// ISO/IEC 14496-3 Table 1.18, samplingFrequencyIndex 0..12.
constexpr std::array<std::uint32_t, 13> aac_sample_rates{
    96000, 88200, 64000, 48000, 44100, 32000, 24000, 22050, 16000, 12000, 11025, 8000, 7350};
// TS 102 366 Table 4.13: AC-3 bit rate (kbit/s) by frmsizecod >> 1.
constexpr std::array<std::uint32_t, 19> ac3_bit_rates{
    32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 384, 448, 512, 576, 640};
constexpr std::array<std::uint32_t, 3> dolby_sample_rates{48000, 44100, 32000}; // By fscod.
// Full-bandwidth channels by acmod (TS 102 366 Table 4.3).
constexpr std::array<std::uint8_t, 8> acmod_channels{2, 1, 2, 3, 3, 4, 4, 5};
constexpr std::array<std::uint32_t, 4> eac3_blocks{1, 2, 3, 6}; // By numblkscod.

[[noreturn]] void malformed(const std::string& what) {
    throw RemuxException(RemuxFailure::malformed, what);
}
[[noreturn]] void unsupported(const std::string& what) {
    throw RemuxException(RemuxFailure::unsupported, what);
}

/// Most-significant-bit-first reader over a byte range.
class BitReader {
public:
    BitReader(const std::uint8_t* data, std::size_t size, const char* what)
        : data_(data), size_(size), what_(what) {}
    std::uint32_t bits(unsigned count) {
        std::uint32_t value = 0;
        for (unsigned bit = 0; bit < count; ++bit) {
            if (position_ >= size_ * 8) {
                malformed(std::string(what_) + " shorter than its fields");
            }
            value = (value << 1) | ((data_[position_ / 8] >> (7 - position_ % 8)) & 1U);
            ++position_;
        }
        return value;
    }
    void skip(unsigned count) {
        (void)bits(count);
    }

private:
    const std::uint8_t* data_;
    std::size_t size_;
    const char* what_;
    std::size_t position_ = 0;
};

/// Most-significant-bit-first writer for packed configuration boxes.
class BitWriter {
public:
    void bits(std::uint32_t value, unsigned count) {
        for (unsigned bit = count; bit-- > 0;) {
            if (filled_ % 8 == 0) {
                out_.push_back(0);
            }
            out_.back() |= static_cast<std::uint8_t>(((value >> bit) & 1U) << (7 - filled_ % 8));
            ++filled_;
        }
    }
    [[nodiscard]] const Bytes& bytes() const {
        return out_;
    }

private:
    Bytes out_;
    std::size_t filled_ = 0;
};

/// An MPEG-4 descriptor (ISO/IEC 14496-1 8.3.3) with a one- to four-byte
/// length, seven bits per byte and the high bit set on all but the last.
Bytes descriptor(std::uint8_t tag, const Bytes& body) {
    Bytes out{tag};
    const auto length = static_cast<std::uint32_t>(body.size());
    if (length >= (1U << 28)) {
        throw std::length_error("descriptor too long");
    }
    for (int shift = 21; shift > 0; shift -= 7) {
        if (length >> shift) {
            out.push_back(static_cast<std::uint8_t>(0x80 | ((length >> shift) & 0x7f)));
        }
    }
    out.push_back(static_cast<std::uint8_t>(length & 0x7f));
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

/// AudioSampleEntry fields (14496-12 12.2.3) after the box header. The
/// 16.16 samplerate field cannot hold rates above 65535; those use 0, and
/// the decoder configuration carries the rate.
void audio_entry_fields(BoxWriter& out, std::uint16_t channels, std::uint32_t sample_rate) {
    out.zeros(6);
    out.u16(data_reference_index);
    out.zeros(8); // Reserved.
    out.u16(channels);
    out.u16(16); // samplesize.
    out.u16(0);  // pre_defined.
    out.u16(0);  // Reserved.
    out.u32(sample_rate <= std::numeric_limits<std::uint16_t>::max() ? sample_rate << 16 : 0);
}
} // namespace

Bytes visual_sample_entry(const std::string& codec, std::uint16_t width, std::uint16_t height,
                          std::uint32_t display_width, std::uint32_t display_height,
                          const Bytes& decoder_configuration) {
    if (codec != "avc1" && codec != "hvc1") {
        throw std::invalid_argument("visual sample entry codec");
    }
    BoxWriter out;
    const auto entry = out.open(codec.c_str());
    out.zeros(6);
    out.u16(data_reference_index);
    out.zeros(2 + 2 + 12); // pre_defined, reserved, pre_defined.
    out.u16(width);
    out.u16(height);
    out.u32(screen_resolution_72_dpi);
    out.u32(screen_resolution_72_dpi);
    out.u32(0);    // Reserved.
    out.u16(1);    // frame_count.
    out.zeros(32); // compressorname.
    out.u16(color_depth_24_bit);
    out.u16(0xffff); // pre_defined = -1.
    const auto config = out.open(codec == "avc1" ? "avcC" : "hvcC");
    out.bytes(decoder_configuration);
    out.close(config);
    if (width && height && display_width && display_height) {
        // Pixel aspect ratio: display_width / width over display_height / height.
        auto horizontal = static_cast<std::uint64_t>(display_width) * height;
        auto vertical = static_cast<std::uint64_t>(width) * display_height;
        const auto divisor = std::gcd(horizontal, vertical);
        horizontal /= divisor;
        vertical /= divisor;
        if (horizontal != vertical && horizontal <= std::numeric_limits<std::uint32_t>::max() &&
            vertical <= std::numeric_limits<std::uint32_t>::max()) {
            const auto pasp = out.open("pasp");
            out.u32(static_cast<std::uint32_t>(horizontal));
            out.u32(static_cast<std::uint32_t>(vertical));
            out.close(pasp);
        }
    }
    out.close(entry);
    return out.take();
}

AacConfig read_aac_config(const Bytes& audio_specific_config) {
    constexpr std::uint32_t escape_object_type = 31;
    constexpr std::uint32_t explicit_frequency_index = 15;
    BitReader bits(audio_specific_config.data(), audio_specific_config.size(),
                   "AudioSpecificConfig");
    AacConfig config;
    auto object_type = bits.bits(5);
    if (object_type == escape_object_type) {
        object_type = 32 + bits.bits(6);
    }
    config.object_type = static_cast<std::uint8_t>(object_type);
    const auto index = bits.bits(4);
    if (index == explicit_frequency_index) {
        config.sample_rate = bits.bits(24);
    } else if (index < aac_sample_rates.size()) {
        config.sample_rate = aac_sample_rates[index];
    } else {
        malformed("AudioSpecificConfig with a reserved sampling frequency index");
    }
    config.channels = static_cast<std::uint8_t>(bits.bits(4));
    return config;
}

Bytes aac_config(std::uint8_t object_type, std::uint32_t sample_rate, std::uint8_t channels) {
    std::uint32_t index = 0;
    while (index < aac_sample_rates.size() && aac_sample_rates[index] != sample_rate) {
        ++index;
    }
    if (index == aac_sample_rates.size()) {
        unsupported("AAC sampling frequency without a frequency index");
    }
    BitWriter bits;
    bits.bits(object_type, 5);
    bits.bits(index, 4);
    bits.bits(channels, 4);
    bits.bits(0, 3); // GASpecificConfig: frameLength 1024, no core coder, no extension.
    return bits.bytes();
}

Bytes aac_sample_entry(std::uint8_t channels, std::uint32_t sample_rate,
                       const Bytes& audio_specific_config) {
    BoxWriter out;
    const auto entry = out.open("mp4a");
    audio_entry_fields(out, channels, sample_rate);
    Bytes decoder_config{mpeg4_audio_object_type, audio_stream_type};
    decoder_config.insert(decoder_config.end(), 3 + 4 + 4, 0); // Buffer size, max/avg bit rate.
    const auto specific = descriptor(decoder_specific_info_tag, audio_specific_config);
    decoder_config.insert(decoder_config.end(), specific.begin(), specific.end());
    Bytes es{0, 0, 0}; // ES_ID 0 (set by the track) and no optional fields.
    const auto config = descriptor(decoder_config_tag, decoder_config);
    es.insert(es.end(), config.begin(), config.end());
    const auto sl = descriptor(sl_config_tag, {sl_predefined_mp4});
    es.insert(es.end(), sl.begin(), sl.end());
    const auto esds = out.open_full("esds", 0, 0);
    out.bytes(descriptor(es_descriptor_tag, es));
    out.close(esds);
    out.close(entry);
    return out.take();
}

DolbyFrame read_dolby_frame(const std::uint8_t* data, std::size_t size) {
    BitReader bits(data, size, "AC-3 frame header");
    if (bits.bits(16) != dolby_sync_word) {
        malformed("AC-3 audio without a sync word");
    }
    // bsid sits at bits 40-44 in both formats, which tells them apart.
    BitReader peek(data, size, "AC-3 frame header");
    peek.skip(40);
    const auto bsid = peek.bits(5);
    DolbyFrame frame;
    frame.bsid = static_cast<std::uint8_t>(bsid);
    if (bsid <= 8) {
        bits.skip(16); // crc1.
        frame.fscod = static_cast<std::uint8_t>(bits.bits(2));
        const auto frmsizecod = bits.bits(6);
        if (frame.fscod == 3 || (frmsizecod >> 1) >= ac3_bit_rates.size()) {
            malformed("AC-3 frame with a reserved rate code");
        }
        frame.sample_rate = dolby_sample_rates[frame.fscod];
        frame.bit_rate_code = static_cast<std::uint8_t>(frmsizecod >> 1);
        const auto kbps = ac3_bit_rates[frame.bit_rate_code];
        // TS 102 366 Table 4.13 in closed form: 16-bit words per frame.
        std::uint32_t words = 0;
        if (frame.sample_rate == 48000) {
            words = 2 * kbps;
        } else if (frame.sample_rate == 32000) {
            words = 3 * kbps;
        } else {
            words = kbps * 1000 * ac3_samples_per_frame / 44100 / 16 + (frmsizecod & 1);
        }
        frame.frame_bytes = 2 * words;
        frame.samples = ac3_samples_per_frame;
        bits.skip(5); // bsid.
        frame.bsmod = static_cast<std::uint8_t>(bits.bits(3));
        frame.acmod = static_cast<std::uint8_t>(bits.bits(3));
        if ((frame.acmod & 1) && frame.acmod != 1) {
            bits.skip(2); // cmixlev.
        }
        if (frame.acmod & 4) {
            bits.skip(2); // surmixlev.
        }
        if (frame.acmod == 2) {
            bits.skip(2); // dsurmod.
        }
        frame.lfeon = static_cast<std::uint8_t>(bits.bits(1));
    } else if (bsid >= 11 && bsid <= 16) {
        frame.enhanced = true;
        frame.stream_type = static_cast<std::uint8_t>(bits.bits(2));
        bits.skip(3); // substreamid.
        frame.frame_bytes = 2 * (bits.bits(11) + 1);
        frame.fscod = static_cast<std::uint8_t>(bits.bits(2));
        std::uint32_t blocks = 6;
        if (frame.fscod == 3) {
            const auto fscod2 = bits.bits(2);
            if (fscod2 == 3) {
                malformed("E-AC-3 frame with a reserved rate code");
            }
            frame.sample_rate = dolby_sample_rates[fscod2] / 2;
        } else {
            frame.sample_rate = dolby_sample_rates[frame.fscod];
            blocks = eac3_blocks[bits.bits(2)];
        }
        frame.samples = blocks * eac3_samples_per_block;
        frame.acmod = static_cast<std::uint8_t>(bits.bits(3));
        frame.lfeon = static_cast<std::uint8_t>(bits.bits(1));
        frame.bsmod = 0; // Carried deeper in bsi; 0 (complete main) when absent.
    } else {
        unsupported("AC-3 bitstream id " + std::to_string(bsid));
    }
    frame.channels = static_cast<std::uint8_t>(acmod_channels[frame.acmod] + frame.lfeon);
    return frame;
}

Bytes ac3_sample_entry(const DolbyFrame& frame) {
    BoxWriter out;
    const auto entry = out.open("ac-3");
    audio_entry_fields(out, frame.channels, frame.sample_rate);
    BitWriter dac3;
    dac3.bits(frame.fscod, 2);
    dac3.bits(frame.bsid, 5);
    dac3.bits(frame.bsmod, 3);
    dac3.bits(frame.acmod, 3);
    dac3.bits(frame.lfeon, 1);
    dac3.bits(frame.bit_rate_code, 5);
    dac3.bits(0, 5); // Reserved.
    const auto box = out.open("dac3");
    out.bytes(dac3.bytes());
    out.close(box);
    out.close(entry);
    return out.take();
}

Bytes eac3_sample_entry(const DolbyFrame& frame) {
    constexpr std::uint8_t dependent_stream_type = 1;
    if (!frame.enhanced || frame.stream_type == dependent_stream_type) {
        unsupported("E-AC-3 with dependent substreams");
    }
    if (frame.fscod == 3) {
        unsupported("E-AC-3 at a reduced sample rate");
    }
    BoxWriter out;
    const auto entry = out.open("ec-3");
    audio_entry_fields(out, frame.channels, frame.sample_rate);
    const auto kbps = static_cast<std::uint32_t>(static_cast<std::uint64_t>(frame.frame_bytes) * 8 *
                                                 frame.sample_rate / frame.samples / 1000);
    BitWriter dec3;
    dec3.bits(kbps, 13);
    dec3.bits(0, 3); // num_ind_sub: one independent substream.
    dec3.bits(frame.fscod, 2);
    dec3.bits(frame.bsid, 5);
    dec3.bits(0, 1); // Reserved.
    dec3.bits(0, 1); // asvc.
    dec3.bits(frame.bsmod, 3);
    dec3.bits(frame.acmod, 3);
    dec3.bits(frame.lfeon, 1);
    dec3.bits(0, 3); // Reserved.
    dec3.bits(0, 4); // num_dep_sub.
    dec3.bits(0, 1); // Reserved (no chan_loc without dependent substreams).
    const auto box = out.open("dec3");
    out.bytes(dec3.bytes());
    out.close(box);
    out.close(entry);
    return out.take();
}

Bytes track_header_box(TrackKind kind, std::uint32_t track_id, std::uint32_t display_width,
                       std::uint32_t display_height) {
    constexpr std::uint32_t enabled_in_movie = 0x000003;
    BoxWriter out;
    const auto tkhd = out.open_full("tkhd", 0, enabled_in_movie);
    out.u32(0); // Creation time.
    out.u32(0); // Modification time.
    out.u32(track_id);
    out.u32(0);   // Reserved.
    out.u32(0);   // Duration.
    out.zeros(8); // Reserved.
    out.u16(0);   // Layer.
    out.u16(0);   // Alternate group.
    out.u16(kind == TrackKind::audio ? full_volume : 0);
    out.u16(0); // Reserved.
    out.identity_matrix();
    const bool video = kind == TrackKind::video;
    out.u32(video ? (std::min<std::uint32_t>(display_width, 0xffff) << 16) : 0);
    out.u32(video ? (std::min<std::uint32_t>(display_height, 0xffff) << 16) : 0);
    out.close(tkhd);
    return out.take();
}

std::uint16_t packed_language(const std::string& code) {
    constexpr std::uint16_t undetermined = 0x55c4; // "und".
    if (code.size() != 3) {
        return undetermined;
    }
    std::uint16_t packed = 0;
    for (const auto letter : code) {
        if (letter < 'a' || letter > 'z') {
            return undetermined;
        }
        packed = static_cast<std::uint16_t>((packed << 5) | (letter - 0x60));
    }
    return packed;
}
} // namespace send_airplay2::detail
