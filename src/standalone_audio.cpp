// SPDX-License-Identifier: Apache-2.0
#include "standalone_audio.h"
#include "ogg_audio.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace send_airplay2::detail {
namespace {
constexpr std::uint64_t maximum_source_bytes = 1ULL << 40;
constexpr std::uint64_t maximum_read_bytes = 128ULL << 20;
constexpr std::uint64_t maximum_flac_metadata_bytes = 16ULL << 20;
constexpr std::size_t maximum_read_calls = 1'048'576;
constexpr std::size_t maximum_header_count = 4096;
constexpr std::size_t maximum_seek_points = 65536;
constexpr std::uint64_t maximum_seconds = 7 * 24 * 3600;
constexpr std::uint32_t output_rate = 48000;
constexpr std::size_t riff_header_bytes = 12;
constexpr std::size_t chunk_header_bytes = 8;
constexpr std::size_t wave_format_bytes = 16;
constexpr std::size_t extensible_format_bytes = 40;
constexpr std::size_t rf64_size_bytes = 28;
constexpr std::size_t flac_streaminfo_bytes = 34;
constexpr std::size_t flac_clock_fields_offset = 10;
constexpr unsigned flac_sample_count_bits = 36;
constexpr unsigned flac_bit_depth_bits = 5;
constexpr unsigned flac_channel_count_bits = 3;
constexpr unsigned flac_bit_depth_shift = flac_sample_count_bits;
constexpr unsigned flac_channel_count_shift = flac_bit_depth_shift + flac_bit_depth_bits;
constexpr unsigned flac_sample_rate_shift = flac_channel_count_shift + flac_channel_count_bits;
constexpr std::uint64_t flac_sample_count_mask = (1ULL << flac_sample_count_bits) - 1;
constexpr std::size_t wave_extension_offset = wave_format_bytes;
constexpr std::size_t wave_valid_bits_offset = wave_extension_offset + 2;
constexpr std::size_t wave_channel_mask_offset = wave_valid_bits_offset + 2;
constexpr std::size_t wave_subtype_guid_offset = wave_channel_mask_offset + 4;
constexpr std::size_t wave_subtype_tail_offset = wave_subtype_guid_offset + 4;
constexpr std::size_t flac_seek_point_bytes = 18;
constexpr std::uint32_t oversized_riff_chunk = 0xffffffffU;

void require(bool condition, RemuxFailure reason, const char* message) {
    if (!condition) {
        throw RemuxException(reason, message);
    }
}
std::uint64_t read_little_endian(const Bytes& bytes, std::size_t offset, std::size_t count) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < count; ++index) {
        value |= std::uint64_t{bytes.at(offset + index)} << (8 * index);
    }
    return value;
}
std::uint64_t read_big_endian(const Bytes& bytes, std::size_t offset, std::size_t count) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < count; ++index) {
        value = (value << 8) | bytes.at(offset + index);
    }
    return value;
}
bool tag(const Bytes& bytes, std::size_t offset, const char* name) {
    return offset <= bytes.size() && bytes.size() - offset >= 4 &&
           std::memcmp(bytes.data() + offset, name, 4) == 0;
}
class HeaderReader {
public:
    HeaderReader(const MediaSource& source, const MediaReadContext& context)
        : source_(source), context_(context) {
        if (!source.size || !source.read_at || !context.server_stopped ||
            !context.request_cancelled) {
            throw std::invalid_argument("standalone audio probe requires source and context");
        }
        check();
        size = source_.size();
        check();
        require(size > 0 && size <= maximum_source_bytes, RemuxFailure::too_large,
                "standalone audio source size bound");
    }
    HeaderReader(const HeaderReader&) = delete;
    HeaderReader& operator=(const HeaderReader&) = delete;
    HeaderReader(HeaderReader&&) = delete;
    HeaderReader& operator=(HeaderReader&&) = delete;
    void check() const {
        if (context_.should_stop()) {
            throw std::runtime_error("standalone audio probe stopped");
        }
    }
    Bytes read(std::uint64_t offset, std::size_t length) {
        check();
        require(offset <= size && length <= size - offset, RemuxFailure::malformed,
                "standalone audio header outside source");
        require(length <= maximum_read_bytes - requested_, RemuxFailure::too_large,
                "standalone audio read budget");
        requested_ += length;
        Bytes bytes(length);
        std::size_t done = 0;
        while (done != length) {
            check();
            require(++calls_ <= maximum_read_calls, RemuxFailure::too_large,
                    "standalone audio callback budget");
            const auto capacity = std::min<std::size_t>(65536, length - done);
            const auto count =
                source_.read_at(offset + done, bytes.data() + done, capacity, context_);
            check();
            if (!count || count > capacity) {
                throw std::runtime_error("standalone audio header read failed");
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
void validate_format(SourceTrack& track) {
    const auto rate = static_cast<std::uint64_t>(track.audio_sample_rate);
    require(rate >= 8000 && rate <= 192000 && track.audio_channels >= 1 &&
                track.audio_channels <= 8,
            RemuxFailure::unsupported, "standalone audio rate or channels unsupported");
    require(track.standalone_sample_frames > 0 &&
                track.standalone_sample_frames < maximum_seconds * rate,
            RemuxFailure::unsupported, "standalone audio requires known duration below seven days");
    const auto frames = track.standalone_sample_frames;
    const auto duration =
        frames / rate * output_rate + (frames % rate * output_rate + rate / 2) / rate;
    require(duration > 0 && duration < maximum_seconds * output_rate, RemuxFailure::unsupported,
            "standalone audio duration not representable");
    constexpr std::uint64_t supported_speakers = 0x63f; // FL/FR/FC/LFE/BL/BR/SL/SR.
    require(track.audio_channel_mask != 0 && (track.audio_channel_mask & ~supported_speakers) == 0,
            RemuxFailure::unsupported, "standalone audio speaker layout outside downmix subset");
    track.timescale = static_cast<std::uint32_t>(rate);
}
MediaTrackInventory inventory(SourceContainer container, SourceTrack track) {
    validate_format(track);
    track.id = 1;
    track.kind = SourceTrackKind::audio;
    track.default_track = true;
    MediaTrackInventory result;
    result.container = container;
    result.tracks.push_back(std::move(track));
    return result;
}
// WAVEFORMATEX's little-endian base fields: tag/channels/rate/byte-rate/
// block-align/bits at offsets 0/2/4/8/12/14. Extension offsets are named above.
SourceTrack wave_format(const Bytes& bytes) {
    require(bytes.size() >= wave_format_bytes, RemuxFailure::malformed, "WAVE format truncated");
    auto format = read_little_endian(bytes, 0, 2);
    SourceTrack track;
    track.audio_channels = read_little_endian(bytes, 2, 2);
    track.audio_sample_rate = static_cast<double>(read_little_endian(bytes, 4, 4));
    track.audio_bit_depth = read_little_endian(bytes, 14, 2);
    if (format == 0xfffe) {
        require(bytes.size() == extensible_format_bytes &&
                    read_little_endian(bytes, wave_extension_offset, 2) == 22,
                RemuxFailure::unsupported, "WAVE extensible shape unsupported");
        require(read_little_endian(bytes, wave_valid_bits_offset, 2) == track.audio_bit_depth,
                RemuxFailure::unsupported, "WAVE reduced valid-bit packing unsupported");
        // On-disk GUID: first DWORD is the PCM/IEEE subtype, remaining bytes fixed.
        const std::array<std::uint8_t, 12> guid_tail{0, 0,    0x10, 0,    0x80, 0,
                                                     0, 0xaa, 0,    0x38, 0x9b, 0x71};
        require(std::equal(guid_tail.begin(), guid_tail.end(),
                           bytes.begin() + wave_subtype_tail_offset),
                RemuxFailure::unsupported, "WAVE subtype GUID unsupported");
        format = read_little_endian(bytes, wave_subtype_guid_offset, 4);
        track.audio_channel_mask = read_little_endian(bytes, wave_channel_mask_offset, 4);
    } else {
        require(bytes.size() == 16 || (bytes.size() == 18 &&
                                       read_little_endian(bytes, wave_extension_offset, 2) == 0),
                RemuxFailure::unsupported, "WAVE format extension unsupported");
        require(track.audio_channels == 1 || track.audio_channels == 2, RemuxFailure::unsupported,
                "WAVE surround requires explicit mask");
        track.audio_channel_mask = track.audio_channels == 1 ? 4 : 3;
    }
    require((format == 1 && (track.audio_bit_depth == 16 || track.audio_bit_depth == 24 ||
                             track.audio_bit_depth == 32)) ||
                (format == 3 && (track.audio_bit_depth == 32 || track.audio_bit_depth == 64)),
            RemuxFailure::unsupported, "WAVE codec or packing unsupported");
    track.codec = format == 1 ? "PCM_INT_LE" : "PCM_FLOAT_LE";
    const auto alignment = track.audio_channels * (track.audio_bit_depth / 8);
    require(alignment > 0 && read_little_endian(bytes, 12, 2) == alignment &&
                read_little_endian(bytes, 8, 4) ==
                    static_cast<std::uint64_t>(track.audio_sample_rate) * alignment,
            RemuxFailure::malformed, "WAVE block alignment or byte rate mismatch");
    std::uint64_t mask = track.audio_channel_mask, channels = 0;
    while (mask) {
        channels += mask & 1;
        mask >>= 1;
    }
    require(channels == track.audio_channels, RemuxFailure::unsupported,
            "WAVE speaker mask count mismatch");
    return track;
}
MediaTrackInventory wave_inventory(HeaderReader& reader) {
    const auto header = reader.read(0, riff_header_bytes);
    const bool rf64 = tag(header, 0, "RF64");
    require(tag(header, 8, "WAVE"), RemuxFailure::unsupported, "RIFF input is not WAVE");
    require(rf64 ? read_little_endian(header, 4, 4) == oversized_riff_chunk
                 : read_little_endian(header, 4, 4) == reader.size - 8,
            RemuxFailure::malformed, "WAVE RIFF size mismatch");
    SourceTrack track;
    bool found_format = false, found_data = false, found_sizes = false, found_fact = false;
    std::uint64_t data_bytes = 0, rf64_data_bytes = 0, rf64_frames = 0, fact_frames = 0;
    auto offset = std::uint64_t{riff_header_bytes};
    std::size_t chunks = 0;
    while (offset < reader.size) {
        reader.check();
        require(++chunks <= maximum_header_count, RemuxFailure::too_large,
                "WAVE chunk count bound");
        const auto chunk = reader.read(offset, chunk_header_bytes);
        auto length = read_little_endian(chunk, 4, 4);
        const auto payload = offset + chunk_header_bytes;
        if (tag(chunk, 0, "ds64")) {
            require(rf64 && !found_sizes && offset == riff_header_bytes &&
                        length == rf64_size_bytes,
                    RemuxFailure::unsupported, "RF64 size table shape unsupported");
            const auto sizes = reader.read(payload, rf64_size_bytes);
            require(read_little_endian(sizes, 0, 8) == reader.size - 8 &&
                        read_little_endian(sizes, 24, 4) == 0,
                    RemuxFailure::malformed, "RF64 root size or extra size table unsupported");
            rf64_data_bytes = read_little_endian(sizes, 8, 8);
            rf64_frames = read_little_endian(sizes, 16, 8);
            found_sizes = true;
        } else if (tag(chunk, 0, "fmt ")) {
            require(!found_format && !found_data && length <= extensible_format_bytes,
                    RemuxFailure::unsupported, "WAVE format ordering or shape unsupported");
            track = wave_format(reader.read(payload, static_cast<std::size_t>(length)));
            found_format = true;
        } else if (tag(chunk, 0, "data")) {
            require(found_format && !found_data && (!rf64 || found_sizes),
                    RemuxFailure::unsupported, "WAVE data ordering or duplication unsupported");
            if (rf64) {
                require(length == oversized_riff_chunk, RemuxFailure::malformed,
                        "RF64 data size marker missing");
                length = rf64_data_bytes;
            }
            data_bytes = length;
            found_data = true;
        } else if (tag(chunk, 0, "fact")) {
            require(!found_fact && length == 4, RemuxFailure::unsupported,
                    "WAVE fact shape unsupported");
            fact_frames = read_little_endian(reader.read(payload, 4), 0, 4);
            found_fact = true;
        } else {
            require(length != oversized_riff_chunk, RemuxFailure::unsupported,
                    "WAVE oversized unknown chunk unsupported");
        }
        require(payload <= reader.size && length <= reader.size - payload, RemuxFailure::malformed,
                "WAVE chunk exceeds source");
        offset = payload + length;
        if (length & 1) {
            require(offset < reader.size, RemuxFailure::malformed, "WAVE chunk pad missing");
            ++offset;
        }
    }
    require(found_format && found_data && (!rf64 || found_sizes), RemuxFailure::malformed,
            "WAVE required chunks missing");
    const auto alignment = track.audio_channels * (track.audio_bit_depth / 8);
    require(data_bytes % alignment == 0, RemuxFailure::malformed, "WAVE partial audio frame");
    track.standalone_sample_frames = data_bytes / alignment;
    // RF64's sentinel fact count refers to the 64-bit ds64 sampleCount.
    const auto effective_fact_frames =
        rf64 && fact_frames == oversized_riff_chunk ? rf64_frames : fact_frames;
    require((!rf64 || rf64_frames == track.standalone_sample_frames) &&
                (!found_fact || effective_fact_frames == track.standalone_sample_frames),
            RemuxFailure::malformed, "WAVE sample count disagrees with packed data");
    return inventory(SourceContainer::wave, std::move(track));
}
MediaTrackInventory flac_inventory(HeaderReader& reader) {
    SourceTrack track;
    track.codec = "FLAC";
    std::uint64_t offset = 4;
    bool last = false, found_seek = false;
    std::size_t blocks = 0;
    std::vector<std::pair<std::uint64_t, std::uint64_t>> seek_points;
    while (!last) {
        require(++blocks <= maximum_header_count, RemuxFailure::too_large,
                "FLAC metadata block count bound");
        const auto header = reader.read(offset, 4);
        const auto type = header[0] & 0x7f;
        last = (header[0] & 0x80) != 0;
        const auto length = read_big_endian(header, 1, 3);
        offset += 4;
        require(offset <= maximum_flac_metadata_bytes &&
                    length <= maximum_flac_metadata_bytes - offset,
                RemuxFailure::too_large, "FLAC metadata size bound");
        require(length <= reader.size - offset, RemuxFailure::malformed,
                "FLAC metadata exceeds source");
        if (blocks == 1) {
            require(type == 0 && length == flac_streaminfo_bytes, RemuxFailure::malformed,
                    "FLAC first block must be STREAMINFO");
            const auto info = reader.read(offset, flac_streaminfo_bytes);
            require(read_big_endian(info, 0, 2) >= 16 &&
                        read_big_endian(info, 2, 2) >= read_big_endian(info, 0, 2),
                    RemuxFailure::malformed, "FLAC block-size range malformed");
            // STREAMINFO packs rate/channels-1/depth-1/total samples into one big-endian word.
            const auto packed = read_big_endian(info, flac_clock_fields_offset, 8);
            track.audio_sample_rate = static_cast<double>(packed >> flac_sample_rate_shift);
            track.audio_channels =
                ((packed >> flac_channel_count_shift) & ((1U << flac_channel_count_bits) - 1)) + 1;
            track.audio_bit_depth =
                ((packed >> flac_bit_depth_shift) & ((1U << flac_bit_depth_bits) - 1)) + 1;
            track.standalone_sample_frames = packed & flac_sample_count_mask;
            require(track.audio_bit_depth == 16 || track.audio_bit_depth == 24,
                    RemuxFailure::unsupported, "FLAC bit depth outside standalone subset");
            constexpr std::array<std::uint64_t, 8> layouts{4, 3, 7, 0x33, 0x37, 0x3f, 0x70f, 0x63f};
            // STREAMINFO's three-bit channel field guarantees a count in 1..8.
            track.audio_channel_mask =
                layouts.at(static_cast<std::size_t>(track.audio_channels - 1));
        } else if (type == 3) {
            require(!found_seek && length % flac_seek_point_bytes == 0 &&
                        length / flac_seek_point_bytes <= maximum_seek_points,
                    RemuxFailure::unsupported, "FLAC seek table shape unsupported");
            found_seek = true;
            bool placeholder = false;
            for (std::uint64_t point = 0; point < length; point += flac_seek_point_bytes) {
                const auto bytes = reader.read(offset + point, flac_seek_point_bytes);
                const auto sample = read_big_endian(bytes, 0, 8),
                           position = read_big_endian(bytes, 8, 8);
                if (sample == std::numeric_limits<std::uint64_t>::max()) {
                    placeholder = true;
                    continue;
                }
                require(!placeholder && sample < track.standalone_sample_frames &&
                            read_big_endian(bytes, 16, 2) > 0 &&
                            (seek_points.empty() || (sample > seek_points.back().first &&
                                                     position > seek_points.back().second)),
                        RemuxFailure::malformed, "FLAC seek coordinates malformed");
                seek_points.emplace_back(sample, position);
            }
        } else {
            require(blocks != 1 && type >= 1 && type <= 5, RemuxFailure::unsupported,
                    "FLAC metadata type outside standalone subset");
        }
        offset += length;
    }
    require(offset < reader.size, RemuxFailure::malformed, "FLAC has no frame data");
    for (const auto& point : seek_points) {
        require(point.second < reader.size - offset, RemuxFailure::malformed,
                "FLAC seek offset outside frame data");
    }
    const auto sync = reader.read(offset, 2);
    require(sync[0] == 0xff && (sync[1] & 0xfe) == 0xf8, RemuxFailure::malformed,
            "FLAC first frame sync missing");
    return inventory(SourceContainer::flac, std::move(track));
}
} // namespace
bool is_standalone_audio(SourceContainer container) noexcept {
    return container == SourceContainer::wave || container == SourceContainer::flac ||
           container == SourceContainer::ogg;
}
MediaTrackInventory probe_audio_only_tracks(const MediaSource& source,
                                            const MediaReadContext& context) {
    HeaderReader reader(source, context);
    const auto signature = reader.read(0, 4);
    if (tag(signature, 0, "fLaC")) {
        return flac_inventory(reader);
    }
    if (tag(signature, 0, "RIFF") || tag(signature, 0, "RF64")) {
        return wave_inventory(reader);
    }
    auto snapshot = source;
    snapshot.size = [size = reader.size] { return size; };
    if (tag(signature, 0, "OggS")) {
        return probe_ogg_audio_tracks(snapshot, context);
    }
    return probe_media_tracks(snapshot, context);
}
} // namespace send_airplay2::detail
