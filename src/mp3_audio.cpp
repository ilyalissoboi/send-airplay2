// SPDX-License-Identifier: Apache-2.0
#include "mp3_audio.h"
#include "mp4_demux.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace send_airplay2::detail {
namespace {
constexpr std::uint64_t maximum_source_bytes = 1ULL << 40;
constexpr std::uint64_t maximum_requested_bytes = 64ULL << 20;
constexpr std::size_t maximum_callbacks = 2'100'000;
constexpr std::uint64_t maximum_frames = 1'000'000;
constexpr std::size_t maximum_seek_points = 65536;
constexpr std::uint64_t maximum_seconds = 7 * 24 * 3600;
constexpr std::uint64_t maximum_id3_bytes = 1 << 20;
constexpr std::size_t mpeg_header_bytes = 4, id3_header_bytes = 10, id3v1_bytes = 128;
constexpr std::size_t xing_toc_bytes = 100;
constexpr std::size_t encoder_tag_bytes = 36, encoder_trim_offset = 21, encoder_crc_offset = 34;
constexpr std::size_t encoder_revision_offset = 9;
constexpr std::uint32_t xing_frames_flag = 1, xing_bytes_flag = 2, xing_toc_flag = 4,
                        xing_quality_flag = 8;
constexpr std::uint32_t encoder_trim_mask = (1U << 12) - 1;
constexpr std::size_t vbri_offset = mpeg_header_bytes + 32;

void require(bool condition, RemuxFailure reason, const char* message) {
    if (!condition) {
        throw RemuxException(reason, message);
    }
}
std::uint32_t big_endian(const Bytes& bytes, std::size_t offset, std::size_t length) {
    require(offset <= bytes.size() && length <= bytes.size() - offset, RemuxFailure::malformed,
            "MP3 metadata field truncated");
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < length; ++index) {
        value = (value << 8) | bytes[offset + index];
    }
    return value;
}
bool tag(const Bytes& bytes, std::size_t offset, const char* text, std::size_t count) {
    return offset <= bytes.size() && count <= bytes.size() - offset &&
           std::memcmp(bytes.data() + offset, text, count) == 0;
}
class FrameReader {
public:
    FrameReader(const MediaSource& source, const MediaReadContext& context)
        : source_(source), context_(context) {
        if (!source.size || !source.read_at || !context.server_stopped ||
            !context.request_cancelled) {
            throw std::invalid_argument("MP3 probe requires source and context");
        }
        check();
        size = source.size();
        check();
        require(size >= mpeg_header_bytes && size <= maximum_source_bytes, RemuxFailure::too_large,
                "MP3 source size bound");
    }
    FrameReader(const FrameReader&) = delete;
    FrameReader& operator=(const FrameReader&) = delete;
    FrameReader(FrameReader&&) = delete;
    FrameReader& operator=(FrameReader&&) = delete;
    void check() const {
        if (context_.should_stop()) {
            throw std::runtime_error("MP3 probe stopped");
        }
    }
    Bytes read(std::uint64_t offset, std::size_t length) {
        check();
        require(offset <= size && length <= size - offset, RemuxFailure::malformed,
                "MP3 header outside source");
        require(length <= maximum_requested_bytes - requested_, RemuxFailure::too_large,
                "MP3 header byte budget");
        requested_ += length;
        Bytes bytes(length);
        std::size_t done = 0;
        while (done != length) {
            check();
            require(++calls_ <= maximum_callbacks, RemuxFailure::too_large, "MP3 callback budget");
            const auto capacity = std::min<std::size_t>(65536, length - done);
            const auto count =
                source_.read_at(offset + done, bytes.data() + done, capacity, context_);
            check();
            if (!count || count > capacity) {
                throw std::runtime_error("MP3 header read failed");
            }
            done += count;
        }
        return bytes;
    }
    std::uint64_t size = 0;

private:
    const MediaSource& source_;
    const MediaReadContext& context_;
    std::uint64_t requested_ = 0;
    std::size_t calls_ = 0;
};

struct FrameHeader {
    std::uint32_t version = 0, rate = 0, channels = 0, bitrate = 0;
    std::uint32_t samples = 0, bytes = 0, side_bytes = 0;
};
FrameHeader frame_header(const Bytes& bytes) {
    const auto word = big_endian(bytes, 0, mpeg_header_bytes);
    constexpr unsigned version_shift = 19, layer_shift = 17, protection_shift = 16;
    constexpr unsigned bitrate_shift = 12, rate_shift = 10, padding_shift = 9, mode_shift = 6;
    require((word >> 21) == 0x7ff, RemuxFailure::malformed, "MP3 frame sync missing");
    FrameHeader result;
    result.version = (word >> version_shift) & 3;
    require(result.version != 1 && ((word >> layer_shift) & 3) == 1, RemuxFailure::unsupported,
            "MP3 requires MPEG layer III and a defined version");
    require(((word >> protection_shift) & 1) != 0, RemuxFailure::unsupported,
            "CRC-protected MP3 frames outside initial subset");
    const auto bitrate_index = (word >> bitrate_shift) & 15;
    const auto rate_index = (word >> rate_shift) & 3;
    require(bitrate_index > 0 && bitrate_index < 15 && rate_index < 3 && (word & 3) == 0,
            RemuxFailure::unsupported, "MP3 free-format, rate or emphasis unsupported");
    constexpr std::array<std::uint32_t, 14> mpeg1_kbps{32,  40,  48,  56,  64,  80,  96,
                                                       112, 128, 160, 192, 224, 256, 320};
    constexpr std::array<std::uint32_t, 14> low_rate_kbps{8,  16, 24, 32,  40,  48,  56,
                                                          64, 80, 96, 112, 128, 144, 160};
    constexpr std::array<std::uint32_t, 3> rates{44100, 48000, 32000};
    const auto divisor = result.version == 3 ? 1U : result.version == 2 ? 2U : 4U;
    result.rate = rates[rate_index] / divisor;
    result.bitrate = (result.version == 3 ? mpeg1_kbps : low_rate_kbps)[bitrate_index - 1] * 1000;
    result.channels = ((word >> mode_shift) & 3) == 3 ? 1 : 2;
    result.samples = result.version == 3 ? 1152 : 576;
    result.side_bytes =
        result.version == 3 ? (result.channels == 1 ? 17 : 32) : (result.channels == 1 ? 9 : 17);
    // Samples per frame * bits per second / (8 bits/byte * samples per second).
    result.bytes =
        result.samples * result.bitrate / (8 * result.rate) + ((word >> padding_shift) & 1);
    require(result.bytes > mpeg_header_bytes + result.side_bytes, RemuxFailure::malformed,
            "MP3 frame cannot hold its side information");
    return result;
}
std::pair<std::uint64_t, std::uint64_t> audio_extent(FrameReader& reader) {
    std::uint64_t begin = 0, end = reader.size;
    const auto first = reader.read(0, 3);
    if (tag(first, 0, "ID3", 3)) {
        const auto header = reader.read(0, id3_header_bytes);
        require((header[3] == 3 || header[3] == 4) && header[4] == 0 && header[5] == 0,
                RemuxFailure::unsupported, "MP3 ID3 version, revision or flags outside subset");
        std::uint32_t length = 0;
        for (std::size_t index = 6; index < id3_header_bytes; ++index) {
            require(header[index] < 128, RemuxFailure::malformed, "MP3 ID3 size is not synchsafe");
            length = (length << 7) | header[index];
        }
        require(length <= maximum_id3_bytes, RemuxFailure::too_large, "MP3 ID3 size bound");
        require(length <= end - id3_header_bytes, RemuxFailure::malformed,
                "MP3 ID3 body truncated");
        begin = id3_header_bytes + length;
    }
    if (end >= id3v1_bytes && tag(reader.read(end - id3v1_bytes, 3), 0, "TAG", 3)) {
        end -= id3v1_bytes;
    }
    require(begin < end, RemuxFailure::malformed, "MP3 has no MPEG frame extent");
    return {begin, end};
}

/// LAME Info-tag CRC: reflected CRC-16 polynomial 0xa001, initial zero.
std::uint16_t info_crc(const Bytes& frame, std::size_t end) {
    std::uint16_t value = 0;
    for (std::size_t index = 0; index < end; ++index) {
        value ^= frame[index];
        for (unsigned bit = 0; bit < 8; ++bit) {
            value = static_cast<std::uint16_t>((value >> 1) ^ ((value & 1) ? 0xa001 : 0));
        }
    }
    return value;
}
struct InfoTag {
    bool present = false;
    std::optional<std::uint32_t> frames, bytes;
    std::optional<Mp3EncoderTrim> trim;
};
InfoTag info_tag(FrameReader& reader, std::uint64_t offset, const FrameHeader& header) {
    const auto frame = reader.read(offset, header.bytes);
    require(!tag(frame, vbri_offset, "VBRI", 4), RemuxFailure::unsupported,
            "VBRI timeline requires separate validation");
    const auto xing_offset = mpeg_header_bytes + header.side_bytes;
    InfoTag result;
    if (!tag(frame, xing_offset, "Xing", 4) && !tag(frame, xing_offset, "Info", 4)) {
        return result;
    }
    result.present = true;
    require(std::all_of(frame.begin() + mpeg_header_bytes, frame.begin() + xing_offset,
                        [](std::uint8_t byte) { return byte == 0; }),
            RemuxFailure::unsupported, "MP3 Xing/Info frame requires empty side information");
    const auto flags = big_endian(frame, xing_offset + 4, 4);
    constexpr auto known_flags =
        xing_frames_flag | xing_bytes_flag | xing_toc_flag | xing_quality_flag;
    require((flags & ~known_flags) == 0 && (flags & xing_frames_flag) != 0,
            RemuxFailure::unsupported,
            "MP3 Xing/Info requires a known frame count and defined flags");
    auto cursor = xing_offset + 8;
    result.frames = big_endian(frame, cursor, 4);
    cursor += 4;
    if (flags & xing_bytes_flag) {
        result.bytes = big_endian(frame, cursor, 4);
        cursor += 4;
    }
    if (flags & xing_toc_flag) {
        require(cursor <= frame.size() && xing_toc_bytes <= frame.size() - cursor,
                RemuxFailure::malformed, "MP3 Xing TOC truncated");
        require(frame[cursor] == 0 &&
                    std::is_sorted(frame.begin() + cursor, frame.begin() + cursor + xing_toc_bytes),
                RemuxFailure::malformed, "MP3 Xing TOC is not monotonic");
        cursor += xing_toc_bytes; // Native scan, not this approximate TOC, provides locators.
    }
    if (flags & xing_quality_flag) {
        (void)big_endian(frame, cursor, 4);
        cursor += 4;
    }
    if (tag(frame, cursor, "LAME", 4) || tag(frame, cursor, "Lavc", 4) ||
        tag(frame, cursor, "Lavf", 4)) {
        require(cursor <= frame.size() && encoder_tag_bytes <= frame.size() - cursor,
                RemuxFailure::malformed, "MP3 encoder tag truncated");
        require((frame[cursor + encoder_revision_offset] >> 4) == 0, RemuxFailure::unsupported,
                "MP3 encoder tag revision unsupported");
        require(info_crc(frame, cursor + encoder_crc_offset) ==
                    big_endian(frame, cursor + encoder_crc_offset, 2),
                RemuxFailure::malformed, "MP3 encoder tag checksum mismatch");
        const auto packed = big_endian(frame, cursor + encoder_trim_offset, 3);
        result.trim = Mp3EncoderTrim{static_cast<std::uint16_t>(packed >> 12),
                                     static_cast<std::uint16_t>(packed & encoder_trim_mask)};
    }
    return result;
}
} // namespace

Mp3AudioInventory probe_mp3_audio(const MediaSource& source, const MediaReadContext& context) {
    FrameReader reader(source, context);
    const auto extent = audio_extent(reader);
    const auto first = frame_header(reader.read(extent.first, mpeg_header_bytes));
    require(first.bytes <= extent.second - extent.first, RemuxFailure::malformed,
            "MP3 initial frame truncated");
    const auto metadata = info_tag(reader, extent.first, first);
    Mp3AudioInventory result;
    result.sample_rate = first.rate;
    result.channels = first.channels;
    result.samples_per_frame = first.samples;
    result.audio_begin = extent.first;
    result.audio_end = extent.second;
    result.xing = metadata.present;
    result.encoder_trim = metadata.trim;
    std::uint64_t position = extent.first, physical_frames = 0, reservoir_available = 0;
    std::uint32_t first_audio_bitrate = 0;
    while (position < extent.second) {
        require(++physical_frames <= maximum_frames, RemuxFailure::too_large,
                "MP3 frame count bound");
        const auto header = frame_header(reader.read(position, mpeg_header_bytes));
        require(header.version == first.version && header.rate == first.rate &&
                    header.channels == first.channels,
                RemuxFailure::unsupported, "MP3 stream parameters changed");
        require(header.bytes <= extent.second - position, RemuxFailure::malformed,
                "MP3 final frame truncated");
        const auto side = reader.read(position + mpeg_header_bytes, header.side_bytes);
        // main_data_begin is a 9-bit MPEG-1 or 8-bit low-rate byte distance.
        const auto reservoir_back_bytes = static_cast<std::uint16_t>(
            header.version == 3 ? (std::uint16_t{side[0]} << 1) | (side[1] >> 7) : side[0]);
        require(reservoir_back_bytes <= reservoir_available, RemuxFailure::malformed,
                "MP3 reservoir points before source");
        if (!(physical_frames == 1 && metadata.present)) {
            if (first_audio_bitrate == 0) {
                first_audio_bitrate = header.bitrate;
            }
            result.variable_bitrate |= header.bitrate != first_audio_bitrate;
            if (result.seek_points.empty() ||
                result.coded_samples - result.seek_points.back().coded_sample >=
                    result.sample_rate) {
                require(result.seek_points.size() < maximum_seek_points, RemuxFailure::too_large,
                        "MP3 seek locator bound");
                result.seek_points.push_back(
                    {position, result.coded_samples, reservoir_back_bytes});
            }
            ++result.frame_count;
            result.coded_samples += header.samples;
            reservoir_available += header.bytes - mpeg_header_bytes - header.side_bytes;
        }
        position += header.bytes;
    }
    require(result.frame_count >= 2 && result.coded_samples < maximum_seconds * result.sample_rate,
            RemuxFailure::unsupported, "MP3 requires positive bounded multi-frame audio");
    require(!metadata.frames || *metadata.frames == result.frame_count, RemuxFailure::malformed,
            "MP3 Xing frame count disagrees with scan");
    require(!metadata.bytes || *metadata.bytes == result.audio_end - result.audio_begin,
            RemuxFailure::malformed, "MP3 Xing byte count disagrees with extent");
    if (result.encoder_trim) {
        const auto trim = std::uint64_t{result.encoder_trim->delay} + result.encoder_trim->padding;
        require(trim < result.coded_samples, RemuxFailure::malformed,
                "MP3 encoder trim consumes presentation");
        result.audible_samples = result.coded_samples - trim;
    }
    reader.check();
    return result;
}

Mp3SeekPoint locate_mp3_sample(const Mp3AudioInventory& inventory, std::uint64_t audible_sample) {
    if (inventory.seek_points.empty() || inventory.coded_samples == 0) {
        throw std::invalid_argument("MP3 inventory has no seek locators");
    }
    const auto delay = inventory.encoder_trim ? inventory.encoder_trim->delay : 0;
    const auto target = std::min(audible_sample, inventory.coded_samples - 1);
    const auto coded_target = std::min(inventory.coded_samples - 1, target + delay);
    const auto after =
        std::upper_bound(inventory.seek_points.begin(), inventory.seek_points.end(), coded_target,
                         [](std::uint64_t sample, const Mp3SeekPoint& point) {
                             return sample < point.coded_sample;
                         });
    return after == inventory.seek_points.begin() ? inventory.seek_points.front()
                                                  : *std::prev(after);
}
} // namespace send_airplay2::detail
