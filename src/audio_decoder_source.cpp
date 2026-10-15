// SPDX-License-Identifier: Apache-2.0
#include "audio_decoder_source.h"
#include "media_track_inventory.h"
#include "mkv_demux.h"
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/samplefmt.h>
#include <libswresample/swresample.h>
}
#include <algorithm>
#include <array>
#include <charconv>
#include <cerrno>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <exception>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace send_airplay2::detail {
namespace {
constexpr std::uint64_t maximum_source_bytes = 1ULL << 40;
constexpr std::uint64_t maximum_read_bytes = 256ULL << 20;
constexpr std::size_t maximum_read_calls = 1'048'576;
constexpr int io_buffer_bytes = 32768;
constexpr int maximum_frame_samples = 65536;
constexpr int output_rate = AudioDecoderSource::output_rate;
constexpr std::uint32_t maximum_window_samples = 10 * AudioDecoderSource::output_rate;
constexpr std::uint64_t maximum_input_samples = 20 * AudioDecoderSource::output_rate;
constexpr std::uint64_t maximum_presentation_samples = 7ULL * 24 * 60 * 60 * output_rate;
constexpr AVRational output_clock{1, AudioDecoderSource::output_rate};
constexpr AVRational nanosecond_clock{1, 1'000'000'000};

[[noreturn]] void fail(AudioDecodeFailure reason, const char* message) {
    throw AudioDecodeException(reason, message);
}
void require(bool condition, AudioDecodeFailure reason, const char* message) {
    if (!condition) {
        fail(reason, message);
    }
}
void checked(int result) {
    require(result >= 0, AudioDecodeFailure::codec_error, "audio codec operation failed");
}
void check_stop(const MediaReadContext& context) {
    require(context.server_stopped && context.request_cancelled, AudioDecodeFailure::source_error,
            "audio source cancellation context is invalid");
    require(!context.should_stop(), AudioDecodeFailure::stopped,
            "audio source cancelled or expired");
}
template <typename T, void (*Free)(T**)> struct AvDeleter {
    void operator()(T* value) const noexcept {
        Free(&value);
    }
};
using Format = std::unique_ptr<AVFormatContext, AvDeleter<AVFormatContext, avformat_close_input>>;
using Codec = std::unique_ptr<AVCodecContext, AvDeleter<AVCodecContext, avcodec_free_context>>;
using Frame = std::unique_ptr<AVFrame, AvDeleter<AVFrame, av_frame_free>>;
using Packet = std::unique_ptr<AVPacket, AvDeleter<AVPacket, av_packet_free>>;
using Resampler = std::unique_ptr<SwrContext, AvDeleter<SwrContext, swr_free>>;
struct IoDeleter {
    void operator()(AVIOContext* value) const noexcept {
        if (value) {
            av_freep(&value->buffer);
            avio_context_free(&value);
        }
    }
};
using Io = std::unique_ptr<AVIOContext, IoDeleter>;

void validate_backend() {
    const std::string_view configuration(avcodec_configuration());
    const std::string_view license(avcodec_license());
    // Header-prefix parser analysis relies on the pinned implementation. Reject
    // runtime component drift before using that version-specific contract.
    require(avcodec_version() == LIBAVCODEC_VERSION_INT &&
                avformat_version() == LIBAVFORMAT_VERSION_INT &&
                avutil_version() == LIBAVUTIL_VERSION_INT &&
                swresample_version() == LIBSWRESAMPLE_VERSION_INT &&
                license == "LGPL version 2.1 or later" && license == avformat_license() &&
                license == avutil_license() && license == swresample_license() &&
                configuration == avformat_configuration() &&
                configuration == avutil_configuration() &&
                configuration == swresample_configuration() &&
                configuration.find("--disable-gpl") != std::string_view::npos &&
                configuration.find("--disable-nonfree") != std::string_view::npos &&
                configuration.find("--disable-network") != std::string_view::npos,
            AudioDecodeFailure::unsupported, "audio decoder backend profile is unsupported");
}

/// Contains exceptions inside FFmpeg callbacks; checked again on the C++ side.
struct Reader {
    MediaSource source;
    std::uint64_t size = 0;
    std::uint64_t position = 0;
    std::uint64_t requested_bytes = 0;
    std::size_t calls = 0;
    const MediaReadContext* context = nullptr;
    std::exception_ptr error;
    static constexpr std::size_t header_cache_bytes = 1024;
    static constexpr std::size_t large_packet_header_page_bytes = 256;
    struct HeaderPage {
        std::array<std::uint8_t, header_cache_bytes> bytes{};
        std::uint64_t offset = 0;
        std::size_t length = 0;
    };
    std::array<HeaderPage, 2> header_pages{};
    std::size_t header_read_ahead = large_packet_header_page_bytes;

    void check() const {
        if (error) {
            std::rethrow_exception(error);
        }
        require(context != nullptr, AudioDecodeFailure::source_error,
                "audio source operation has no context");
        check_stop(*context);
    }
    /// Direct indexed/header reads share the operation's callback and requested-
    /// byte budgets with FFmpeg AVIO. They never move its buffered position.
    void read_exact(std::uint64_t offset, std::uint8_t* output, std::size_t length) {
        require(offset <= size && length <= size - offset, AudioDecodeFailure::malformed,
                "indexed audio read exceeds source");
        std::size_t done = 0;
        while (done < length) {
            check();
            const auto capacity = std::min<std::size_t>(length - done, io_buffer_bytes);
            require(++calls <= maximum_read_calls &&
                        capacity <= maximum_read_bytes - requested_bytes,
                    AudioDecodeFailure::too_large, "audio source read budget exceeded");
            requested_bytes += capacity;
            const auto actual = source.read_at(offset + done, output + done, capacity, *context);
            check();
            require(actual > 0 && actual <= capacity, AudioDecodeFailure::source_error,
                    "indexed audio source returned an invalid read length");
            done += actual;
        }
    }
    /// Shared small read-ahead for container and codec headers. Co-located tiny
    /// units consume one host read rather than a callback per parser invocation.
    /// Every refill is charged to the unchanged operation budgets; cache hits
    /// still check cancellation. Larger complete packets use direct reads.
    void read_header(std::uint64_t offset, std::uint8_t* output, std::size_t length) {
        check();
        require(offset <= size && length <= size - offset, AudioDecodeFailure::malformed,
                "indexed audio header exceeds source");
        if (length > header_read_ahead) {
            read_exact(offset, output, length);
            return;
        }
        while (length != 0) {
            check();
            const auto page_number = offset / header_read_ahead;
            const auto page_offset = page_number * header_read_ahead;
            auto& page = header_pages[page_number % header_pages.size()];
            if (page.length == 0 || page.offset != page_offset) {
                page.offset = page_offset;
                page.length = static_cast<std::size_t>(
                    std::min<std::uint64_t>(header_read_ahead, size - page_offset));
                read_exact(page.offset, page.bytes.data(), page.length);
            }
            const auto within = static_cast<std::size_t>(offset - page_offset);
            const auto count = std::min(length, page.length - within);
            std::memcpy(output, page.bytes.data() + within, count);
            offset += count;
            output += count;
            length -= count;
        }
    }
    static int read(void* opaque, std::uint8_t* output, int capacity) noexcept {
        auto& reader = *static_cast<Reader*>(opaque);
        try {
            reader.check();
            require(capacity > 0, AudioDecodeFailure::source_error, "invalid decoder read");
            if (reader.position == reader.size) {
                return AVERROR_EOF;
            }
            const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(
                reader.size - reader.position, std::min(capacity, io_buffer_bytes)));
            require(++reader.calls <= maximum_read_calls &&
                        count <= maximum_read_bytes - reader.requested_bytes,
                    AudioDecodeFailure::too_large, "audio source read budget exceeded");
            reader.requested_bytes += count;
            const auto actual =
                reader.source.read_at(reader.position, output, count, *reader.context);
            reader.check();
            require(actual > 0 && actual <= count, AudioDecodeFailure::source_error,
                    "audio source returned an invalid read length");
            reader.position += actual;
            return static_cast<int>(actual);
        } catch (...) {
            reader.error = std::current_exception();
            return AVERROR(EIO);
        }
    }
    static std::int64_t seek(void* opaque, std::int64_t offset, int whence) noexcept {
        auto& reader = *static_cast<Reader*>(opaque);
        try {
            reader.check();
            if ((whence & ~AVSEEK_FORCE) == AVSEEK_SIZE) {
                return static_cast<std::int64_t>(reader.size);
            }
            const auto origin = whence & ~AVSEEK_FORCE;
            const auto base = origin == SEEK_CUR   ? static_cast<std::int64_t>(reader.position)
                              : origin == SEEK_END ? static_cast<std::int64_t>(reader.size)
                                                   : 0;
            require(origin == SEEK_SET || origin == SEEK_CUR || origin == SEEK_END,
                    AudioDecodeFailure::source_error, "invalid decoder seek origin");
            require(offset >= -base && offset <= static_cast<std::int64_t>(reader.size) - base,
                    AudioDecodeFailure::source_error, "decoder seek outside source");
            reader.position = static_cast<std::uint64_t>(base + offset);
            return static_cast<std::int64_t>(reader.position);
        } catch (...) {
            reader.error = std::current_exception();
            return AVERROR(EIO);
        }
    }
    static int interrupt(void* opaque) noexcept {
        try {
            static_cast<Reader*>(opaque)->check();
            return 0;
        } catch (...) {
            return 1;
        }
    }
};
/// Lifetime is shorter than Reader; all owners clean up with a live Reader.
struct Operation {
    Reader& reader;
    Operation(Reader& input, const MediaReadContext& context) : reader(input) {
        check_stop(context);
        reader.context = &context;
        reader.error = {};
        reader.calls = 0;
        reader.requested_bytes = 0;
        for (auto& page : reader.header_pages) {
            page.length = 0;
        }
    }
    ~Operation() {
        reader.context = nullptr;
    }
    Operation(const Operation&) = delete;
    Operation& operator=(const Operation&) = delete;
    Operation(Operation&&) = delete;
    Operation& operator=(Operation&&) = delete;
};

/// Bounded basic header validation complements FFmpeg's codec-specific parsing.
/// Matching a fourcc/CodecID alone must not bless unrelated private data.
void validate_flac_private(const Bytes& data) {
    constexpr std::size_t metadata_header_bytes = 8;
    constexpr std::size_t streaminfo_bytes = 34;
    require(data.size() >= metadata_header_bytes + streaminfo_bytes &&
                std::memcmp(data.data(), "fLaC", 4) == 0 && (data[4] & 0x7f) == 0 && data[5] == 0 &&
                data[6] == 0 && data[7] == streaminfo_bytes,
            AudioDecodeFailure::malformed, "FLAC STREAMINFO header is malformed");
}
void validate_opus_private(const Bytes& data) {
    constexpr std::size_t identification_bytes = 19;
    require(data.size() >= identification_bytes && std::memcmp(data.data(), "OpusHead", 8) == 0,
            AudioDecodeFailure::malformed, "Opus identification header is malformed");
    require(data[8] == 1 && data[9] >= 1 && data[9] <= 8, AudioDecodeFailure::unsupported,
            "Opus version or channels are unsupported");
    const auto mapping = data[18];
    if (mapping == 0) {
        require(data[9] <= 2 && data.size() == identification_bytes, AudioDecodeFailure::malformed,
                "Opus family-zero mapping is malformed");
    } else {
        require(mapping == 1, AudioDecodeFailure::unsupported,
                "Opus channel mapping family is unsupported");
        require(data.size() == identification_bytes + 2 + data[9], AudioDecodeFailure::malformed,
                "Opus channel map is truncated");
    }
}
void validate_vorbis_private(const Bytes& data) {
    constexpr std::size_t header_signature_bytes = 7;
    require(!data.empty() && data[0] == 2, AudioDecodeFailure::malformed,
            "Vorbis private header count is malformed");
    std::size_t offset = 1;
    std::size_t lengths[3]{};
    for (std::size_t header = 0; header < 2; ++header) {
        std::uint8_t part = 0;
        do {
            require(offset < data.size(), AudioDecodeFailure::malformed,
                    "Vorbis header lacing is truncated");
            part = data[offset++];
            require(part <= data.size() - lengths[header], AudioDecodeFailure::malformed,
                    "Vorbis header length exceeds private data");
            lengths[header] += part;
        } while (part == 255);
    }
    require(lengths[0] <= data.size() - offset && lengths[1] <= data.size() - offset - lengths[0],
            AudioDecodeFailure::malformed, "Vorbis headers exceed private data");
    lengths[2] = data.size() - offset - lengths[0] - lengths[1];
    for (std::size_t header = 0; header < 3; ++header) {
        require(lengths[header] >= header_signature_bytes && data[offset] == 1 + 2 * header &&
                    std::memcmp(data.data() + offset + 1, "vorbis", 6) == 0,
                AudioDecodeFailure::malformed, "Vorbis header signature is malformed");
        offset += lengths[header];
    }
}
// AudioSampleEntry offsets include the size/type header. All words are big
// endian; the version-one extension follows the 36-byte version-zero layout.
constexpr std::size_t audio_entry_version_offset = 16;
constexpr std::size_t audio_entry_channels_offset = 24;
constexpr std::size_t audio_entry_depth_offset = 26;
constexpr std::size_t audio_entry_rate_offset = 32;
std::uint16_t configuration_be16(const Bytes& data, std::size_t offset) {
    constexpr std::size_t field_bytes = 2;
    require(offset <= data.size() && data.size() - offset >= field_bytes,
            AudioDecodeFailure::malformed, "audio configuration word is truncated");
    return static_cast<std::uint16_t>((static_cast<unsigned>(data[offset]) << 8) |
                                      data[offset + 1]);
}
std::uint32_t configuration_be32(const Bytes& data, std::size_t offset) {
    constexpr std::size_t field_bytes = 4;
    require(offset <= data.size() && data.size() - offset >= field_bytes,
            AudioDecodeFailure::malformed, "audio configuration field is truncated");
    std::uint32_t value = 0;
    for (std::size_t byte = 0; byte < field_bytes; ++byte) {
        value = (value << 8) | data[offset + byte];
    }
    return value;
}
Bytes audio_box_payload(const Bytes& entry, std::size_t children_begin, const char* type) {
    constexpr std::size_t box_header_bytes = 8;
    require(children_begin <= entry.size(), AudioDecodeFailure::malformed,
            "audio configuration child range is truncated");
    for (auto offset = children_begin; offset < entry.size();) {
        require(entry.size() - offset >= box_header_bytes, AudioDecodeFailure::malformed,
                "audio configuration box header is truncated");
        const auto size = configuration_be32(entry, offset);
        require(size >= box_header_bytes && size <= entry.size() - offset,
                AudioDecodeFailure::malformed, "audio configuration box is out of bounds");
        if (std::memcmp(entry.data() + offset + 4, type, 4) == 0) {
            return Bytes(entry.begin() + offset + box_header_bytes, entry.begin() + offset + size);
        }
        offset += size;
    }
    fail(AudioDecodeFailure::malformed, "required audio configuration box is absent");
}
/// Version-zero AudioSampleEntry children start after its 36-byte fixed header.
Bytes decoder_mp4_audio_private_box(const Bytes& entry, const char* type) {
    constexpr std::size_t audio_header_bytes = 36;
    require(entry.size() >= audio_header_bytes, AudioDecodeFailure::malformed,
            "audio sample entry is truncated");
    require(configuration_be16(entry, audio_entry_version_offset) == 0,
            AudioDecodeFailure::unsupported, "audio sample entry version is unsupported");
    return audio_box_payload(entry, audio_header_bytes, type);
}
AVCodecID mov_pcm_codec(const SourceTrack& track) {
    const auto& data = track.configuration;
    constexpr std::size_t version_zero_bytes = 36;
    constexpr std::size_t version_one_bytes = 52;
    require(data.size() >= version_zero_bytes, AudioDecodeFailure::malformed,
            "MOV PCM sample entry is truncated");
    const auto channels = configuration_be16(data, audio_entry_channels_offset);
    const auto rate = configuration_be32(data, audio_entry_rate_offset);
    require(channels >= 1 && channels <= 8 && (rate & 0xffff) == 0 && (rate >> 16) >= 8000 &&
                (rate >> 16) <= 192000,
            AudioDecodeFailure::unsupported, "MOV PCM rate or channels are unsupported");
    const auto& name = track.codec;
    if (name == "sowt" || name == "twos") {
        require(configuration_be16(data, audio_entry_version_offset) == 0 &&
                    configuration_be16(data, audio_entry_depth_offset) == 16,
                AudioDecodeFailure::unsupported, "MOV 16-bit PCM version or depth is unsupported");
        return name == "sowt" ? AV_CODEC_ID_PCM_S16LE : AV_CODEC_ID_PCM_S16BE;
    }
    require(data.size() >= version_one_bytes &&
                configuration_be16(data, audio_entry_version_offset) == 1,
            AudioDecodeFailure::unsupported, "MOV PCM packing version is unsupported");
    constexpr std::size_t samples_per_packet_offset = version_zero_bytes;
    constexpr std::size_t bytes_per_packet_offset = samples_per_packet_offset + 4;
    constexpr std::size_t bytes_per_frame_offset = bytes_per_packet_offset + 4;
    constexpr std::size_t legacy_sample_word_offset = bytes_per_frame_offset + 4;
    constexpr std::uint32_t legacy_sample_word_bytes = 2;
    const auto bytes_per_sample = name == "in24" ? 3U : name == "fl64" ? 8U : 4U;
    // These version-one forms retain the legacy 16-bit sample-size word; the
    // actual packed width is specified by the codec and packet/frame byte counts.
    require(configuration_be32(data, samples_per_packet_offset) == 1 &&
                configuration_be32(data, bytes_per_packet_offset) == bytes_per_sample &&
                configuration_be32(data, bytes_per_frame_offset) == channels * bytes_per_sample &&
                configuration_be32(data, legacy_sample_word_offset) == legacy_sample_word_bytes,
            AudioDecodeFailure::unsupported, "MOV PCM packet packing is unsupported");
    const auto wave = audio_box_payload(data, version_one_bytes, "wave");
    const auto original_format = audio_box_payload(wave, 0, "frma");
    require(original_format.size() == 4 && std::memcmp(original_format.data(), name.data(), 4) == 0,
            AudioDecodeFailure::malformed, "MOV PCM original format disagrees with sample entry");
    const auto endian = audio_box_payload(wave, 0, "enda");
    require(endian.size() == 2 && endian[0] == 0 && endian[1] <= 1, AudioDecodeFailure::unsupported,
            "MOV PCM endian flag is unsupported");
    const bool little = endian[1] == 1;
    if (name == "in24") {
        return little ? AV_CODEC_ID_PCM_S24LE : AV_CODEC_ID_PCM_S24BE;
    }
    if (name == "in32") {
        return little ? AV_CODEC_ID_PCM_S32LE : AV_CODEC_ID_PCM_S32BE;
    }
    if (name == "fl32") {
        return little ? AV_CODEC_ID_PCM_F32LE : AV_CODEC_ID_PCM_F32BE;
    }
    return little ? AV_CODEC_ID_PCM_F64LE : AV_CODEC_ID_PCM_F64BE;
}
AVCodecID matroska_pcm_codec(const SourceTrack& track) {
    const bool little = track.codec == "A_PCM/INT/LIT";
    if (track.codec == "A_PCM/FLOAT/IEEE") {
        if (track.audio_bit_depth == 32) {
            return AV_CODEC_ID_PCM_F32LE;
        }
        if (track.audio_bit_depth == 64) {
            return AV_CODEC_ID_PCM_F64LE;
        }
    } else {
        if (track.audio_bit_depth == 16) {
            return little ? AV_CODEC_ID_PCM_S16LE : AV_CODEC_ID_PCM_S16BE;
        }
        if (track.audio_bit_depth == 24) {
            return little ? AV_CODEC_ID_PCM_S24LE : AV_CODEC_ID_PCM_S24BE;
        }
        if (track.audio_bit_depth == 32) {
            return little ? AV_CODEC_ID_PCM_S32LE : AV_CODEC_ID_PCM_S32BE;
        }
    }
    fail(AudioDecodeFailure::unsupported, "Matroska PCM bit depth is unsupported");
}
AVCodecID source_codec(const SourceTrack& track, SourceContainer container) {
    const auto& name = track.codec;
    if (container == SourceContainer::mp4) {
        if (name == "mp4a") {
            // MPEG-4 systems objectTypeIndication, never a fourcc-only guess.
            constexpr std::uint8_t mpeg4_audio = 0x40;
            constexpr std::uint8_t ac3_audio = 0xA5;
            constexpr std::uint8_t eac3_audio = 0xA6;
            constexpr std::uint8_t dts_core_audio = 0xA9;
            const auto type = mp4_audio_object_type(track.configuration);
            if (type == mpeg4_audio) {
                return AV_CODEC_ID_AAC;
            }
            if (type == ac3_audio) {
                return AV_CODEC_ID_AC3;
            }
            if (type == eac3_audio) {
                return AV_CODEC_ID_EAC3;
            }
            if (type == dts_core_audio) {
                return AV_CODEC_ID_DTS;
            }
            fail(AudioDecodeFailure::unsupported, "selected mp4a object type is unsupported");
        }
        if (name == "ac-3") {
            return AV_CODEC_ID_AC3;
        }
        if (name == "ec-3") {
            return AV_CODEC_ID_EAC3;
        }
        if (name == "dtsc") {
            return AV_CODEC_ID_DTS;
        }
        if (name == "fLaC") {
            const auto data = decoder_mp4_audio_private_box(track.configuration, "dfLa");
            require(data.size() >= 4 && data[0] == 0, AudioDecodeFailure::malformed,
                    "FLAC specific box version is malformed");
            Bytes private_data{'f', 'L', 'a', 'C'};
            private_data.insert(private_data.end(), data.begin() + 4, data.end());
            validate_flac_private(private_data);
            return AV_CODEC_ID_FLAC;
        }
        if (name == "Opus") {
            const auto data = decoder_mp4_audio_private_box(track.configuration, "dOps");
            require(data.size() >= 11 && data[0] == 0, AudioDecodeFailure::malformed,
                    "Opus specific box is malformed");
            require(data[1] >= 1 && data[1] <= 8 &&
                        ((data[10] == 0 && data[1] <= 2 && data.size() == 11) ||
                         (data[10] == 1 && data.size() == 13 + data[1])),
                    AudioDecodeFailure::unsupported, "Opus specific channel map is unsupported");
            return AV_CODEC_ID_OPUS;
        }
        if (name == "sowt" || name == "twos" || name == "in24" || name == "in32" ||
            name == "fl32" || name == "fl64") {
            return mov_pcm_codec(track);
        }
    } else {
        if (name == "A_AAC") {
            return AV_CODEC_ID_AAC;
        }
        if (name == "A_AC3") {
            return AV_CODEC_ID_AC3;
        }
        if (name == "A_EAC3") {
            return AV_CODEC_ID_EAC3;
        }
        if (name == "A_DTS") {
            return AV_CODEC_ID_DTS;
        }
        if (name == "A_FLAC") {
            validate_flac_private(track.configuration);
            return AV_CODEC_ID_FLAC;
        }
        if (name == "A_OPUS") {
            validate_opus_private(track.configuration);
            return AV_CODEC_ID_OPUS;
        }
        if (name == "A_VORBIS") {
            validate_vorbis_private(track.configuration);
            return AV_CODEC_ID_VORBIS;
        }
        if (name == "A_TRUEHD") {
            require(track.configuration.empty(), AudioDecodeFailure::unsupported,
                    "TrueHD private configuration is unsupported");
            return AV_CODEC_ID_TRUEHD;
        }
        if (name == "A_PCM/INT/LIT" || name == "A_PCM/INT/BIG" || name == "A_PCM/FLOAT/IEEE") {
            return matroska_pcm_codec(track);
        }
    }
    fail(AudioDecodeFailure::unsupported, "selected audio codec is unsupported");
}
void validate_timeline(const SourceTrack& track, SourceContainer container) {
    require(!track.encoded && track.standard_timeline, AudioDecodeFailure::unsupported,
            "selected audio protection or timeline is unsupported");
    if (container == SourceContainer::mp4) {
        require(track.sample_description_count == 1 && track.timescale > 0,
                AudioDecodeFailure::unsupported, "selected audio description is unsupported");
        require(track.edits.size() <= 2, AudioDecodeFailure::unsupported,
                "selected audio edit sequence is unsupported");
        for (std::size_t index = 0; index < track.edits.size(); ++index) {
            const auto& edit = track.edits[index];
            const bool empty = track.edits.size() == 2 && index == 0;
            require(edit.rate_integer == 1 && edit.rate_fraction == 0 &&
                        (empty ? edit.media_time == -1 : edit.media_time >= 0),
                    AudioDecodeFailure::unsupported, "selected audio edit is unsupported");
        }
    }
}
std::uint64_t decoder_track_id(const AVStream& stream, SourceContainer container) {
    if (container == SourceContainer::mp4) {
        // MOV reads the unsigned tkhd ID into its signed int field.
        return static_cast<std::uint32_t>(stream.id);
    }
    const auto* value = av_dict_get(stream.metadata, "sap2_matroska_track_number", nullptr, 0);
    require(value && value->value, AudioDecodeFailure::unsupported,
            "Matroska decoder lacks original track identity metadata");
    const std::string_view text(value->value);
    std::uint64_t id = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), id, 16);
    require(parsed.ec == std::errc{} && parsed.ptr == text.data() + text.size() && id != 0,
            AudioDecodeFailure::malformed, "invalid Matroska decoder track identity");
    return id;
}
int match_stream(const AVFormatContext& format, const SourceTrack& selected,
                 SourceContainer container) {
    require(format.nb_streams <= max_source_tracks, AudioDecodeFailure::too_large,
            "decoder has too many streams");
    int matched = -1;
    for (unsigned index = 0; index < format.nb_streams; ++index) {
        const auto& stream = *format.streams[index];
        // Attached pictures/data are not audio identities; no ordinal fallback.
        if (stream.codecpar->codec_type != AVMEDIA_TYPE_AUDIO) {
            continue;
        }
        if (decoder_track_id(stream, container) == selected.id) {
            require(matched == -1, AudioDecodeFailure::malformed,
                    "duplicate selected decoder identity");
            matched = static_cast<int>(index);
        }
    }
    require(matched >= 0, AudioDecodeFailure::invalid_selection,
            "selected audio has no decoder stream");
    require(format.streams[matched]->codecpar->codec_id == source_codec(selected, container),
            AudioDecodeFailure::unsupported, "selected source and decoder codecs disagree");
    return matched;
}
struct InputAudioFormat {
    int rate = 0;
    int sample_format = -1;
    int channels = 0;
    std::uint64_t channel_mask = 0;
    bool established = false; ///< True only after an actual decoded frame passes validation.
};
/// Opt-in HLS clock: absolute timestamps, legacy fixed-frame grid, or bounded
/// start reconstruction. Rounded timestamps alone cannot define seek phase.
struct AudioCadence {
    bool enabled = false;
    bool reopen_at_start = false;
    bool rebuild_from_start = false;
    bool indexed_clock = false;
    int frame_samples = 0;
    std::int64_t origin_input_sample = 0;
    std::uint32_t absolute_precision_samples = 0;
};
/// Validated audio coordinates; positions use source samples before packet
/// skip/discard. Decoder-owned trimming advances PTS/shortens frames exactly once.
struct IndexedAudioPacket {
    std::uint64_t offset = 0;
    std::uint32_t size = 0;
    std::int64_t position = 0;
    std::uint32_t samples = 0;
    std::uint32_t skip = 0;
    std::uint32_t discard = 0;
};

/// One independently restartable sparse coordinate, never a full packet table.
/// Vorbis duration parsing needs only the previous packet's overlap mode.
struct AudioRestartAnchor {
    MkvAudioCursor cursor;
    std::int64_t position = 0;
    std::uint64_t ordinal = 0;
    MkvAudioPacket previous;
};
constexpr std::size_t maximum_audio_anchors = 65536;
struct IndexedPacketAnalysis {
    IndexedAudioPacket packet;
    bool restartable;
};

struct ParserDeleter {
    void operator()(AVCodecParserContext* value) const noexcept {
        av_parser_close(value);
    }
};
using Parser = std::unique_ptr<AVCodecParserContext, ParserDeleter>;
struct PacketAnalysis {
    std::uint32_t samples;
    bool restartable;
};

/** Bounded duration/restart analysis using the already pinned codec parsers.
 * Complete Matroska packets are required. FLAC/Vorbis/TrueHD analysis reads a
 * padded 64-byte prefix; Opus framing analysis needs the bounded complete packet.
 * This validates headers, not every compressed payload's integrity. Window
 * decoding remains responsible for malformed payload and format-change refusal.
 */
class CompressedPacketParser {
public:
    explicit CompressedPacketParser(const AVCodecParameters& parameters)
        : parser_(av_parser_init(parameters.codec_id)), codec_(avcodec_alloc_context3(nullptr)),
          expected_rate_(parameters.sample_rate) {
        require(parser_ && codec_, AudioDecodeFailure::unsupported,
                "indexed audio parser unavailable");
        require(parameters.ch_layout.order == AV_CHANNEL_ORDER_NATIVE,
                AudioDecodeFailure::unsupported, "indexed compressed layout is unsupported");
        expected_mask_ = parameters.ch_layout.u.mask;
        checked(avcodec_parameters_to_context(codec_.get(), &parameters));
        parser_->flags |= PARSER_FLAG_COMPLETE_FRAMES;
    }
    CompressedPacketParser(const CompressedPacketParser&) = delete;
    CompressedPacketParser& operator=(const CompressedPacketParser&) = delete;
    CompressedPacketParser(CompressedPacketParser&&) = delete;
    CompressedPacketParser& operator=(CompressedPacketParser&&) = delete;

    PacketAnalysis analyse(Reader& reader, const MkvAudioPacket& packet) {
        constexpr std::uint32_t maximum_compressed_packet_bytes = 1U << 20;
        constexpr std::uint32_t header_prefix_bytes = 64;
        require(packet.size > 0 && packet.size <= maximum_compressed_packet_bytes,
                AudioDecodeFailure::unsupported, "indexed compressed packet exceeds bounds");
        const auto length = codec_->codec_id == AV_CODEC_ID_OPUS
                                ? packet.size
                                : std::min(packet.size, header_prefix_bytes);
        std::vector<std::uint8_t> bytes(length + AV_INPUT_BUFFER_PADDING_SIZE, 0);
        reader.read_header(packet.offset, bytes.data(), length);
        if (codec_->codec_id == AV_CODEC_ID_TRUEHD) {
            require(length >= 8, AudioDecodeFailure::unsupported,
                    "indexed TrueHD access-unit header is truncated");
            constexpr std::uint32_t access_unit_length_mask = 0x0fff;
            constexpr std::uint32_t bytes_per_word = 2;
            // The big-endian header word's low 12 bits count two-byte words;
            // its high nibble belongs to parity, not the access-unit length.
            const auto header_word = (static_cast<std::uint32_t>(bytes[0]) << 8) | bytes[1];
            const auto declared_unit_bytes =
                (header_word & access_unit_length_mask) * bytes_per_word;
            require(declared_unit_bytes == packet.size, AudioDecodeFailure::unsupported,
                    "indexed TrueHD block must contain one access unit");
        }
        // TrueHD non-major units inherit the last major sync's duration. Other
        // parsers must establish each packet's duration without stale fallback.
        if (codec_->codec_id != AV_CODEC_ID_TRUEHD) {
            parser_->duration = 0;
        }
        std::uint8_t* output = nullptr;
        int output_length = 0;
        const auto consumed = av_parser_parse2(parser_.get(), codec_.get(), &output, &output_length,
                                               bytes.data(), static_cast<int>(length),
                                               AV_NOPTS_VALUE, AV_NOPTS_VALUE, packet.offset);
        require(consumed == static_cast<int>(length) && output_length == consumed && output &&
                    parser_->duration > 0 && parser_->duration <= maximum_frame_samples,
                AudioDecodeFailure::unsupported, "indexed compressed packet header is unsupported");
        require(codec_->sample_rate == expected_rate_ &&
                    codec_->ch_layout.order == AV_CHANNEL_ORDER_NATIVE &&
                    codec_->ch_layout.u.mask == expected_mask_,
                AudioDecodeFailure::unsupported, "indexed compressed packet format changed");
        return {static_cast<std::uint32_t>(parser_->duration),
                codec_->codec_id != AV_CODEC_ID_TRUEHD || parser_->key_frame == 1};
    }

private:
    Parser parser_;
    Codec codec_;
    int expected_rate_;
    std::uint64_t expected_mask_ = 0;
};
} // namespace

struct AudioDecoderSource::Impl {
    Reader reader; // Outlives every FFmpeg owner which refers to it.
    Io io;
    Format format;
    Codec codec;
    SourceTrack selected;
    SourceContainer container = SourceContainer::mp4;
    int stream_index = -1;
    bool usable = true;
    InputAudioFormat input_format;
    AudioCadence cadence;
    std::vector<AudioRestartAnchor> audio_anchors;
    std::size_t anchor_begin = 0;
    std::uint64_t indexed_packet_count = 0;
    std::int64_t indexed_end_position = 0;
    std::int64_t index_delay = 0;
    std::int64_t index_precision = 0;
    std::int64_t index_presentation_limit = 0;
    std::uint32_t index_bytes_per_frame = 0;

    Impl(const MediaSource& source, std::uint64_t size) : reader{source, size} {}
    void open_format() {
        auto* buffer = static_cast<std::uint8_t*>(av_malloc(io_buffer_bytes));
        require(buffer != nullptr, AudioDecodeFailure::codec_error, "audio I/O allocation failed");
        io.reset(avio_alloc_context(buffer, io_buffer_bytes, 0, &reader, Reader::read, nullptr,
                                    Reader::seek));
        if (!io) {
            av_free(buffer);
            fail(AudioDecodeFailure::codec_error, "audio I/O context allocation failed");
        }
        format.reset(avformat_alloc_context());
        require(format != nullptr, AudioDecodeFailure::codec_error,
                "audio format allocation failed");
        format->pb = io.get();
        format->flags |= AVFMT_FLAG_CUSTOM_IO;
        format->interrupt_callback = AVIOInterruptCB{Reader::interrupt, &reader};
        format->probesize = 1 << 20;
        format->max_streams = static_cast<int>(max_source_tracks);
        format->max_analyze_duration = AV_TIME_BASE;
        const auto* demuxer =
            av_find_input_format(container == SourceContainer::mp4 ? "mov" : "matroska");
        require(demuxer != nullptr, AudioDecodeFailure::unsupported, "audio demuxer unavailable");
        auto* raw_format = format.release();
        const auto opened = avformat_open_input(&raw_format, nullptr, demuxer, nullptr);
        format.reset(raw_format);
        reader.check();
        require(opened >= 0 && format, AudioDecodeFailure::malformed,
                "audio container open failed");
        stream_index = match_stream(*format, selected, container);
        discard_other_streams();
        const auto found = avformat_find_stream_info(format.get(), nullptr);
        reader.check();
        require(found >= 0, AudioDecodeFailure::malformed, "audio stream probe failed");
    }
    void open_codec() {
        stream_index = match_stream(*format, selected, container);
        const auto* parameters = format->streams[stream_index]->codecpar;
        const auto* decoder = avcodec_find_decoder(parameters->codec_id);
        require(decoder != nullptr, AudioDecodeFailure::unsupported, "audio decoder unavailable");
        codec.reset(avcodec_alloc_context3(decoder));
        require(codec != nullptr, AudioDecodeFailure::codec_error,
                "audio decoder allocation failed");
        checked(avcodec_parameters_to_context(codec.get(), parameters));
        if (container == SourceContainer::matroska && selected.codec.compare(0, 6, "A_PCM/") == 0 &&
            codec->ch_layout.order == AV_CHANNEL_ORDER_UNSPEC &&
            codec->ch_layout.nb_channels >= 1 && codec->ch_layout.nb_channels <= 2) {
            // Matroska PCM has no channel-mask element. Only mono/stereo have
            // unambiguous ordinary speaker identities here; never guess surround.
            const auto channels = codec->ch_layout.nb_channels;
            av_channel_layout_uninit(&codec->ch_layout);
            av_channel_layout_default(&codec->ch_layout, channels);
        }
        // MOV/MKV already publish edited/delay-adjusted PTS and packet skip data.
        // Decoder preinit must not independently skip codecpar padding again.
        codec->initial_padding = 0;
        codec->err_recognition = AV_EF_EXPLODE;
        codec->pkt_timebase = format->streams[stream_index]->time_base;
        if (parameters->codec_id == AV_CODEC_ID_AC3 || parameters->codec_id == AV_CODEC_ID_EAC3) {
            checked(av_opt_set_double(codec->priv_data, "drc_scale", 0.0, 0));
        }
        checked(avcodec_open2(codec.get(), decoder, nullptr));
        if (container == SourceContainer::matroska) {
            // SamplingFrequency can describe AAC's coded rate; SBR output is
            // separately declared and defaults to that rate only when omitted.
            const auto declared_output_rate = selected.audio_output_sample_rate != 0
                                                  ? selected.audio_output_sample_rate
                                                  : selected.audio_sample_rate;
            require(std::isfinite(declared_output_rate) &&
                        declared_output_rate == parameters->sample_rate &&
                        selected.audio_channels ==
                            static_cast<std::uint64_t>(parameters->ch_layout.nb_channels),
                    AudioDecodeFailure::unsupported,
                    "selected Matroska audio metadata disagrees with decoder");
        }
        // Reopening must preserve the format established by decoded frames;
        // configure() validates the new epoch against it before conversion.
        if (!input_format.established) {
            input_format.rate = parameters->sample_rate;
            input_format.sample_format = parameters->format;
            input_format.channels = parameters->ch_layout.nb_channels;
            if (parameters->ch_layout.order == AV_CHANNEL_ORDER_NATIVE) {
                input_format.channel_mask = parameters->ch_layout.u.mask;
            }
        }
        const bool legacy_cadence =
            parameters->codec_id == AV_CODEC_ID_AAC || parameters->codec_id == AV_CODEC_ID_AC3 ||
            parameters->codec_id == AV_CODEC_ID_EAC3 || parameters->codec_id == AV_CODEC_ID_DTS;
        if (cadence.enabled && !legacy_cadence) {
            cadence.reopen_at_start = true;
            require(parameters->sample_rate > 0, AudioDecodeFailure::unsupported,
                    "selected audio has no sample rate");
            cadence.rebuild_from_start =
                av_rescale_q_rnd(1, format->streams[stream_index]->time_base,
                                 AVRational{1, parameters->sample_rate}, AV_ROUND_UP) > 1;
        }
    }
    void discard_other_streams() {
        for (unsigned index = 0; index < format->nb_streams; ++index) {
            format->streams[index]->discard =
                static_cast<int>(index) == stream_index ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
        }
    }
    void index_matroska_audio() {
        const bool packed_pcm = selected.codec.compare(0, 6, "A_PCM/") == 0;
        const bool compressed =
            codec->codec_id == AV_CODEC_ID_FLAC || codec->codec_id == AV_CODEC_ID_OPUS ||
            codec->codec_id == AV_CODEC_ID_VORBIS || codec->codec_id == AV_CODEC_ID_TRUEHD;
        if (!cadence.enabled || container != SourceContainer::matroska ||
            (!packed_pcm && !compressed)) {
            return;
        }
        auto* stream = format->streams[stream_index];
        require(codec->sample_rate >= 8000 && codec->sample_rate <= 192000 &&
                    selected.audio_channels >= 1 && selected.audio_channels <= 8 &&
                    codec->ch_layout.order == AV_CHANNEL_ORDER_NATIVE &&
                    av_channel_layout_check(&codec->ch_layout),
                AudioDecodeFailure::unsupported, "indexed audio layout or rate is unsupported");
        const AVRational input_clock{1, codec->sample_rate};
        const auto absolute_precision =
            av_rescale_q_rnd(1, stream->time_base, output_clock, AV_ROUND_UP);
        // Preserve the window decoder's one-second source-clock admission bound
        // before replacing the stream clock or narrowing the reported precision.
        constexpr auto maximum_absolute_precision_samples = output_rate;
        require(absolute_precision > 0 && absolute_precision <= maximum_absolute_precision_samples,
                AudioDecodeFailure::unsupported, "indexed audio timestamp clock is unsupported");
        const auto precision = av_rescale_q_rnd(1, stream->time_base, input_clock, AV_ROUND_UP);
        const auto delay = av_rescale_q(static_cast<std::int64_t>(selected.codec_delay_ns),
                                        nanosecond_clock, input_clock);
        require(delay >= 0 && delay <= maximum_frame_samples, AudioDecodeFailure::unsupported,
                "indexed audio CodecDelay exceeds trim budget");
        const auto bytes_per_frame = selected.audio_channels * (selected.audio_bit_depth / 8);
        require(!packed_pcm || (bytes_per_frame > 0 && selected.audio_bit_depth % 8 == 0),
                AudioDecodeFailure::unsupported, "indexed audio packing is unsupported");
        auto parser =
            compressed ? std::make_unique<CompressedPacketParser>(*stream->codecpar) : nullptr;
        const bool small_packet_codec = codec->codec_id == AV_CODEC_ID_TRUEHD ||
                                        codec->codec_id == AV_CODEC_ID_OPUS ||
                                        codec->codec_id == AV_CODEC_ID_VORBIS;
        reader.header_read_ahead = small_packet_codec ? Reader::header_cache_bytes
                                                      : Reader::large_packet_header_page_bytes;
        index_delay = delay;
        index_precision = precision;
        index_bytes_per_frame = static_cast<std::uint32_t>(bytes_per_frame);
        index_presentation_limit = av_rescale_q(
            static_cast<std::int64_t>(maximum_presentation_samples), output_clock, input_clock);
        MkvAudioScanner scanner(header_reader(), reader.size, selected.id);
        MkvAudioPacket packet, previous;
        MkvAudioCursor cursor;
        std::int64_t position = 0;
        while (scanner.next(packet, cursor)) {
            const bool first = indexed_packet_count == 0;
            const auto analysis = analyse_indexed_packet(packet, position, first, parser.get());
            if (first || (analysis.restartable &&
                          position - audio_anchors.back().position >= codec->sample_rate)) {
                require(audio_anchors.size() < maximum_audio_anchors, AudioDecodeFailure::too_large,
                        "indexed audio restart anchor budget exceeded");
                if (audio_anchors.size() == audio_anchors.capacity()) {
                    constexpr std::size_t initial_anchor_capacity = 64;
                    const auto capacity =
                        std::max(initial_anchor_capacity,
                                 std::min(maximum_audio_anchors, audio_anchors.capacity() * 2));
                    audio_anchors.reserve(capacity);
                }
                audio_anchors.push_back({cursor, position, indexed_packet_count, previous});
            }
            position += analysis.packet.samples;
            previous = packet;
            ++indexed_packet_count;
        }
        require(indexed_packet_count != 0, AudioDecodeFailure::malformed,
                "selected audio has no packets");
        indexed_end_position = position;
        cadence.absolute_precision_samples = static_cast<std::uint32_t>(absolute_precision);
        // Packet positions are exact relative to the bounded initial origin.
        // Keep the original absolute clock precision separately for reporting.
        stream->time_base = codec->pkt_timebase = input_clock;
        cadence.rebuild_from_start = false;
        cadence.indexed_clock = true;
    }
    RandomReader header_reader() {
        return [this](std::uint64_t offset, std::uint8_t* output, std::size_t length) {
            reader.read_header(offset, output, length);
        };
    }
    /// Shared admission/reconstruction checks. position advances only in the
    /// caller, after this complete packet and its signed trim have been checked.
    IndexedPacketAnalysis analyse_indexed_packet(const MkvAudioPacket& packet,
                                                 std::int64_t& position, bool first,
                                                 CompressedPacketParser* parser) {
        const AVRational input_clock{1, codec->sample_rate};
        require(packet.stored_timestamp_ns <=
                    static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()),
                AudioDecodeFailure::unsupported, "indexed audio timestamp exceeds bounds");
        const auto timestamp = av_rescale_q(static_cast<std::int64_t>(packet.stored_timestamp_ns),
                                            nanosecond_clock, input_clock) -
                               index_delay;
        if (first) {
            position = timestamp;
        }
        require(position >= -maximum_frame_samples && position <= index_presentation_limit &&
                    timestamp >= position - index_precision - 1 &&
                    timestamp <= position + index_precision + 1,
                AudioDecodeFailure::unsupported,
                "indexed audio packet clock is discontinuous or out of bounds");
        PacketAnalysis analysis{};
        if (!parser) {
            require(packet.size > 0 && packet.size % index_bytes_per_frame == 0 &&
                        packet.size / index_bytes_per_frame <= maximum_frame_samples,
                    AudioDecodeFailure::unsupported, "indexed audio packet packing exceeds bounds");
            analysis = {packet.size / index_bytes_per_frame, true};
        } else {
            analysis = parser->analyse(reader, packet);
            require(!first || analysis.restartable, AudioDecodeFailure::unsupported,
                    "indexed audio has no initial restart point");
        }
        const auto padding = av_rescale_q(packet.discard_padding_ns, nanosecond_clock, input_clock);
        require(padding >= -maximum_frame_samples && padding <= maximum_frame_samples,
                AudioDecodeFailure::unsupported, "indexed audio packet trim exceeds bounds");
        const auto skip = padding < 0 ? static_cast<std::uint32_t>(-padding)
                          : first     ? static_cast<std::uint32_t>(index_delay)
                                      : 0U;
        const auto discard = padding > 0 ? static_cast<std::uint32_t>(padding) : 0U;
        if (parser && codec->codec_id == AV_CODEC_ID_VORBIS && first) {
            // The decoder removes its first overlap frame intrinsically and
            // would overwrite a larger skip. Smaller delay preserves silence.
            require(index_delay <= analysis.samples && skip <= analysis.samples && discard == 0,
                    AudioDecodeFailure::unsupported,
                    "indexed Vorbis first trim exceeds decoder overlap priming");
        }
        require(parser ? skip + discard <= analysis.samples : skip + discard < analysis.samples,
                AudioDecodeFailure::unsupported,
                "indexed audio packet trim is outside admitted frame");
        return {{packet.offset, packet.size, position, analysis.samples, skip, discard},
                analysis.restartable};
    }
    void seek(std::uint64_t begin) {
        const auto metadata_preroll = av_rescale_q(
            static_cast<std::int64_t>(selected.seek_preroll_ns), nanosecond_clock, output_clock);
        const auto preroll = std::max<std::int64_t>(output_rate, metadata_preroll);
        if (!audio_anchors.empty()) {
            const auto target = av_rescale_q(static_cast<std::int64_t>(begin) - preroll,
                                             output_clock, AVRational{1, codec->sample_rate});
            const auto after =
                std::upper_bound(audio_anchors.begin(), audio_anchors.end(), target,
                                 [](std::int64_t sample, const AudioRestartAnchor& anchor) {
                                     return sample < anchor.position;
                                 });
            anchor_begin = after == audio_anchors.begin()
                               ? 0
                               : static_cast<std::size_t>(after - audio_anchors.begin() - 1);
            avcodec_flush_buffers(codec.get());
            return;
        }
        if (cadence.rebuild_from_start ||
            (cadence.reopen_at_start && begin <= static_cast<std::uint64_t>(preroll))) {
            // A timestamp-zero seek can land after a negative CodecDelay packet;
            // the demuxer also clears consumed initial skip state on seeks.
            // Reopen callback-backed owners in dependency order so every epoch
            // sees the same first packet and fresh demuxer-owned trimming.
            codec.reset();
            format.reset();
            io.reset();
            reader.position = 0;
            open_format();
            open_codec();
            return;
        }
        const auto* stream = format->streams[stream_index];
        const auto raw_target = static_cast<std::int64_t>(begin) - preroll;
        // MOV's negative AAC packet carries the overlap/priming history at start.
        const auto target =
            container == SourceContainer::mp4 ? raw_target : std::max<std::int64_t>(0, raw_target);
        const auto timestamp = av_rescale_q(target, output_clock, stream->time_base);
        const auto result =
            avformat_seek_file(format.get(), stream_index, std::numeric_limits<std::int64_t>::min(),
                               timestamp, timestamp, AVSEEK_FLAG_BACKWARD);
        reader.check();
        require(result >= 0, AudioDecodeFailure::unsupported, "selected audio seek failed");
        avcodec_flush_buffers(codec.get());
    }
};

namespace {
/// One window has fresh resampler history, rebuilt by decoding preroll.
class WindowDecoder {
public:
    WindowDecoder(AVCodecContext& codec, AVStream& stream, Reader& reader,
                  InputAudioFormat& input_format, AudioCadence& cadence, DecodedAudioWindow& output,
                  std::uint32_t count, bool respect_opus_packet_duration)
        : codec_(codec), stream_(stream), reader_(reader), expected_format_(input_format),
          cadence_(cadence), output_(output), count_(count), frame_(av_frame_alloc()),
          packet_(av_packet_alloc()), respect_opus_packet_duration_(respect_opus_packet_duration) {
        require(frame_ && packet_, AudioDecodeFailure::codec_error,
                "audio window allocation failed");
        const auto quantum = av_rescale_q_rnd(1, stream.time_base, output_clock, AV_ROUND_UP);
        require(quantum > 0 && quantum <= output_rate, AudioDecodeFailure::unsupported,
                "audio timestamp clock is unsupported");
        output_.timestamp_precision_samples =
            std::max(static_cast<std::uint32_t>(quantum), cadence.absolute_precision_samples);
        // Report the original absolute uncertainty, but validate decoder-frame
        // continuity against the exact indexed clock. Container rounding must
        // not conceal a payload/trim gap in reconstructed sample positions.
        continuity_precision_samples_ = cadence.indexed_clock ? static_cast<std::uint32_t>(quantum)
                                                              : output_.timestamp_precision_samples;
        // A previously established format also describes an empty post-EOF
        // window, even when a far-late seek returns no additional frames.
        if (input_format.established && input_format.sample_format >= 0 &&
            input_format.channels > 0 && input_format.channels <= 8 &&
            input_format.channel_mask != 0) {
            output_.channels = static_cast<std::uint32_t>(input_format.channels);
            output_.channel_mask = input_format.channel_mask;
        }
    }
    void run(AVFormatContext& format) {
        std::size_t packets = 0;
        while (!complete_) {
            reader_.check();
            const auto read = av_read_frame(&format, packet_.get());
            reader_.check();
            if (read == AVERROR_EOF) {
                checked(avcodec_send_packet(&codec_, nullptr));
                drain();
                flush_resampler();
                output_.eof = true;
                break;
            }
            require(read >= 0, AudioDecodeFailure::malformed, "audio packet read failed");
            require(++packets <= maximum_read_calls, AudioDecodeFailure::too_large,
                    "audio packet budget exceeded");
            if (packet_->stream_index == stream_.index) {
                begin_dts_packet_clock(*packet_);
                validate_packet_trim(*packet_);
                checked(avcodec_send_packet(&codec_, packet_.get()));
                drain();
            }
            av_packet_unref(packet_.get());
        }
        finish_window();
    }
    void run_indexed(const std::function<std::optional<IndexedAudioPacket>()>& next) {
        while (!complete_) {
            reader_.check();
            const auto packet = next();
            if (!packet) {
                checked(avcodec_send_packet(&codec_, nullptr));
                drain();
                flush_resampler();
                output_.eof = true;
                break;
            }
            const auto& indexed = *packet;
            require(indexed.size <= static_cast<std::uint32_t>(std::numeric_limits<int>::max()),
                    AudioDecodeFailure::too_large,
                    "indexed audio packet allocation exceeds bounds");
            checked(av_new_packet(packet_.get(), static_cast<int>(indexed.size)));
            reader_.read_exact(indexed.offset, packet_->data, indexed.size);
            packet_->pts = packet_->dts = indexed.position;
            packet_->duration = indexed.samples;
            packet_->stream_index = stream_.index;
            if (indexed.skip || indexed.discard) {
                constexpr std::size_t skip_side_data_bytes = 10;
                auto* trim = av_packet_new_side_data(packet_.get(), AV_PKT_DATA_SKIP_SAMPLES,
                                                     skip_side_data_bytes);
                require(trim != nullptr, AudioDecodeFailure::codec_error,
                        "indexed audio trim allocation failed");
                for (unsigned byte = 0; byte < 4; ++byte) {
                    trim[byte] = static_cast<std::uint8_t>(indexed.skip >> (8 * byte));
                    trim[4 + byte] = static_cast<std::uint8_t>(indexed.discard >> (8 * byte));
                }
                trim[8] = trim[9] = 0;
            }
            validate_packet_trim(*packet_);
            checked(avcodec_send_packet(&codec_, packet_.get()));
            drain();
            av_packet_unref(packet_.get());
        }
        finish_window();
    }
    WindowDecoder(const WindowDecoder&) = delete;
    WindowDecoder& operator=(const WindowDecoder&) = delete;
    WindowDecoder(WindowDecoder&&) = delete;
    WindowDecoder& operator=(WindowDecoder&&) = delete;

private:
    /// FFmpeg owns nanosecond-to-sample conversion; bound its packet-level result
    /// before decoder consumption. Do not manually apply that trim a second time.
    static void validate_packet_trim(const AVPacket& packet) {
        constexpr std::size_t trim_side_data_bytes = 10;
        std::size_t size = 0;
        const auto* data = av_packet_get_side_data(&packet, AV_PKT_DATA_SKIP_SAMPLES, &size);
        if (!data) {
            return;
        }
        require(size == trim_side_data_bytes, AudioDecodeFailure::malformed,
                "audio packet trim side data is malformed");
        const auto little_endian_count = [](const std::uint8_t* bytes) {
            std::uint32_t value = 0;
            for (unsigned byte = 0; byte < 4; ++byte) {
                value |= static_cast<std::uint32_t>(bytes[byte]) << (8 * byte);
            }
            return value;
        };
        require(little_endian_count(data) <= maximum_frame_samples &&
                    little_endian_count(data + 4) <= maximum_frame_samples,
                AudioDecodeFailure::unsupported, "audio packet trim exceeds frame budget");
    }
    AVCodecContext& codec_;
    AVStream& stream_;
    Reader& reader_;
    InputAudioFormat& expected_format_;
    AudioCadence& cadence_;
    DecodedAudioWindow& output_;
    std::uint32_t count_;
    Frame frame_;
    Packet packet_;
    Resampler resampler_;
    int input_rate_ = 0;
    int input_format_ = -1;
    std::uint64_t input_mask_ = 0;
    std::uint64_t input_samples_ = 0;
    std::int64_t origin_ = 0;
    std::int64_t next_output_ = 0;
    std::int64_t covered_ = 0;
    bool complete_ = false;
    bool respect_opus_packet_duration_ = false;
    std::uint32_t continuity_precision_samples_ = 0;
    std::optional<std::int64_t> rebuild_origin_input_sample_;
    std::int64_t dts_block_file_offset_ = -1;
    std::optional<std::int64_t> dts_block_origin_input_sample_;
    std::uint64_t dts_block_samples_ = 0;
    std::size_t dts_block_frames_ = 0;

    /// The pinned Matroska demuxer gives every lace packet its containing
    /// block's file position. A new position resets the sample clock; unknown
    /// positions retain the strict generic timestamp path instead of guessing.
    void begin_dts_packet_clock(const AVPacket& packet) {
        const auto position =
            cadence_.enabled && codec_.codec_id == AV_CODEC_ID_DTS ? packet.pos : -1;
        if (position < 0 || position != dts_block_file_offset_) {
            dts_block_file_offset_ = position;
            dts_block_origin_input_sample_.reset();
            dts_block_samples_ = 0;
            dts_block_frames_ = 0;
        }
    }

    void record_dts_block_frame(std::int64_t position, int samples) {
        if (dts_block_file_offset_ < 0) {
            return;
        }
        // RFC 9559's one-octet lace count admits at most 256 frames per block.
        constexpr std::size_t maximum_lace_frames = 256;
        require(dts_block_frames_ < maximum_lace_frames, AudioDecodeFailure::unsupported,
                "DTS block exceeds bounded lace frame count");
        if (!dts_block_origin_input_sample_) {
            dts_block_origin_input_sample_ = position;
        }
        dts_block_samples_ += static_cast<std::uint64_t>(samples);
        ++dts_block_frames_;
    }

    void finish_window() {
        require(output_.channels != 0, AudioDecodeFailure::malformed,
                "audio source has no decoded frames");
        const auto frames = covered_ <= static_cast<std::int64_t>(output_.first_sample)
                                ? 0
                                : std::min<std::uint64_t>(count_, covered_ - output_.first_sample);
        output_.samples.resize(static_cast<std::size_t>(frames) * output_.channels);
    }

    void configure(const AVFrame& frame, std::int64_t timestamp, std::int64_t input_position) {
        require(frame.ch_layout.order == AV_CHANNEL_ORDER_NATIVE &&
                    av_channel_layout_check(&frame.ch_layout) && frame.ch_layout.nb_channels >= 1 &&
                    frame.ch_layout.nb_channels <= 8 && frame.sample_rate >= 8000 &&
                    frame.sample_rate <= 192000,
                AudioDecodeFailure::unsupported, "audio frame layout or rate is unsupported");
        input_rate_ = frame.sample_rate;
        input_format_ = frame.format;
        input_mask_ = frame.ch_layout.u.mask;
        require((expected_format_.rate == 0 || expected_format_.rate == input_rate_) &&
                    (expected_format_.sample_format < 0 ||
                     expected_format_.sample_format == input_format_) &&
                    (expected_format_.channels == 0 ||
                     expected_format_.channels == frame.ch_layout.nb_channels) &&
                    (expected_format_.channel_mask == 0 ||
                     expected_format_.channel_mask == input_mask_),
                AudioDecodeFailure::unsupported,
                "selected audio format changed across seek epochs");
        expected_format_ = {input_rate_, input_format_, frame.ch_layout.nb_channels, input_mask_,
                            true};
        output_.channels = static_cast<std::uint32_t>(frame.ch_layout.nb_channels);
        output_.channel_mask = input_mask_;
        output_.samples.assign(static_cast<std::size_t>(count_) * output_.channels, 0.0F);
        origin_ = next_output_ = timestamp;
        SwrContext* raw = nullptr;
        auto layout = frame.ch_layout; // Native mask layout has no owned map.
        const auto allocated = swr_alloc_set_opts2(
            &raw, &layout, AV_SAMPLE_FMT_FLT, output_rate, &layout,
            static_cast<AVSampleFormat>(frame.format), frame.sample_rate, 0, nullptr);
        resampler_.reset(raw);
        checked(allocated);
        require(resampler_ != nullptr, AudioDecodeFailure::codec_error,
                "resampler allocation failed");
        checked(swr_init(resampler_.get()));
        align_resampler_phase(frame, input_position);
    }
    /// Start each epoch on the same rational input/output phase as presentation
    /// sample zero. Dummy history is discarded during preroll, never a seek offset.
    void align_resampler_phase(const AVFrame& frame, std::int64_t input_position) {
        const auto period = input_rate_ / std::gcd(input_rate_, output_rate);
        require(period <= 8192, AudioDecodeFailure::unsupported,
                "audio resampling phase period is unsupported");
        const auto padding = static_cast<int>((input_position % period + period) % period);
        next_output_ =
            av_rescale_q(input_position - padding, AVRational{1, input_rate_}, output_clock);
        if (!padding) {
            return;
        }
        Frame silence(av_frame_alloc());
        require(silence != nullptr, AudioDecodeFailure::codec_error,
                "audio phase allocation failed");
        silence->format = input_format_;
        silence->sample_rate = input_rate_;
        silence->nb_samples = padding;
        checked(av_channel_layout_copy(&silence->ch_layout, &frame.ch_layout));
        checked(av_frame_get_buffer(silence.get(), 0));
        checked(av_samples_set_silence(silence->extended_data, 0, padding,
                                       static_cast<int>(output_.channels),
                                       static_cast<AVSampleFormat>(input_format_)));
        convert(silence->extended_data, padding);
    }
    std::int64_t input_position(const AVFrame& frame) {
        auto position =
            av_rescale_q(frame.pts, stream_.time_base, AVRational{1, frame.sample_rate});
        if (!cadence_.enabled) {
            return position;
        }
        if (dts_block_file_offset_ >= 0 && dts_block_origin_input_sample_) {
            // RFC 9559 10.3.5: only the first lace frame has a stored PTS;
            // following frames are contiguous. Counting decoded samples avoids
            // the demuxer's second rounding of an integer block duration.
            // Every new block still passes the unchanged grid/continuity checks.
            position =
                *dts_block_origin_input_sample_ + static_cast<std::int64_t>(dts_block_samples_);
        }
        const auto quantum =
            av_rescale_q_rnd(1, stream_.time_base, AVRational{1, frame.sample_rate}, AV_ROUND_UP);
        // A sample-accurate clock supplies the absolute resampler phase directly,
        // including trimmed first/final frames and variable codec block sizes.
        if (quantum <= 1) {
            return position;
        }
        if (cadence_.rebuild_from_start) {
            const AVRational input_clock{1, frame.sample_rate};
            const auto latest = av_rescale_q(
                static_cast<std::int64_t>(maximum_presentation_samples), output_clock, input_clock);
            const auto earliest = -av_rescale_q(static_cast<std::int64_t>(maximum_input_samples),
                                                output_clock, input_clock);
            require(position >= earliest && position <= latest, AudioDecodeFailure::unsupported,
                    "Matroska sample clock exceeds presentation bounds");
            // Every epoch starts at the same first audible frame. Count actual
            // decoded samples, including variable blocks and decoder-owned trim;
            // rounded timestamps validate continuity but cannot move that phase.
            if (!rebuild_origin_input_sample_) {
                rebuild_origin_input_sample_ = position;
            }
            const auto expected =
                *rebuild_origin_input_sample_ + static_cast<std::int64_t>(input_samples_);
            require(position >= expected - quantum - 1 && position <= expected + quantum + 1,
                    AudioDecodeFailure::unsupported,
                    "Matroska timestamp disagrees with rebuilt sample clock");
            return expected;
        }
        if (!cadence_.frame_samples) {
            require(output_.first_sample == 0 && frame.nb_samples > 2 * (quantum + 1),
                    AudioDecodeFailure::unsupported,
                    "canonical Matroska clock must start at zero with unambiguous frames");
            cadence_.frame_samples = frame.nb_samples;
            cadence_.origin_input_sample = position;
        }
        require(frame.nb_samples == cadence_.frame_samples, AudioDecodeFailure::unsupported,
                "canonical Matroska frame cadence changed");
        const auto distance = position - cadence_.origin_input_sample;
        const auto step = cadence_.frame_samples;
        const auto frame_number =
            distance >= 0 ? (distance + step / 2) / step : -((-distance + step / 2) / step);
        const auto snapped = cadence_.origin_input_sample + frame_number * step;
        require(position >= snapped - quantum - 1 && position <= snapped + quantum + 1,
                AudioDecodeFailure::unsupported,
                "Matroska timestamp is off the canonical frame grid");
        return snapped;
    }
    void consume(const AVFrame& frame) {
        reader_.check();
        require(frame.nb_samples > 0 && frame.nb_samples <= maximum_frame_samples &&
                    frame.pts != AV_NOPTS_VALUE && frame.sample_rate >= 8000 &&
                    frame.sample_rate <= 192000,
                AudioDecodeFailure::unsupported, "audio frame timestamp or length is unsupported");
        if (codec_.codec_id == AV_CODEC_ID_DTS) {
            require(codec_.profile == AV_PROFILE_DTS, AudioDecodeFailure::unsupported,
                    "DTS input is outside the tested core profile");
        }
        const auto position = input_position(frame);
        const auto timestamp =
            cadence_.enabled
                ? av_rescale_q(position, AVRational{1, frame.sample_rate}, output_clock)
                : av_rescale_q(frame.pts, stream_.time_base, output_clock);
        require(timestamp >= -static_cast<std::int64_t>(maximum_input_samples) &&
                    timestamp <= static_cast<std::int64_t>(maximum_presentation_samples),
                AudioDecodeFailure::unsupported,
                "audio frame timestamp exceeds presentation bounds");
        if (!resampler_) {
            configure(frame, timestamp, position);
        }
        require(frame.sample_rate == input_rate_ && frame.format == input_format_ &&
                    frame.ch_layout.order == AV_CHANNEL_ORDER_NATIVE &&
                    frame.ch_layout.u.mask == input_mask_ &&
                    frame.ch_layout.nb_channels == static_cast<int>(output_.channels),
                AudioDecodeFailure::unsupported, "audio format changed during decoding");
        const auto expected = origin_ + av_rescale_q(static_cast<std::int64_t>(input_samples_),
                                                     AVRational{1, input_rate_}, output_clock);
        const auto tolerance = static_cast<std::int64_t>(continuity_precision_samples_) + 1;
        require(timestamp >= expected - tolerance && timestamp <= expected + tolerance,
                AudioDecodeFailure::unsupported, "audio presentation timeline is discontinuous");
        record_dts_block_frame(position, frame.nb_samples);
        auto samples = frame.nb_samples;
        if (respect_opus_packet_duration_) {
            // MOV's final Opus sample may cover less than a complete codec frame.
            // The decoder retains that duration but still emits the whole frame;
            // trim before resampling rather than exposing encoder tail padding.
            const auto duration_samples =
                av_rescale_q(frame.duration, stream_.time_base, AVRational{1, input_rate_});
            require(duration_samples > 0 && duration_samples <= samples,
                    AudioDecodeFailure::unsupported, "Opus packet duration is unsupported");
            samples = static_cast<int>(duration_samples);
        }
        input_samples_ += static_cast<std::uint64_t>(samples);
        require(av_rescale_q(static_cast<std::int64_t>(input_samples_), AVRational{1, input_rate_},
                             output_clock) <= static_cast<std::int64_t>(maximum_input_samples),
                AudioDecodeFailure::too_large, "audio window preroll budget exceeded");
        convert(frame.extended_data, samples);
    }
    void convert(std::uint8_t* const* input, int samples) {
        const auto capacity = swr_get_out_samples(resampler_.get(), samples);
        require(capacity >= 0 && capacity <= 4 * maximum_frame_samples,
                AudioDecodeFailure::too_large, "audio resampler output is too large");
        std::vector<float> pcm(static_cast<std::size_t>(capacity) * output_.channels);
        auto* data = reinterpret_cast<std::uint8_t*>(pcm.data());
        const auto actual = swr_convert(resampler_.get(), &data, capacity,
                                        const_cast<const std::uint8_t**>(input), samples);
        checked(actual);
        copy_pcm(pcm, actual);
    }
    void copy_pcm(const std::vector<float>& pcm, int frames) {
        require(std::all_of(pcm.begin(),
                            pcm.begin() + static_cast<std::size_t>(frames) * output_.channels,
                            [](float sample) { return std::isfinite(sample); }),
                AudioDecodeFailure::malformed, "audio decoder returned nonfinite PCM");
        const auto begin = static_cast<std::int64_t>(output_.first_sample);
        const auto end = begin + count_;
        const auto frame_end = next_output_ + frames;
        const auto copy_begin = std::max(begin, next_output_);
        const auto copy_end = std::min(end, frame_end);
        if (copy_end > copy_begin) {
            const auto input_offset =
                static_cast<std::size_t>(copy_begin - next_output_) * output_.channels;
            const auto output_offset =
                static_cast<std::size_t>(copy_begin - begin) * output_.channels;
            const auto count = static_cast<std::size_t>(copy_end - copy_begin) * output_.channels;
            std::copy_n(pcm.begin() + input_offset, count, output_.samples.begin() + output_offset);
        }
        covered_ = std::max(covered_, frame_end);
        next_output_ = frame_end;
        complete_ = covered_ >= end;
    }
    void drain() {
        for (;;) {
            reader_.check();
            const auto result = avcodec_receive_frame(&codec_, frame_.get());
            if (result == AVERROR(EAGAIN) || result == AVERROR_EOF) {
                return;
            }
            checked(result);
            consume(*frame_);
            av_frame_unref(frame_.get());
        }
    }
    void flush_resampler() {
        if (resampler_ && !complete_) {
            convert(nullptr, 0);
        }
    }
};
} // namespace

AudioDecoderSource::AudioDecoderSource(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
AudioDecoderSource::~AudioDecoderSource() = default;
bool AudioDecoderSource::requires_start_rebuild() const noexcept {
    return impl_->cadence.rebuild_from_start;
}
void validate_audio_codec_backend() {
    validate_backend();
}

std::unique_ptr<AudioDecoderSource>
AudioDecoderSource::open(const MediaSource& source, const MediaReadContext& context,
                         std::optional<std::uint64_t> explicit_track_id,
                         bool canonical_matroska_clock) {
    require(source.size && source.read_at, AudioDecodeFailure::source_error,
            "audio source callbacks missing");
    validate_backend();
    check_stop(context);
    const auto size = source.size();
    check_stop(context);
    require(size > 0 && size <= maximum_source_bytes, AudioDecodeFailure::too_large,
            "audio source size is unsupported");
    auto snapshot = source;
    snapshot.size = [size] { return size; };
    const auto inventory = probe_media_tracks(snapshot, context);
    const auto selection = select_source_audio(inventory, explicit_track_id);
    require(selection.status != AudioSelectionStatus::selection_required,
            AudioDecodeFailure::selection_required, "source audio selection is required");
    require(selection.status == AudioSelectionStatus::selected,
            AudioDecodeFailure::invalid_selection, "selected source audio is unavailable");
    auto impl = std::make_unique<Impl>(snapshot, size);
    impl->selected = inventory.tracks[selection.track_index];
    impl->container = inventory.container;
    impl->cadence.enabled =
        canonical_matroska_clock && inventory.container == SourceContainer::matroska;
    validate_timeline(impl->selected, inventory.container);
    (void)source_codec(impl->selected, inventory.container);
    require(impl->selected.seek_preroll_ns <= 10ULL * 1'000'000'000,
            AudioDecodeFailure::unsupported, "audio preroll metadata exceeds window budget");
    Operation operation(impl->reader, context);
    impl->open_format();
    impl->open_codec();
    impl->index_matroska_audio();
    impl->reader.check();
    return std::unique_ptr<AudioDecoderSource>(new AudioDecoderSource(std::move(impl)));
}

AudioDecoderSource::IndexStatistics AudioDecoderSource::index_statistics() const noexcept {
    return {impl_->indexed_packet_count, impl_->audio_anchors.size(),
            impl_->audio_anchors.capacity() * sizeof(AudioRestartAnchor)};
}

DecodedAudioWindow AudioDecoderSource::read_window(std::uint64_t first_sample, std::uint32_t count,
                                                   const MediaReadContext& context) {
    require(impl_->usable && count > 0 && count <= maximum_window_samples &&
                first_sample <= maximum_presentation_samples - count,
            AudioDecodeFailure::unsupported, "invalid audio window request or failed decoder");
    try {
        require(!requires_start_rebuild() || first_sample + count <= maximum_start_rebuild_samples,
                AudioDecodeFailure::unsupported,
                "coarse Matroska window exceeds bounded start reconstruction");
        Operation operation(impl_->reader, context);
        impl_->seek(first_sample);
        DecodedAudioWindow window;
        window.source_track_id = impl_->selected.id;
        window.first_sample = first_sample;
        WindowDecoder decoder(*impl_->codec, *impl_->format->streams[impl_->stream_index],
                              impl_->reader, impl_->input_format, impl_->cadence, window, count,
                              impl_->container == SourceContainer::mp4 &&
                                  impl_->codec->codec_id == AV_CODEC_ID_OPUS);
        if (!impl_->audio_anchors.empty()) {
            MkvAudioScanner scanner(impl_->header_reader(), impl_->reader.size, impl_->selected.id);
            const auto& anchor = impl_->audio_anchors[impl_->anchor_begin];
            scanner.resume(anchor.cursor);
            auto* parameters = impl_->format->streams[impl_->stream_index]->codecpar;
            const bool packed_pcm = impl_->selected.codec.compare(0, 6, "A_PCM/") == 0;
            auto parser =
                packed_pcm ? nullptr : std::make_unique<CompressedPacketParser>(*parameters);
            if (parser && impl_->codec->codec_id == AV_CODEC_ID_VORBIS && anchor.ordinal != 0) {
                // Seed only the parser's prior overlap mode, so the anchor's
                // duration matches open even across a block-size transition.
                (void)parser->analyse(impl_->reader, anchor.previous);
            }
            auto position = anchor.position;
            auto ordinal = anchor.ordinal;
            decoder.run_indexed([&]() -> std::optional<IndexedAudioPacket> {
                MkvAudioPacket packet;
                MkvAudioCursor cursor;
                if (!scanner.next(packet, cursor)) {
                    require(ordinal == impl_->indexed_packet_count &&
                                position == impl_->indexed_end_position,
                            AudioDecodeFailure::malformed, "indexed audio source end changed");
                    return {};
                }
                require(ordinal < impl_->indexed_packet_count, AudioDecodeFailure::malformed,
                        "indexed audio source packet count changed");
                auto analysed =
                    impl_->analyse_indexed_packet(packet, position, ordinal == 0, parser.get());
                position += analysed.packet.samples;
                ++ordinal;
                return analysed.packet;
            });
        } else {
            decoder.run(*impl_->format);
        }
        impl_->reader.check();
        return window;
    } catch (...) {
        impl_->usable = false;
        throw;
    }
}
} // namespace send_airplay2::detail
