// SPDX-License-Identifier: Apache-2.0
#include "ogg_audio.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

namespace send_airplay2::detail {
namespace {
constexpr std::uint64_t maximum_source_bytes = 64ULL << 20;
constexpr std::size_t maximum_pages = 65536;
constexpr std::size_t maximum_callbacks = 1'048'576;
constexpr std::size_t maximum_packet_bytes = 1 << 20;
constexpr std::size_t page_header_bytes = 27;
constexpr std::size_t granule_offset = 6;
constexpr std::size_t serial_offset = 14;
constexpr std::size_t sequence_offset = 18;
constexpr std::size_t checksum_offset = 22;
constexpr std::size_t segment_count_offset = 26;
constexpr std::uint8_t continued_flag = 1, bos_flag = 2, eos_flag = 4;
constexpr std::uint64_t no_granule = std::numeric_limits<std::uint64_t>::max();
constexpr std::uint64_t maximum_seconds = 7 * 24 * 3600;
constexpr std::uint32_t output_rate = 48000;
constexpr std::uint32_t opus_rate = 48000;
constexpr std::size_t opus_identification_bytes = 19;
constexpr std::size_t opus_version_offset = 8, opus_channels_offset = 9;
constexpr std::size_t opus_pre_skip_offset = 10, opus_mapping_family_offset = 18;
constexpr std::size_t opus_streams_offset = 19, opus_coupled_offset = 20;
constexpr std::size_t opus_channel_map_offset = 21;
constexpr std::size_t vorbis_identification_bytes = 30;
constexpr std::size_t vorbis_version_offset = 7, vorbis_channels_offset = 11;
constexpr std::size_t vorbis_rate_offset = 12, vorbis_blocks_offset = 28;
constexpr std::size_t vorbis_framing_offset = 29;

void require(bool condition, RemuxFailure reason, const char* message) {
    if (!condition) {
        throw RemuxException(reason, message);
    }
}
std::uint64_t little_endian(const Bytes& bytes, std::size_t offset, std::size_t count) {
    require(offset <= bytes.size() && count <= bytes.size() - offset, RemuxFailure::malformed,
            "Ogg field truncated");
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < count; ++index) {
        value |= std::uint64_t{bytes[offset + index]} << (8 * index);
    }
    return value;
}
bool signature(const Bytes& bytes, std::size_t offset, const char* text, std::size_t length) {
    return offset <= bytes.size() && length <= bytes.size() - offset &&
           std::memcmp(bytes.data() + offset, text, length) == 0;
}
constexpr auto crc_table() {
    std::array<std::uint32_t, 256> table{};
    for (std::size_t index = 0; index < table.size(); ++index) {
        auto value = static_cast<std::uint32_t>(index) << 24;
        for (unsigned bit = 0; bit < 8; ++bit) {
            value = (value << 1) ^ ((value & 0x80000000U) ? 0x04c11db7U : 0U);
        }
        table[index] = value;
    }
    return table;
}
constexpr auto checksums = crc_table();
std::uint32_t page_crc(const Bytes& bytes) {
    std::uint32_t crc = 0;
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        // Ogg CRC has zero initial/final value and a zeroed checksum field.
        const auto byte =
            index >= checksum_offset && index < checksum_offset + 4 ? 0 : bytes[index];
        crc = (crc << 8) ^ checksums[((crc >> 24) ^ byte) & 255];
    }
    return crc;
}

/// Reads each physical page once, with bounded allocation and borrowed ownership.
class PageReader {
public:
    PageReader(const MediaSource& source, const MediaReadContext& context)
        : source_(source), context_(context) {
        if (!source.size || !source.read_at || !context.server_stopped ||
            !context.request_cancelled) {
            throw std::invalid_argument("Ogg probe requires source and context");
        }
        check();
        size = source.size();
        check();
        require(size > 0 && size <= maximum_source_bytes, RemuxFailure::too_large,
                "Ogg bounded scan requires source <=64 MiB");
    }
    PageReader(const PageReader&) = delete;
    PageReader& operator=(const PageReader&) = delete;
    PageReader(PageReader&&) = delete;
    PageReader& operator=(PageReader&&) = delete;
    void check() const {
        if (context_.should_stop()) {
            throw std::runtime_error("Ogg probe stopped");
        }
    }
    Bytes next() {
        require(++pages_ <= maximum_pages, RemuxFailure::too_large, "Ogg page count bound");
        auto page = read(page_header_bytes);
        require(signature(page, 0, "OggS", 4) && page[4] == 0 && (page[5] & ~7) == 0,
                RemuxFailure::malformed, "Ogg capture, version or flags invalid");
        const auto laces = read(page[segment_count_offset]);
        std::size_t payload_bytes = 0;
        for (auto lace : laces) {
            payload_bytes += lace;
        }
        const auto payload = read(payload_bytes);
        page.insert(page.end(), laces.begin(), laces.end());
        page.insert(page.end(), payload.begin(), payload.end());
        require(page_crc(page) == little_endian(page, checksum_offset, 4), RemuxFailure::malformed,
                "Ogg page checksum mismatch");
        check();
        return page;
    }
    std::uint64_t size = 0, position = 0;

private:
    Bytes read(std::size_t count) {
        check();
        require(position <= size && count <= size - position, RemuxFailure::malformed,
                "Ogg page truncated");
        Bytes bytes(count);
        std::size_t done = 0;
        while (done != count) {
            check();
            require(++calls_ <= maximum_callbacks, RemuxFailure::too_large, "Ogg callback bound");
            const auto capacity = std::min<std::size_t>(65536, count - done);
            const auto actual = source_.read_at(position, bytes.data() + done, capacity, context_);
            check();
            if (!actual || actual > capacity) {
                throw std::runtime_error("Ogg page read failed");
            }
            position += actual;
            done += actual;
        }
        return bytes;
    }
    const MediaSource& source_;
    const MediaReadContext& context_;
    std::size_t pages_ = 0, calls_ = 0;
};

/// Validate bounded length-prefixed comments without retaining labels or pictures.
void comments(const Bytes& packet, std::size_t offset, bool vorbis) {
    auto skip_string = [&] {
        const auto length = little_endian(packet, offset, 4);
        offset += 4;
        require(length <= packet.size() - offset, RemuxFailure::malformed,
                "Ogg comment string truncated");
        offset += static_cast<std::size_t>(length);
    };
    skip_string(); // Vendor.
    const auto count = little_endian(packet, offset, 4);
    offset += 4;
    require(count <= 65536, RemuxFailure::too_large, "Ogg comment count bound");
    for (std::uint64_t index = 0; index < count; ++index) {
        skip_string();
    }
    require(!vorbis || (offset + 1 == packet.size() && packet[offset] == 1),
            RemuxFailure::malformed, "Vorbis comment framing invalid");
    // Opus permits trailing comment padding. Metadata is not used as a timeline.
}
std::uint64_t channel_mask(std::uint64_t channels) {
    // Vorbis order and Opus families 0/1 have these conventional speaker sets.
    constexpr std::array<std::uint64_t, 8> masks{4, 3, 7, 0x33, 0x37, 0x3f, 0x70f, 0x63f};
    require(channels >= 1 && channels <= 8 && channels != 7, RemuxFailure::unsupported,
            "Ogg channel layout outside downmix subset");
    return masks[static_cast<std::size_t>(channels - 1)];
}
std::uint32_t opus_packet_samples(const Bytes& packet) {
    require(!packet.empty(), RemuxFailure::malformed, "empty Opus data packet");
    // RFC 6716 TOC config selects per-frame samples at 48 kHz; code selects count.
    const auto config = packet[0] >> 3;
    const auto samples = config >= 16        ? 120U << (config & 3)
                         : config >= 12      ? 480U << (config & 1)
                         : (config & 3) == 3 ? 2880U
                                             : 480U << (config & 3);
    const auto code = packet[0] & 3;
    require(code != 3 || packet.size() >= 2, RemuxFailure::malformed, "Opus frame count truncated");
    const auto frames = code == 0 ? 1U : code == 3 ? packet[1] & 63U : 2U;
    require(frames > 0 && frames * samples <= 5760, RemuxFailure::malformed,
            "Opus packet duration exceeds 120 ms");
    return frames * samples;
}

class PacketInventory {
public:
    void consume(const Bytes& packet) {
        if (packets_ == 0) {
            identification(packet);
        } else if (packets_ == 1) {
            require(
                signature(packet, opus_ ? 0 : 1, opus_ ? "OpusTags" : "vorbis", opus_ ? 8 : 6) &&
                    (opus_ || packet[0] == 3),
                RemuxFailure::malformed, "Ogg comment header missing");
            comments(packet, opus_ ? 8 : 7, !opus_);
        } else if (!opus_ && packets_ == 2) {
            require(packet.size() > 7 && packet[0] == 5 && signature(packet, 1, "vorbis", 6),
                    RemuxFailure::malformed, "Vorbis setup header missing");
            // Complete setup/codebooks are validated by the pinned decoder.
        } else {
            require(!packet.empty() && (opus_ || (packet[0] & 1) == 0), RemuxFailure::malformed,
                    "Ogg audio packet missing or unexpected header");
            ++audio_packets;
            if (opus_) {
                last_opus_samples = opus_packet_samples(packet);
                decoded_opus_samples += last_opus_samples;
            }
        }
        ++packets_;
    }
    bool headers_complete() const {
        return packets_ >= (opus_ ? 2U : 3U);
    }
    bool opus() const {
        return opus_;
    }
    SourceTrack track;
    std::uint64_t audio_packets = 0, decoded_opus_samples = 0;
    std::uint32_t last_opus_samples = 0, pre_skip = 0;

private:
    void identification(const Bytes& packet) {
        opus_ = signature(packet, 0, "OpusHead", 8);
        if (opus_) {
            require(packet.size() >= opus_identification_bytes && packet[opus_version_offset] == 1,
                    RemuxFailure::unsupported, "Opus identification version or length unsupported");
            track.codec = "OPUS";
            track.audio_channels = packet[opus_channels_offset];
            track.audio_channel_mask = channel_mask(track.audio_channels);
            track.audio_sample_rate = opus_rate; // Input-rate header is informational only.
            pre_skip = static_cast<std::uint32_t>(little_endian(packet, opus_pre_skip_offset, 2));
            const auto family = packet[opus_mapping_family_offset];
            require((family == 0 && track.audio_channels <= 2 &&
                     packet.size() == opus_identification_bytes) ||
                        (family == 1 &&
                         packet.size() == opus_channel_map_offset + track.audio_channels),
                    RemuxFailure::unsupported, "Opus channel mapping family unsupported");
            if (family == 1) {
                const auto streams = packet[opus_streams_offset],
                           coupled = packet[opus_coupled_offset];
                require(streams > 0 && streams <= 8 && coupled <= streams && streams + coupled <= 8,
                        RemuxFailure::unsupported, "Opus stream map outside subset");
                for (std::size_t channel = 0; channel < track.audio_channels; ++channel) {
                    require(packet[opus_channel_map_offset + channel] < streams + coupled,
                            RemuxFailure::unsupported, "Opus channel map index unsupported");
                }
            }
            track.seek_preroll_ns = 80'000'000; // RFC 7845 minimum; decoder uses >=1 second.
        } else {
            require(packet.size() == vorbis_identification_bytes && packet[0] == 1 &&
                        signature(packet, 1, "vorbis", 6),
                    RemuxFailure::unsupported, "Ogg codec identification unsupported");
            require(little_endian(packet, vorbis_version_offset, 4) == 0 &&
                        packet[vorbis_framing_offset] == 1,
                    RemuxFailure::unsupported,
                    "Vorbis identification version or framing unsupported");
            const auto small_block = packet[vorbis_blocks_offset] & 15,
                       large_block = packet[vorbis_blocks_offset] >> 4;
            require(small_block >= 6 && small_block <= large_block && large_block <= 13,
                    RemuxFailure::malformed, "Vorbis block sizes invalid");
            track.codec = "VORBIS";
            track.audio_channels = packet[vorbis_channels_offset];
            track.audio_channel_mask = channel_mask(track.audio_channels);
            track.audio_sample_rate =
                static_cast<double>(little_endian(packet, vorbis_rate_offset, 4));
        }
        require(track.audio_sample_rate >= 8000 && track.audio_sample_rate <= 192000,
                RemuxFailure::unsupported, "Ogg sample rate outside subset");
        track.timescale = static_cast<std::uint32_t>(track.audio_sample_rate);
        track.id = 1;
        track.kind = SourceTrackKind::audio;
        track.default_track = true;
        track.configuration = packet;
    }
    bool opus_ = false;
    std::size_t packets_ = 0;
};
} // namespace

MediaTrackInventory probe_ogg_audio_tracks(const MediaSource& source,
                                           const MediaReadContext& context) {
    PageReader reader(source, context);
    PacketInventory packets;
    Bytes pending;
    std::uint64_t sequence = 0, previous_granule = 0;
    std::uint32_t serial = 0;
    bool eos = false;
    while (reader.position < reader.size) {
        require(!eos, RemuxFailure::unsupported, "Ogg chaining or trailing bytes unsupported");
        const auto page = reader.next();
        const auto flags = page[5];
        const auto page_serial = static_cast<std::uint32_t>(little_endian(page, serial_offset, 4));
        require(sequence == 0 ? flags == bos_flag : !(flags & bos_flag), RemuxFailure::unsupported,
                "Ogg requires one beginning of stream");
        if (sequence == 0) {
            serial = page_serial;
        }
        require(page_serial == serial, RemuxFailure::unsupported,
                "Ogg multiplexed serial unsupported");
        require(little_endian(page, sequence_offset, 4) == sequence++, RemuxFailure::malformed,
                "Ogg page sequence gap or duplicate");
        require(bool(flags & continued_flag) == !pending.empty(), RemuxFailure::malformed,
                "Ogg packet continuation mismatch");
        const auto laces = page[segment_count_offset];
        require(laces != 0, RemuxFailure::unsupported, "Ogg empty pages outside subset");
        const auto audio_before = packets.audio_packets;
        const bool headers_before = packets.headers_complete();
        std::size_t payload = page_header_bytes + laces, completed = 0;
        for (std::size_t index = 0; index < laces; ++index) {
            const auto length = page[page_header_bytes + index];
            require(length <= maximum_packet_bytes - pending.size(), RemuxFailure::too_large,
                    "Ogg packet exceeds 1 MiB");
            pending.insert(pending.end(), page.begin() + payload, page.begin() + payload + length);
            payload += length;
            if (length < 255) {
                packets.consume(pending);
                pending.clear();
                ++completed;
            }
        }
        require(headers_before || !packets.headers_complete() || pending.empty(),
                RemuxFailure::unsupported, "Ogg audio must start on a fresh page");
        const auto granule = little_endian(page, granule_offset, 8);
        eos = (flags & eos_flag) != 0;
        if (sequence == 1) {
            require(completed == 1 && pending.empty() && granule == 0, RemuxFailure::malformed,
                    "Ogg BOS must contain only identification");
        } else if (packets.audio_packets == audio_before) {
            require(!eos && granule == (completed ? 0 : no_granule), RemuxFailure::malformed,
                    "Ogg header or continued-page granule invalid");
        } else {
            require(headers_before && granule != no_granule &&
                        granule <=
                            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) &&
                        granule >= previous_granule,
                    RemuxFailure::malformed, "Ogg audio granule invalid or regressed");
            if (packets.opus()) {
                // Admit zero-origin Opus only. EOS may trim at most the last packet.
                require(eos ? granule <= packets.decoded_opus_samples &&
                                  granule > packets.decoded_opus_samples - packets.last_opus_samples
                            : granule == packets.decoded_opus_samples,
                        RemuxFailure::unsupported, "Opus origin, gap or end trim outside subset");
            }
            previous_granule = granule;
        }
        require(!eos || (pending.empty() && packets.audio_packets > audio_before &&
                         reader.position == reader.size),
                RemuxFailure::unsupported, "Ogg EOS must finish audio at physical EOF");
    }
    require(eos && pending.empty() && packets.headers_complete() && packets.audio_packets != 0 &&
                previous_granule > packets.pre_skip,
            RemuxFailure::malformed, "Ogg end or positive audible duration missing");
    packets.track.standalone_sample_frames = previous_granule - packets.pre_skip;
    require(packets.track.standalone_sample_frames < maximum_seconds * packets.track.timescale,
            RemuxFailure::unsupported, "Ogg duration exceeds seven-day bound");
    const auto frames = packets.track.standalone_sample_frames;
    const auto rate = packets.track.timescale;
    const auto output_frames =
        frames / rate * output_rate + (frames % rate * output_rate + rate / 2) / rate;
    require(output_frames > 0 && output_frames < maximum_seconds * output_rate,
            RemuxFailure::unsupported, "Ogg rounded duration outside presentation bound");
    reader.check();
    MediaTrackInventory result;
    result.container = SourceContainer::ogg;
    result.tracks.push_back(std::move(packets.track));
    return result;
}
} // namespace send_airplay2::detail
