// SPDX-License-Identifier: Apache-2.0
// Multivariant playlists and codec strings for the HLS remux (D60 phase 3c).
// Written from RFC 6381, ISO/IEC 14496-15 Annex E and RFC 8216bis; no
// third-party code.
#include "hls_variant.h"
#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <string>

namespace send_airplay2::detail {
namespace {
constexpr std::size_t box_header_bytes = 8;
constexpr std::size_t visual_entry_fields = 78; // VisualSampleEntry after its header.
constexpr std::size_t audio_entry_fields = 28;  // AudioSampleEntry version 0.
constexpr std::size_t hevc_constraint_bytes = 6;
constexpr const char* hearing_impaired_characteristics =
    "public.accessibility.transcribes-spoken-dialog,"
    "public.accessibility.describes-music-and-sound";

[[noreturn]] void malformed(const std::string& what) {
    throw RemuxException(RemuxFailure::malformed, "codec string: " + what);
}

std::uint32_t load_u32(const Bytes& data, std::size_t at) {
    return (static_cast<std::uint32_t>(data.at(at)) << 24) |
           (static_cast<std::uint32_t>(data.at(at + 1)) << 16) |
           (static_cast<std::uint32_t>(data.at(at + 2)) << 8) | data.at(at + 3);
}

/// Payload of the first child box of `type` in data[begin, end).
std::optional<Bytes> child(const Bytes& data, std::size_t begin, const char* type) {
    std::size_t position = begin;
    while (position + box_header_bytes <= data.size()) {
        const auto size = load_u32(data, position);
        if (size < box_header_bytes || size > data.size() - position) {
            return std::nullopt;
        }
        if (std::equal(type, type + 4, data.begin() + static_cast<std::ptrdiff_t>(position + 4))) {
            return Bytes(data.begin() + static_cast<std::ptrdiff_t>(position + box_header_bytes),
                         data.begin() + static_cast<std::ptrdiff_t>(position + size));
        }
        position += size;
    }
    return std::nullopt;
}

std::string hex(std::uint32_t value, bool upper) {
    std::array<char, 16> buffer{};
    std::snprintf(buffer.data(), buffer.size(), upper ? "%X" : "%x", value);
    return buffer.data();
}

std::string avc_string(const std::string& codec, const Bytes& entry) {
    const auto avcc = child(entry, box_header_bytes + visual_entry_fields, "avcC");
    if (!avcc || avcc->size() < 4) {
        malformed("avcC missing or short");
    }
    std::array<char, 16> buffer{};
    // AVCProfileIndication, profile_compatibility, AVCLevelIndication.
    std::snprintf(buffer.data(), buffer.size(), ".%02x%02x%02x", (*avcc)[1], (*avcc)[2],
                  (*avcc)[3]);
    return codec + buffer.data();
}

std::string hevc_string(const std::string& codec, const Bytes& entry) {
    const auto hvcc = child(entry, box_header_bytes + visual_entry_fields, "hvcC");
    if (!hvcc || hvcc->size() < 13) {
        malformed("hvcC missing or short");
    }
    const auto& config = *hvcc;
    const auto profile_space = config[1] >> 6;
    const bool high_tier = (config[1] >> 5) & 1;
    const auto profile_idc = config[1] & 0x1f;
    // general_profile_compatibility_flags, written in reverse bit order.
    const auto flags = load_u32(config, 2);
    std::uint32_t reversed = 0;
    for (int bit = 0; bit < 32; ++bit) {
        reversed |= ((flags >> bit) & 1U) << (31 - bit);
    }
    std::string out = codec + '.';
    if (profile_space) {
        out += static_cast<char>('A' + profile_space - 1);
    }
    out += std::to_string(profile_idc) + '.' + hex(reversed, true) + '.' + (high_tier ? 'H' : 'L') +
           std::to_string(config[12]);
    // The six constraint-indicator bytes, trailing zero bytes omitted.
    std::size_t last = hevc_constraint_bytes;
    while (last > 0 && config[6 + last - 1] == 0) {
        --last;
    }
    for (std::size_t index = 0; index < last; ++index) {
        out += '.' + hex(config[6 + index], true);
    }
    return out;
}

/// The descriptor length (ISO/IEC 14496-1 8.3.3) at `at`, advancing past it.
std::size_t descriptor_length(const Bytes& data, std::size_t& at) {
    std::size_t length = 0;
    for (int index = 0; index < 4; ++index) {
        const auto byte = data.at(at++);
        length = (length << 7) | (byte & 0x7fU);
        if (!(byte & 0x80)) {
            return length;
        }
    }
    malformed("esds descriptor length");
}

std::string aac_string(const Bytes& entry) {
    auto start = box_header_bytes + audio_entry_fields;
    const auto version = entry.size() > 17 ? (entry[16] << 8 | entry[17]) : 0;
    start += version == 1 ? 16 : version == 2 ? 36 : 0;
    const auto esds = child(entry, start, "esds");
    if (!esds) {
        malformed("esds missing");
    }
    const auto& data = *esds;
    std::size_t at = 4; // Full box version and flags.
    if (data.at(at++) != 0x03) {
        malformed("esds without an ES descriptor");
    }
    (void)descriptor_length(data, at);
    const auto flags = data.at(at + 2);
    at += 3; // ES_ID and flags.
    if (flags & 0x80) {
        at += 2;
    }
    if (flags & 0x40) {
        at += 1 + data.at(at);
    }
    if (flags & 0x20) {
        at += 2;
    }
    if (data.at(at++) != 0x04) {
        malformed("esds without a decoder configuration");
    }
    (void)descriptor_length(data, at);
    const auto object_type = data.at(at);
    at += 13; // objectTypeIndication .. avgBitrate.
    if (object_type != 0x40) {
        return "mp4a." + hex(object_type, false);
    }
    if (data.at(at++) != 0x05) {
        return "mp4a.40.2"; // No AudioSpecificConfig: assume AAC-LC.
    }
    (void)descriptor_length(data, at);
    auto audio_object_type = static_cast<std::uint32_t>(data.at(at) >> 3);
    if (audio_object_type == 31) {
        // Escape: 6 more bits across the next byte boundary.
        audio_object_type = 32 + (((data.at(at) & 0x07U) << 3) | (data.at(at + 1) >> 5));
    }
    return "mp4a.40." + std::to_string(audio_object_type);
}

std::string yes_no(bool value) {
    return value ? "YES" : "NO";
}

/// RFC 8216bis quoted-strings cannot contain '"' or line breaks.
std::string quoted(const std::string& value) {
    std::string out = "\"";
    for (const auto c : value) {
        if (c != '"' && c != '\r' && c != '\n') {
            out += c;
        }
    }
    return out + '"';
}
} // namespace

std::string codec_string(const Mp4Track& track) {
    const auto& codec = track.codec;
    if (codec == "avc1" || codec == "avc3") {
        return avc_string(codec, track.sample_entry);
    }
    if (codec == "hvc1" || codec == "hev1") {
        return hevc_string(codec, track.sample_entry);
    }
    if (codec == "mp4a") {
        return aac_string(track.sample_entry);
    }
    return codec; // "ac-3" and "ec-3" are their own codec strings.
}

std::string multivariant_playlist(const VariantStream& variant,
                                  const std::vector<SubtitleRendition>& subtitles) {
    std::string out = "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-INDEPENDENT-SEGMENTS\n";
    for (const auto& subtitle : subtitles) {
        out += "#EXT-X-MEDIA:TYPE=SUBTITLES,GROUP-ID=\"subs\",NAME=" + quoted(subtitle.name) +
               ",LANGUAGE=" + quoted(subtitle.language) +
               ",DEFAULT=" + yes_no(subtitle.default_track) +
               ",AUTOSELECT=YES,FORCED=" + yes_no(subtitle.forced);
        if (subtitle.hearing_impaired) {
            out += ",CHARACTERISTICS=" + quoted(hearing_impaired_characteristics);
        }
        out += ",URI=" + quoted(subtitle.uri) + '\n';
    }
    out += "#EXT-X-STREAM-INF:BANDWIDTH=" + std::to_string(variant.peak_bits_per_second) +
           ",AVERAGE-BANDWIDTH=" + std::to_string(variant.average_bits_per_second) +
           ",CODECS=" + quoted(variant.codecs);
    if (variant.width && variant.height) {
        out +=
            ",RESOLUTION=" + std::to_string(variant.width) + 'x' + std::to_string(variant.height);
    }
    if (variant.frame_rate_milli) {
        std::array<char, 32> rate{};
        std::snprintf(rate.data(), rate.size(), "%llu.%03llu",
                      static_cast<unsigned long long>(variant.frame_rate_milli / 1000),
                      static_cast<unsigned long long>(variant.frame_rate_milli % 1000));
        out += ",FRAME-RATE=" + std::string(rate.data());
    }
    if (!subtitles.empty()) {
        out += ",SUBTITLES=\"subs\"";
    }
    out += '\n' + variant.uri + '\n';
    return out;
}
} // namespace send_airplay2::detail
