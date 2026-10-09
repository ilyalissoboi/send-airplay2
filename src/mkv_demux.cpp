// SPDX-License-Identifier: Apache-2.0
// Reads Matroska (RFC 9559; EBML, RFC 8794) block tables for the HLS remux
// (D60). Written from the specifications; no third-party code.
#include "mkv_demux.h"
#include "sample_entries.h"
#include "text_tracks.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace send_airplay2::detail {
namespace {
// Element IDs (RFC 9559 section 5.1), with their EBML length markers.
constexpr std::uint32_t id_ebml = 0x1A45DFA3;
constexpr std::uint32_t id_doc_type = 0x4282;
constexpr std::uint32_t id_segment = 0x18538067;
constexpr std::uint32_t id_seek_head = 0x114D9B74;
constexpr std::uint32_t id_info = 0x1549A966;
constexpr std::uint32_t id_duration = 0x4489;
constexpr std::uint32_t id_timestamp_scale = 0x2AD7B1;
constexpr std::uint32_t id_tracks = 0x1654AE6B;
constexpr std::uint32_t id_track_entry = 0xAE;
constexpr std::uint32_t id_track_number = 0xD7;
constexpr std::uint32_t id_track_type = 0x83;
constexpr std::uint32_t id_flag_enabled = 0xB9;
constexpr std::uint32_t id_flag_default = 0x88;
constexpr std::uint32_t id_codec_id = 0x86;
constexpr std::uint32_t id_codec_private = 0x63A2;
constexpr std::uint32_t id_default_duration = 0x23E383;
constexpr std::uint32_t id_language = 0x22B59C;
constexpr std::uint32_t id_content_encodings = 0x6D80;
constexpr std::uint32_t id_codec_delay = 0x56AA;
constexpr std::uint32_t id_name = 0x536E;
constexpr std::uint32_t id_language_bcp47 = 0x22B59D;
constexpr std::uint32_t id_flag_forced = 0x55AA;
constexpr std::uint32_t id_flag_hearing_impaired = 0x55AB;
constexpr std::uint32_t id_video = 0xE0;
constexpr std::uint32_t id_pixel_width = 0xB0;
constexpr std::uint32_t id_pixel_height = 0xBA;
constexpr std::uint32_t id_display_width = 0x54B0;
constexpr std::uint32_t id_display_height = 0x54BA;
constexpr std::uint32_t id_display_unit = 0x54B2;
constexpr std::uint32_t id_audio = 0xE1;
constexpr std::uint32_t id_sampling_frequency = 0xB5;
constexpr std::uint32_t id_channels = 0x9F;
constexpr std::uint32_t id_cluster = 0x1F43B675;
constexpr std::uint32_t id_cluster_timestamp = 0xE7;
constexpr std::uint32_t id_simple_block = 0xA3;
constexpr std::uint32_t id_block_group = 0xA0;
constexpr std::uint32_t id_block = 0xA1;
constexpr std::uint32_t id_block_duration = 0x9B;
constexpr std::uint32_t id_reference_block = 0xFB;
constexpr std::uint32_t id_cues = 0x1C53BB6B;
constexpr std::uint32_t id_chapters = 0x1043A770;
constexpr std::uint32_t id_tags = 0x1254C367;
constexpr std::uint32_t id_attachments = 0x1941A469;

constexpr std::uint64_t track_type_video = 1;
constexpr std::uint64_t track_type_audio = 2;
constexpr std::uint64_t track_type_subtitle = 17;
constexpr std::uint32_t max_text_frame_bytes = 64 * 1024;       // Larger cues are skipped.
constexpr std::uint64_t max_text_bytes_per_track = 16ULL << 20; // Beyond it, the track is dropped.
constexpr std::uint64_t untimed_cue_us = 5'000'000; // A cue without a duration or successor.
constexpr std::uint64_t default_timestamp_scale = 1'000'000; // Nanoseconds per tick.
constexpr std::uint64_t nanoseconds_per_second = 1'000'000'000;
constexpr std::uint64_t max_metadata_bytes = 16ULL << 20;
constexpr std::size_t max_frames_per_track = 4'000'000;
constexpr std::size_t window_bytes = 16 * 1024;
constexpr std::uint32_t movie_timescale = 1000; // No edit lists use it.
constexpr std::uint8_t simple_block_keyframe = 0x80;
constexpr std::uint8_t block_lacing_mask = 0x06;
constexpr std::uint8_t xiph_lacing = 0x02;
constexpr std::uint8_t fixed_lacing = 0x04;
constexpr std::uint8_t ebml_lacing = 0x06;
constexpr std::size_t dolby_header_bytes = 16; // Covers both sync frame headers' fields.

[[noreturn]] void malformed(const std::string& what) {
    throw RemuxException(RemuxFailure::malformed, "MKV: " + what);
}
[[noreturn]] void unsupported(const std::string& what) {
    throw RemuxException(RemuxFailure::unsupported, "MKV: " + what);
}

/// Cached reads of block headers: one window of the file at a time, so that
/// the small reads of a cluster scan reach the source in 16 KiB requests.
class WindowReader {
public:
    WindowReader(const RandomReader& read, std::uint64_t file_size)
        : read_(read), file_size_(file_size) {}
    std::uint8_t byte(std::uint64_t position) {
        if (position >= file_size_) {
            malformed("element past the end of the file");
        }
        if (position < start_ || position >= start_ + window_.size()) {
            start_ = position;
            window_.resize(static_cast<std::size_t>(
                std::min<std::uint64_t>(window_bytes, file_size_ - position)));
            read_(start_, window_.data(), window_.size());
        }
        return window_[static_cast<std::size_t>(position - start_)];
    }
    void copy(std::uint64_t position, std::uint8_t* output, std::size_t length) {
        if (position > file_size_ || length > file_size_ - position) {
            malformed("element past the end of the file");
        }
        read_(position, output, length);
    }
    [[nodiscard]] std::uint64_t file_size() const {
        return file_size_;
    }

private:
    const RandomReader& read_;
    std::uint64_t file_size_;
    std::uint64_t start_ = 0;
    Bytes window_;
};

/// Byte access for the EBML decoders: the window reader, or a buffer.
struct BufferAccess {
    const Bytes& data;
    std::uint8_t byte(std::uint64_t position) const {
        if (position >= data.size()) {
            malformed("element past the end of its parent");
        }
        return data[static_cast<std::size_t>(position)];
    }
};

/// An EBML variable-length integer (RFC 8794 4): the count of leading zero
/// bits in the first byte gives its length. IDs keep the length marker;
/// sizes and values drop it.
template <class Access>
std::uint64_t read_vint(Access& access, std::uint64_t& position, std::size_t max_length,
                        bool keep_marker, bool* all_ones = nullptr) {
    const auto first = access.byte(position);
    std::size_t length = 1;
    std::uint8_t marker = 0x80;
    while (length <= max_length && !(first & marker)) {
        ++length;
        marker = static_cast<std::uint8_t>(marker >> 1);
    }
    if (length > max_length) {
        malformed("invalid variable-length integer");
    }
    std::uint64_t value = keep_marker ? first : (first & (marker - 1U));
    bool ones = (first & (marker - 1U)) == (marker - 1U);
    for (std::size_t index = 1; index < length; ++index) {
        const auto next = access.byte(position + index);
        value = (value << 8) | next;
        ones = ones && next == 0xff;
    }
    position += length;
    if (all_ones) {
        *all_ones = ones;
    }
    return value;
}

struct Element {
    std::uint32_t id = 0;
    std::uint64_t data = 0; ///< Offset of the element's data.
    std::uint64_t size = 0;
    bool unknown_size = false;
    [[nodiscard]] std::uint64_t end() const {
        return data + size;
    }
};

/// The element header at `position`; its data must end by `limit` unless its
/// size is unknown (then it extends to `limit`).
template <class Access>
Element read_element(Access& access, std::uint64_t position, std::uint64_t limit) {
    Element element;
    element.id = static_cast<std::uint32_t>(read_vint(access, position, 4, true));
    bool unknown = false;
    element.size = read_vint(access, position, 8, false, &unknown);
    element.data = position;
    if (position > limit) {
        malformed("element header past its parent");
    }
    element.unknown_size = unknown;
    if (unknown) {
        element.size = limit - position;
    } else if (element.size > limit - position) {
        malformed("element larger than its parent");
    }
    return element;
}

/// Children of an in-memory element's data [begin, end).
std::vector<Element> children(const Bytes& data, std::uint64_t begin, std::uint64_t end) {
    BufferAccess access{data};
    std::vector<Element> elements;
    for (auto position = begin; position < end;) {
        const auto element = read_element(access, position, end);
        elements.push_back(element);
        position = element.end();
    }
    return elements;
}

std::uint64_t unsigned_value(const Bytes& data, const Element& element) {
    if (element.size > 8) {
        malformed("unsigned integer longer than 8 bytes");
    }
    std::uint64_t value = 0;
    for (std::uint64_t index = 0; index < element.size; ++index) {
        value = (value << 8) | data[static_cast<std::size_t>(element.data + index)];
    }
    return value;
}

double float_value(const Bytes& data, const Element& element) {
    const auto bits = unsigned_value(data, element);
    if (element.size == 4) {
        const auto narrow = static_cast<std::uint32_t>(bits);
        float value = 0;
        std::memcpy(&value, &narrow, sizeof value);
        return value;
    }
    if (element.size == 8) {
        double value = 0;
        std::memcpy(&value, &bits, sizeof value);
        return value;
    }
    if (element.size == 0) {
        return 0;
    }
    malformed("float of an invalid size");
}

std::string string_value(const Bytes& data, const Element& element) {
    std::string value(data.begin() + static_cast<std::ptrdiff_t>(element.data),
                      data.begin() + static_cast<std::ptrdiff_t>(element.end()));
    const auto terminator = value.find('\0');
    if (terminator != std::string::npos) {
        value.resize(terminator); // Strings may be zero-padded (RFC 8794 7.4).
    }
    return value;
}

Bytes binary_value(const Bytes& data, const Element& element) {
    return {data.begin() + static_cast<std::ptrdiff_t>(element.data),
            data.begin() + static_cast<std::ptrdiff_t>(element.end())};
}

/// The fields of one TrackEntry that the remux uses.
struct TrackInfo {
    std::uint64_t number = 0;
    std::uint64_t type = 0;
    bool enabled = true;
    bool default_track = true;
    bool encoded = false; // ContentEncodings present.
    std::string codec_id;
    Bytes codec_private;
    std::uint64_t default_duration_ns = 0;
    std::uint64_t codec_delay_ns = 0; // Priming to discard (RFC 9559 CodecDelay).
    std::string language = "eng";     // The Matroska default.
    std::string language_bcp47;
    std::string name;
    bool forced = false;
    bool hearing_impaired = false;
    std::uint64_t pixel_width = 0;
    std::uint64_t pixel_height = 0;
    std::uint64_t display_width = 0;
    std::uint64_t display_height = 0;
    std::uint64_t display_unit = 0; // 0 means pixels.
    double sampling_frequency = 8000.0;
    std::uint64_t channels = 1;
};

TrackInfo read_track_entry(const Bytes& data, const Element& entry) {
    TrackInfo track;
    for (const auto& field : children(data, entry.data, entry.end())) {
        switch (field.id) {
        case id_track_number:
            track.number = unsigned_value(data, field);
            break;
        case id_track_type:
            track.type = unsigned_value(data, field);
            break;
        case id_flag_enabled:
            track.enabled = unsigned_value(data, field) != 0;
            break;
        case id_flag_default:
            track.default_track = unsigned_value(data, field) != 0;
            break;
        case id_codec_id:
            track.codec_id = string_value(data, field);
            break;
        case id_codec_private:
            track.codec_private = binary_value(data, field);
            break;
        case id_default_duration:
            track.default_duration_ns = unsigned_value(data, field);
            break;
        case id_codec_delay:
            track.codec_delay_ns = unsigned_value(data, field);
            break;
        case id_language:
            track.language = string_value(data, field);
            break;
        case id_language_bcp47:
            track.language_bcp47 = string_value(data, field);
            break;
        case id_name:
            track.name = string_value(data, field);
            break;
        case id_flag_forced:
            track.forced = unsigned_value(data, field) != 0;
            break;
        case id_flag_hearing_impaired:
            track.hearing_impaired = unsigned_value(data, field) != 0;
            break;
        case id_content_encodings:
            track.encoded = true;
            break;
        case id_video:
            for (const auto& video : children(data, field.data, field.end())) {
                switch (video.id) {
                case id_pixel_width:
                    track.pixel_width = unsigned_value(data, video);
                    break;
                case id_pixel_height:
                    track.pixel_height = unsigned_value(data, video);
                    break;
                case id_display_width:
                    track.display_width = unsigned_value(data, video);
                    break;
                case id_display_height:
                    track.display_height = unsigned_value(data, video);
                    break;
                case id_display_unit:
                    track.display_unit = unsigned_value(data, video);
                    break;
                default:
                    break;
                }
            }
            break;
        case id_audio:
            for (const auto& audio : children(data, field.data, field.end())) {
                if (audio.id == id_sampling_frequency) {
                    track.sampling_frequency = float_value(data, audio);
                } else if (audio.id == id_channels) {
                    track.channels = unsigned_value(data, audio);
                }
            }
            break;
        default:
            break;
        }
    }
    return track;
}

bool remuxable_video(const TrackInfo& track) {
    return track.codec_id == "V_MPEG4/ISO/AVC" || track.codec_id == "V_MPEGH/ISO/HEVC";
}
bool aac_codec(const std::string& codec_id) {
    return codec_id == "A_AAC" || codec_id == "A_AAC/MPEG4/LC" || codec_id == "A_AAC/MPEG2/LC" ||
           codec_id == "A_AAC/MPEG4/MAIN" || codec_id == "A_AAC/MPEG2/MAIN";
}
bool remuxable_audio(const TrackInfo& track) {
    return aac_codec(track.codec_id) || track.codec_id == "A_AC3" || track.codec_id == "A_EAC3";
}

/// One frame as stored: where its bytes are and its presentation time in
/// TimestampScale ticks. Frames after the first in a laced block have no
/// timestamp of their own.
struct Frame {
    std::uint64_t offset = 0;
    std::uint32_t size = 0;
    std::int64_t timestamp = 0;
    bool first_in_block = true;
    bool keyframe = false;
};

struct SelectedTrack {
    TrackInfo info;
    std::vector<Frame> frames;
    std::optional<std::uint64_t> last_block_duration; // Ticks; from BlockDuration.
    std::vector<std::uint64_t> durations; // Text tracks only: BlockDuration per frame, 0 if none.
};

bool text_codec(const std::string& codec_id) {
    return codec_id == "S_TEXT/UTF8" || codec_id == "S_TEXT/ASCII" || codec_id == "S_TEXT/WEBVTT" ||
           codec_id == "S_TEXT/ASS" || codec_id == "S_TEXT/SSA";
}

/** The tracks to remux, in order: the first enabled H.264/HEVC video track,
 * the audio track (a default-flagged remuxable one before the first), and
 * every enabled text subtitle track. Text subtitles are optional extras:
 * tracks that cannot be served (bitmap formats, ContentEncodings) are left
 * out, not refused. */
std::vector<TrackInfo> select_tracks(const std::vector<TrackInfo>& infos) {
    const TrackInfo* video = nullptr;
    const TrackInfo* audio = nullptr;
    bool saw_video = false;
    bool saw_audio = false;
    for (const auto& info : infos) {
        if (!info.enabled) {
            continue;
        }
        if (info.type == track_type_video) {
            saw_video = true;
            if (!video && remuxable_video(info)) {
                video = &info;
            }
        } else if (info.type == track_type_audio) {
            saw_audio = true;
            if (remuxable_audio(info) &&
                (!audio || (info.default_track && !audio->default_track))) {
                audio = &info;
            }
        }
    }
    if (!video) {
        unsupported(saw_video ? "video codec is not remuxable (H.264 or HEVC only)"
                              : "no video track");
    }
    if (saw_audio && !audio) {
        unsupported("audio codec is not remuxable (AAC, AC-3 or E-AC-3 only)");
    }
    for (const auto* info : {video, audio}) {
        if (info && info->encoded) {
            unsupported("compressed or encrypted track (ContentEncodings)");
        }
    }
    std::vector<TrackInfo> selected{*video};
    if (audio) {
        selected.push_back(*audio);
    }
    for (const auto& info : infos) {
        if (info.type == track_type_subtitle && info.enabled && !info.encoded &&
            text_codec(info.codec_id)) {
            selected.push_back(info);
        }
    }
    return selected;
}

/// Parses one Block or SimpleBlock (RFC 9559 10) whose data is [begin, end)
/// and appends its frames to the selected track it belongs to.
void read_block(WindowReader& reader, std::uint64_t begin, std::uint64_t end,
                std::int64_t cluster_timestamp, bool simple, bool referenced,
                std::vector<SelectedTrack*>& tracks, std::optional<std::uint64_t> duration) {
    auto position = begin;
    const auto number = read_vint(reader, position, 8, false);
    SelectedTrack* track = nullptr;
    for (auto* candidate : tracks) {
        if (candidate->info.number == number) {
            track = candidate;
        }
    }
    if (!track) {
        return;
    }
    if (end - position < 3) {
        malformed("block shorter than its header");
    }
    const auto relative =
        static_cast<std::int16_t>((reader.byte(position) << 8) | reader.byte(position + 1));
    const auto flags = reader.byte(position + 2);
    position += 3;
    const auto timestamp = cluster_timestamp + relative;
    if (timestamp < 0) {
        unsupported("negative block timestamps");
    }
    const bool keyframe = simple ? (flags & simple_block_keyframe) != 0 : !referenced;
    const auto lacing = static_cast<std::uint8_t>(flags & block_lacing_mask);

    std::vector<std::uint64_t> sizes;
    if (lacing == 0) {
        sizes.push_back(end - position);
    } else {
        if (track->info.type == track_type_video) {
            unsupported("laced video frames");
        }
        const std::size_t count = static_cast<std::size_t>(reader.byte(position++)) + 1;
        if (lacing == xiph_lacing) {
            for (std::size_t index = 0; index + 1 < count; ++index) {
                std::uint64_t size = 0;
                std::uint8_t byte = 0;
                do {
                    if (position >= end) {
                        malformed("Xiph lace sizes past the block");
                    }
                    byte = reader.byte(position++);
                    size += byte;
                } while (byte == 0xff);
                sizes.push_back(size);
            }
        } else if (lacing == ebml_lacing) {
            auto size = read_vint(reader, position, 8, false);
            sizes.push_back(size);
            for (std::size_t index = 1; index + 1 < count; ++index) {
                const auto start = position;
                const auto raw = read_vint(reader, position, 8, false);
                // Signed: subtract half the range of a vint of this length.
                const auto length = position - start;
                const auto bias = (1ULL << (7 * length - 1)) - 1;
                const auto delta = static_cast<std::int64_t>(raw) - static_cast<std::int64_t>(bias);
                if ((delta < 0 && static_cast<std::uint64_t>(-delta) > size)) {
                    malformed("EBML lace size below zero");
                }
                size = static_cast<std::uint64_t>(static_cast<std::int64_t>(size) + delta);
                sizes.push_back(size);
            }
        } else if (lacing == fixed_lacing) {
            if (position > end || (end - position) % count) {
                malformed("fixed-size lace does not divide its block");
            }
            sizes.assign(count, (end - position) / count);
        }
        if (lacing != fixed_lacing) {
            std::uint64_t listed = 0;
            for (const auto size : sizes) {
                listed += size;
            }
            if (position > end || listed > end - position) {
                malformed("lace sizes exceed their block");
            }
            sizes.push_back(end - position - listed);
        }
    }
    for (std::size_t index = 0; index < sizes.size(); ++index) {
        if (sizes[index] > std::numeric_limits<std::uint32_t>::max()) {
            throw RemuxException(RemuxFailure::too_large, "MKV: frame larger than 4 GiB");
        }
        if (track->frames.size() == max_frames_per_track) {
            throw RemuxException(RemuxFailure::too_large, "MKV: more than 4000000 frames");
        }
        Frame frame;
        frame.offset = position;
        frame.size = static_cast<std::uint32_t>(sizes[index]);
        frame.timestamp = timestamp;
        frame.first_in_block = index == 0;
        frame.keyframe = keyframe;
        track->frames.push_back(frame);
        if (track->info.type == track_type_subtitle) {
            track->durations.push_back(duration.value_or(0));
        }
        position += sizes[index];
    }
    track->last_block_duration = duration;
}

bool level1(std::uint32_t id) {
    return id == id_cluster || id == id_cues || id == id_tags || id == id_chapters ||
           id == id_attachments || id == id_seek_head || id == id_info || id == id_tracks;
}

/// Reads one cluster's blocks. Returns where the next top-level element
/// starts: the end of a sized cluster, or the first level-1 element inside an
/// unknown-size one.
std::uint64_t read_cluster(WindowReader& reader, const Element& cluster,
                           std::vector<SelectedTrack*>& tracks,
                           std::int64_t* cluster_timestamp = nullptr) {
    std::int64_t timestamp = 0;
    auto position = cluster.data;
    while (position < cluster.end()) {
        const auto child = read_element(reader, position, cluster.end());
        if (cluster.unknown_size && level1(child.id)) {
            return position;
        }
        if (child.unknown_size) {
            malformed("unknown-size element inside a cluster");
        }
        if (child.id == id_cluster_timestamp) {
            std::uint64_t value = 0;
            for (std::uint64_t index = 0; index < child.size && index < 8; ++index) {
                value = (value << 8) | reader.byte(child.data + index);
            }
            if (value > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
                malformed("cluster timestamp out of range");
            }
            timestamp = static_cast<std::int64_t>(value);
            if (cluster_timestamp) {
                *cluster_timestamp = timestamp;
            }
        } else if (child.id == id_simple_block) {
            read_block(reader, child.data, child.end(), timestamp, true, false, tracks,
                       std::nullopt);
        } else if (child.id == id_block_group) {
            std::optional<Element> block;
            std::optional<std::uint64_t> duration;
            bool referenced = false;
            for (auto inner = child.data; inner < child.end();) {
                const auto field = read_element(reader, inner, child.end());
                if (field.id == id_block) {
                    block = field;
                } else if (field.id == id_reference_block) {
                    referenced = true;
                } else if (field.id == id_block_duration) {
                    std::uint64_t value = 0;
                    for (std::uint64_t index = 0; index < field.size && index < 8; ++index) {
                        value = (value << 8) | reader.byte(field.data + index);
                    }
                    duration = value;
                }
                inner = field.end();
            }
            if (block) {
                read_block(reader, block->data, block->end(), timestamp, false, referenced, tracks,
                           duration);
            }
        }
        position = child.end();
    }
    return cluster.end();
}

Bytes load(WindowReader& reader, const Element& element) {
    if (element.size > max_metadata_bytes) {
        throw RemuxException(RemuxFailure::too_large, "MKV: track metadata larger than 16 MiB");
    }
    Bytes data(static_cast<std::size_t>(element.size));
    reader.copy(element.data, data.data(), data.size());
    return data;
}

/// ns -> ticks of `timescale`, rounded, without overflowing for any
/// timestamp below 2^63 ns.
std::uint64_t scale_time(std::uint64_t nanoseconds, std::uint64_t timescale) {
    return nanoseconds / nanoseconds_per_second * timescale +
           ((nanoseconds % nanoseconds_per_second) * timescale + nanoseconds_per_second / 2) /
               nanoseconds_per_second;
}

std::uint64_t to_nanoseconds(std::int64_t ticks, std::uint64_t timestamp_scale) {
    const auto value = static_cast<std::uint64_t>(ticks);
    if (timestamp_scale && value > std::numeric_limits<std::uint64_t>::max() / timestamp_scale) {
        unsupported("timestamps beyond 2^64 ns");
    }
    return value * timestamp_scale;
}

Mp4Track video_track(const SelectedTrack& selected, std::uint64_t timestamp_scale) {
    const auto& info = selected.info;
    if (selected.frames.empty()) {
        malformed("selected video track has no frames");
    }
    if (info.codec_private.empty()) {
        malformed("video track without a decoder configuration");
    }
    Mp4Track track;
    track.kind = TrackKind::video;
    track.timescale = static_cast<std::uint32_t>(nanoseconds_per_second / timestamp_scale);
    track.codec = info.codec_id == "V_MPEG4/ISO/AVC" ? "avc1" : "hvc1";
    track.language = packed_language(info.language);
    const auto width =
        static_cast<std::uint16_t>(std::min<std::uint64_t>(info.pixel_width, 0xffff));
    const auto height =
        static_cast<std::uint16_t>(std::min<std::uint64_t>(info.pixel_height, 0xffff));
    // A missing display dimension defaults to its pixel dimension, each on its
    // own (RFC 9559 5.1.4.1.28); other display units carry no pixel size.
    const bool pixels = info.display_unit == 0;
    const auto display_width =
        static_cast<std::uint32_t>(pixels && info.display_width ? info.display_width : width);
    const auto display_height =
        static_cast<std::uint32_t>(pixels && info.display_height ? info.display_height : height);
    track.sample_entry = visual_sample_entry(track.codec, width, height, display_width,
                                             display_height, info.codec_private);
    track.track_header = track_header_box(TrackKind::video, 1, display_width, display_height);

    // Decode times are the presentation times in increasing order. A frame
    // decoded before its presentation slot would get a negative composition
    // offset; instead every presentation time moves later by the largest such
    // lead (the reorder delay), and an edit list moves the timeline back, as
    // MP4 writers store B-frames (14496-12 8.6.1.3).
    std::vector<std::int64_t> decode;
    decode.reserve(selected.frames.size());
    for (const auto& frame : selected.frames) {
        decode.push_back(frame.timestamp);
    }
    std::sort(decode.begin(), decode.end());
    std::int64_t reorder_delay = 0;
    for (std::size_t index = 0; index < selected.frames.size(); ++index) {
        reorder_delay = std::max(reorder_delay, decode[index] - selected.frames[index].timestamp);
    }
    track.samples.resize(selected.frames.size());
    for (std::size_t index = 0; index < selected.frames.size(); ++index) {
        const auto& frame = selected.frames[index];
        auto& sample = track.samples[index];
        sample.offset = frame.offset;
        sample.size = frame.size;
        sample.sync = frame.keyframe;
        sample.decode_time = static_cast<std::uint64_t>(decode[index]);
        const auto offset = frame.timestamp + reorder_delay - decode[index];
        if (offset > std::numeric_limits<std::int32_t>::max()) {
            unsupported("frame reordering beyond 32-bit composition offsets");
        }
        sample.composition_offset = static_cast<std::int32_t>(offset);
    }
    if (reorder_delay > 0) {
        track.edits.push_back({0, reorder_delay, 1, 0});
    }
    const auto default_ticks = info.default_duration_ns / timestamp_scale;
    for (std::size_t index = 0; index + 1 < track.samples.size(); ++index) {
        track.samples[index].duration = static_cast<std::uint32_t>(
            track.samples[index + 1].decode_time - track.samples[index].decode_time);
    }
    auto& last = track.samples.back();
    if (selected.last_block_duration) {
        last.duration = static_cast<std::uint32_t>(*selected.last_block_duration);
    } else if (default_ticks) {
        last.duration = static_cast<std::uint32_t>(default_ticks);
    } else {
        last.duration =
            track.samples.size() > 1 ? track.samples[track.samples.size() - 2].duration : 1;
    }
    return track;
}

Mp4Track audio_track(const SelectedTrack& selected, std::uint64_t timestamp_scale,
                     WindowReader& reader) {
    const auto& info = selected.info;
    if (selected.frames.empty()) {
        malformed("selected audio track has no frames");
    }
    Mp4Track track;
    track.kind = TrackKind::audio;
    track.language = packed_language(info.language);
    track.track_header = track_header_box(TrackKind::audio, 2, 0, 0);
    std::uint32_t frame_samples = 0;
    std::uint32_t sample_rate = 0;
    if (aac_codec(info.codec_id)) {
        Bytes config = info.codec_private;
        if (config.empty()) {
            const std::uint8_t object_type =
                info.codec_id.find("MAIN") != std::string::npos ? 1 : 2;
            config = aac_config(object_type, static_cast<std::uint32_t>(info.sampling_frequency),
                                static_cast<std::uint8_t>(info.channels));
        }
        const auto aac = read_aac_config(config);
        sample_rate = aac.sample_rate;
        frame_samples = aac.frame_samples;
        const auto channels =
            aac.channels ? aac.channels : static_cast<std::uint8_t>(info.channels);
        track.codec = "mp4a";
        track.sample_entry = aac_sample_entry(channels, sample_rate, config);
    } else {
        const auto& first = selected.frames.front();
        std::array<std::uint8_t, dolby_header_bytes> header{};
        if (first.size < header.size()) {
            malformed("AC-3 frame shorter than its header");
        }
        reader.copy(first.offset, header.data(), header.size());
        const auto frame = read_dolby_frame(header.data(), header.size());
        if (frame.enhanced != (info.codec_id == "A_EAC3")) {
            malformed("AC-3 codec ID does not match its bitstream");
        }
        if (frame.enhanced && first.size > frame.frame_bytes) {
            // More sync frames in the first block: a dependent substream.
            std::array<std::uint8_t, dolby_header_bytes> next{};
            if (first.size - frame.frame_bytes >= next.size()) {
                reader.copy(first.offset + frame.frame_bytes, next.data(), next.size());
                const auto following = read_dolby_frame(next.data(), next.size());
                if (following.stream_type == 1) {
                    unsupported("E-AC-3 with dependent substreams");
                }
            }
        }
        sample_rate = frame.sample_rate;
        frame_samples = frame.samples;
        track.codec = frame.enhanced ? "ec-3" : "ac-3";
        track.sample_entry = frame.enhanced ? eac3_sample_entry(frame) : ac3_sample_entry(frame);
    }
    if (sample_rate == 0) {
        malformed("audio track without a sample rate");
    }
    track.timescale = sample_rate;
    if (info.codec_delay_ns) {
        // The decoder's priming samples precede the presentation; skip them.
        const auto priming = scale_time(info.codec_delay_ns, sample_rate);
        track.edits.push_back({0, static_cast<std::int64_t>(priming), 1, 0});
    }

    // One codec frame per sample on a continuous timeline. Block timestamps
    // are rounded to TimestampScale, so the timeline follows a block's own
    // timestamp only when they differ by more than that rounding (half a tick)
    // plus one sample: regular streams keep exact frame durations, irregular
    // ones follow the source. Laced frames have no timestamp of their own.
    const auto rounding_samples = scale_time(timestamp_scale / 2, sample_rate) + 1;
    track.samples.resize(selected.frames.size());
    std::uint64_t expected = 0;
    for (std::size_t index = 0; index < selected.frames.size(); ++index) {
        const auto& frame = selected.frames[index];
        if (frame.first_in_block) {
            const auto block_time =
                scale_time(to_nanoseconds(frame.timestamp, timestamp_scale), sample_rate);
            const auto distance =
                block_time > expected ? block_time - expected : expected - block_time;
            if (index == 0 || distance > rounding_samples) {
                expected = block_time;
            }
        }
        auto& sample = track.samples[index];
        sample.offset = frame.offset;
        sample.size = frame.size;
        sample.sync = true;
        sample.decode_time = expected;
        expected += frame_samples;
    }
    for (std::size_t index = 0; index + 1 < track.samples.size(); ++index) {
        const auto delta = track.samples[index + 1].decode_time - track.samples[index].decode_time;
        track.samples[index].duration = static_cast<std::uint32_t>(delta);
    }
    track.samples.back().duration = frame_samples;
    return track;
}
/// Cues from a text track's blocks: payloads converted to WebVTT cue text,
/// times from block timestamps and BlockDuration (else DefaultDuration,
/// else the next cue's start, else 5 s). Empty cues are skipped; a track whose
/// payloads exceed 16 MiB, or that ends up empty, is left out.
std::optional<TextTrack> text_track(const SelectedTrack& selected, std::uint64_t timestamp_scale,
                                    WindowReader& reader) {
    const auto& info = selected.info;
    TextTrack track;
    track.name = info.name;
    track.language = language_tag(info.language, info.language_bcp47);
    track.default_track = info.default_track;
    track.forced = info.forced;
    track.hearing_impaired = info.hearing_impaired;
    std::uint64_t total_bytes = 0;
    const auto to_us = [timestamp_scale](std::uint64_t ticks) {
        return ticks * timestamp_scale / 1000; // TimestampScale is in nanoseconds.
    };
    for (std::size_t index = 0; index < selected.frames.size(); ++index) {
        const auto& frame = selected.frames[index];
        if (frame.size == 0 || frame.size > max_text_frame_bytes) {
            continue;
        }
        total_bytes += frame.size;
        if (total_bytes > max_text_bytes_per_track) {
            return std::nullopt;
        }
        std::string payload(frame.size, '\0');
        reader.copy(frame.offset, reinterpret_cast<std::uint8_t*>(payload.data()), payload.size());
        std::string text;
        if (info.codec_id == "S_TEXT/ASS" || info.codec_id == "S_TEXT/SSA") {
            text = ass_to_webvtt(payload);
        } else if (info.codec_id == "S_TEXT/WEBVTT") {
            text = webvtt_cue_text(payload); // Already WebVTT cue text.
        } else {
            text = subrip_to_webvtt(payload); // S_TEXT/UTF8 and S_TEXT/ASCII.
        }
        if (text.empty()) {
            continue;
        }
        TextCue cue;
        cue.start_us = to_us(static_cast<std::uint64_t>(frame.timestamp));
        std::uint64_t duration_us = 0;
        if (index < selected.durations.size() && selected.durations[index]) {
            duration_us = to_us(selected.durations[index]);
        } else if (info.default_duration_ns) {
            duration_us = info.default_duration_ns / 1000;
        } else if (index + 1 < selected.frames.size()) {
            const auto next =
                to_us(static_cast<std::uint64_t>(selected.frames[index + 1].timestamp));
            duration_us = next > cue.start_us ? next - cue.start_us : 0;
        } else {
            duration_us = untimed_cue_us;
        }
        cue.end_us = cue.start_us + std::max<std::uint64_t>(duration_us, 1000);
        cue.text = std::move(text);
        track.cues.push_back(std::move(cue));
    }
    if (track.cues.empty()) {
        return std::nullopt;
    }
    std::stable_sort(track.cues.begin(), track.cues.end(),
                     [](const TextCue& a, const TextCue& b) { return a.start_us < b.start_us; });
    return track;
}

// --- Indexed reading (D60): segments planned from Cues, read on demand ------

constexpr std::uint32_t id_seek = 0x4DBB;
constexpr std::uint32_t id_seek_id = 0x53AB;
constexpr std::uint32_t id_seek_position = 0x53AC;
constexpr std::uint32_t id_cue_point = 0xBB;
constexpr std::uint32_t id_cue_time = 0xB3;
constexpr std::uint32_t id_cue_track_positions = 0xB7;
constexpr std::uint32_t id_cue_track = 0xF7;
constexpr std::uint32_t id_cue_cluster_position = 0xF1;
constexpr std::uint64_t max_cues_bytes = 32ULL << 20;
constexpr std::uint64_t segment_target_ns = 6'000'000'000;       // Apple HLS 7.5.
constexpr std::uint64_t max_indexed_segment_ns = 20'000'000'000; // Longer: full scan.
constexpr std::uint64_t scan_margin_ns = 2'000'000'000; // Interleaving slack around a segment.

/// A video keyframe the Cues name: its timestamp and its cluster's offset.
struct CuePoint {
    std::int64_t ticks = 0;
    std::uint64_t cluster = 0; // Absolute file offset of the cluster.
};

/// What the level-1 elements before the first cluster say.
struct MkvLayout {
    Element segment;
    std::uint64_t timestamp_scale = default_timestamp_scale;
    std::vector<TrackInfo> selected; // select_tracks() order.
    std::uint64_t first_cluster = 0;
    std::optional<std::uint64_t> cues; // Absolute offset of the Cues element.
    std::optional<double> duration;    // Info Duration, in TimestampScale ticks.
};

MkvLayout read_layout(WindowReader& reader, std::uint64_t file_size) {
    MkvLayout layout;
    const auto ebml = read_element(reader, 0, file_size);
    if (ebml.id != id_ebml) {
        malformed("no EBML header");
    }
    const auto header = load(reader, ebml);
    std::string doc_type = "matroska";
    for (const auto& field : children(header, 0, header.size())) {
        if (field.id == id_doc_type) {
            doc_type = string_value(header, field);
        }
    }
    if (doc_type != "matroska" && doc_type != "webm") {
        unsupported("EBML document type is not Matroska");
    }
    layout.segment = read_element(reader, ebml.end(), file_size);
    if (layout.segment.id != id_segment) {
        malformed("no Segment after the EBML header");
    }
    bool have_tracks = false;
    for (auto position = layout.segment.data; position < layout.segment.end();) {
        const auto element = read_element(reader, position, layout.segment.end());
        if (element.id == id_cluster) {
            layout.first_cluster = position;
            break;
        }
        if (element.unknown_size) {
            malformed("unknown-size element other than Segment or Cluster");
        }
        if (element.id == id_info) {
            const auto data = load(reader, element);
            for (const auto& field : children(data, 0, data.size())) {
                if (field.id == id_timestamp_scale) {
                    layout.timestamp_scale = unsigned_value(data, field);
                } else if (field.id == id_duration) {
                    layout.duration = float_value(data, field);
                }
            }
            if (layout.timestamp_scale == 0 ||
                nanoseconds_per_second % layout.timestamp_scale != 0) {
                unsupported("TimestampScale that does not divide one second");
            }
        } else if (element.id == id_tracks) {
            const auto data = load(reader, element);
            std::vector<TrackInfo> infos;
            for (const auto& entry : children(data, 0, data.size())) {
                if (entry.id == id_track_entry) {
                    infos.push_back(read_track_entry(data, entry));
                }
            }
            layout.selected = select_tracks(infos);
            have_tracks = true;
        } else if (element.id == id_seek_head) {
            const auto data = load(reader, element);
            for (const auto& seek : children(data, 0, data.size())) {
                if (seek.id != id_seek) {
                    continue;
                }
                Bytes target;
                std::optional<std::uint64_t> offset;
                for (const auto& field : children(data, seek.data, seek.end())) {
                    if (field.id == id_seek_id) {
                        target = binary_value(data, field);
                    } else if (field.id == id_seek_position) {
                        offset = unsigned_value(data, field);
                    }
                }
                const Bytes cues_id{0x1C, 0x53, 0xBB, 0x6B};
                if (target == cues_id && offset && *offset < layout.segment.size) {
                    layout.cues = layout.segment.data + *offset;
                }
            }
        } else if (element.id == id_cues) {
            layout.cues = position;
        }
        position = element.end();
    }
    if (!have_tracks) {
        malformed("no Tracks element");
    }
    return layout;
}

/// The video track's cue points, by time, one per timestamp.
std::vector<CuePoint> read_cue_points(WindowReader& reader, const MkvLayout& layout,
                                      std::uint64_t video_track_number) {
    std::vector<CuePoint> points;
    const auto cues = read_element(reader, *layout.cues, layout.segment.end());
    if (cues.id != id_cues || cues.size > max_cues_bytes) {
        return points;
    }
    const auto data = load(reader, cues);
    for (const auto& point : children(data, 0, data.size())) {
        if (point.id != id_cue_point) {
            continue;
        }
        std::optional<std::uint64_t> time;
        std::optional<std::uint64_t> cluster;
        for (const auto& field : children(data, point.data, point.end())) {
            if (field.id == id_cue_time) {
                time = unsigned_value(data, field);
            } else if (field.id == id_cue_track_positions) {
                std::uint64_t track = 0;
                std::optional<std::uint64_t> position;
                for (const auto& entry : children(data, field.data, field.end())) {
                    if (entry.id == id_cue_track) {
                        track = unsigned_value(data, entry);
                    } else if (entry.id == id_cue_cluster_position) {
                        position = unsigned_value(data, entry);
                    }
                }
                if (track == video_track_number && position) {
                    cluster = position;
                }
            }
        }
        if (time && cluster && *cluster < layout.segment.size &&
            *time <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            points.push_back({static_cast<std::int64_t>(*time), layout.segment.data + *cluster});
        }
    }
    std::sort(points.begin(), points.end(),
              [](const CuePoint& a, const CuePoint& b) { return a.ticks < b.ticks; });
    points.erase(
        std::unique(points.begin(), points.end(),
                    [](const CuePoint& a, const CuePoint& b) { return a.ticks == b.ticks; }),
        points.end());
    return points;
}

/// Frames of every selected track read from the clusters starting at `from`,
/// up to and including the first cluster whose timestamp passes `stop_ticks`.
std::vector<SelectedTrack> scan_clusters(WindowReader& reader, const MkvLayout& layout,
                                         std::uint64_t from, std::int64_t stop_ticks) {
    std::vector<SelectedTrack> tracks;
    for (const auto& info : layout.selected) {
        tracks.push_back({info, {}, std::nullopt, {}});
    }
    std::vector<SelectedTrack*> pointers;
    for (auto& track : tracks) {
        pointers.push_back(&track);
    }
    for (auto position = from; position < layout.segment.end();) {
        const auto element = read_element(reader, position, layout.segment.end());
        if (element.id != id_cluster) {
            if (element.unknown_size) {
                malformed("unknown-size element other than Segment or Cluster");
            }
            position = element.end();
            continue;
        }
        std::int64_t cluster_ticks = 0;
        position = read_cluster(reader, element, pointers, &cluster_ticks);
        if (cluster_ticks > stop_ticks) {
            break;
        }
    }
    return tracks;
}

/// The frames of `track` whose block timestamps lie in [start, end).
SelectedTrack frames_in(const SelectedTrack& track, std::int64_t start, std::int64_t end) {
    SelectedTrack subset{track.info, {}, std::nullopt, {}};
    for (std::size_t index = 0; index < track.frames.size(); ++index) {
        const auto& frame = track.frames[index];
        if (frame.timestamp >= start && frame.timestamp < end) {
            subset.frames.push_back(frame);
            if (index < track.durations.size()) {
                subset.durations.push_back(track.durations[index]);
            }
        }
    }
    return subset;
}
} // namespace

struct MkvIndex::Impl {
    MkvLayout layout;
    std::vector<CuePoint> points;    // All video cue points.
    std::vector<std::size_t> starts; // Indices into points: one per segment.
    std::uint64_t file_size = 0;
    Mp4Movie metadata;
    std::int64_t reorder_delay = 0; // Video ticks, from the first segment.
    std::uint64_t audio_origin = 0; // Audio samples: the first segment's first frame.
    std::uint32_t audio_frame_samples = 0;
    std::int64_t end_ticks = 0;         // End of the last video frame.
    std::uint64_t frame_rate_milli = 0; // From the first segment.

    std::int64_t start_ticks(std::size_t segment) const {
        return points[starts[segment]].ticks;
    }
    /// The segment's [start, end) in ticks; the last ends at the video end.
    std::int64_t end_of(std::size_t segment) const {
        return segment + 1 < starts.size() ? start_ticks(segment + 1) : end_ticks;
    }
    std::uint64_t ticks_to_us(std::int64_t ticks) const {
        return static_cast<std::uint64_t>(ticks) * layout.timestamp_scale / 1000;
    }

    /// Every selected track's frames around `segment`: from the cluster of
    /// the last cue point at least the margin before its start, to the first
    /// cluster past its end plus the margin.
    std::vector<SelectedTrack> scan(std::size_t segment, const RandomReader& read) const {
        WindowReader reader(read, file_size);
        const auto margin = static_cast<std::int64_t>(scan_margin_ns / layout.timestamp_scale);
        const auto start = start_ticks(segment);
        std::uint64_t from = layout.first_cluster;
        for (const auto& point : points) {
            if (point.ticks <= start - margin) {
                from = point.cluster;
            }
        }
        if (segment == 0) {
            from = layout.first_cluster;
        }
        const auto stop = segment + 1 < starts.size() ? start_ticks(segment + 1) + margin
                                                      : std::numeric_limits<std::int64_t>::max();
        return scan_clusters(reader, layout, from, stop);
    }

    /// Video frames of `segment` in decode (file) order: from its keyframe at
    /// the cue time to the next segment's keyframe.
    SelectedTrack video_frames(const SelectedTrack& video, std::size_t segment) const {
        const auto start = start_ticks(segment);
        const bool last = segment + 1 == starts.size();
        const auto next = last ? 0 : start_ticks(segment + 1);
        SelectedTrack subset{video.info, {}, video.last_block_duration, {}};
        bool inside = false;
        for (const auto& frame : video.frames) {
            if (!inside && frame.keyframe && frame.timestamp >= start) {
                inside = true;
            } else if (inside && !last && frame.keyframe && frame.timestamp >= next) {
                break;
            }
            if (inside) {
                subset.frames.push_back(frame);
            }
        }
        if (subset.frames.empty()) {
            malformed("a cue point names no video keyframe");
        }
        return subset;
    }

    /// Video samples on the segment's decode span [start, next start): the
    /// sorted presentation times, moved so the first is the cue time (open
    /// GOPs decode leading frames earlier), with the presentation delay of
    /// the first segment so offsets stay as in the full scan; a later segment
    /// that reorders further gets negative offsets (trun version 1).
    std::vector<Mp4Sample> video_samples(const SelectedTrack& frames, std::size_t segment) const {
        std::vector<std::int64_t> decode;
        for (const auto& frame : frames.frames) {
            decode.push_back(frame.timestamp);
        }
        std::sort(decode.begin(), decode.end());
        const auto shift = start_ticks(segment) - decode.front();
        std::vector<Mp4Sample> samples(frames.frames.size());
        for (std::size_t index = 0; index < samples.size(); ++index) {
            const auto& frame = frames.frames[index];
            auto& sample = samples[index];
            sample.offset = frame.offset;
            sample.size = frame.size;
            sample.sync = frame.keyframe;
            const auto dts = decode[index] + shift;
            sample.decode_time = static_cast<std::uint64_t>(dts);
            const auto offset = frame.timestamp + reorder_delay - dts;
            if (offset > std::numeric_limits<std::int32_t>::max() ||
                offset < std::numeric_limits<std::int32_t>::min()) {
                unsupported("frame reordering beyond 32-bit composition offsets");
            }
            sample.composition_offset = static_cast<std::int32_t>(offset);
        }
        for (std::size_t index = 0; index + 1 < samples.size(); ++index) {
            samples[index].duration = static_cast<std::uint32_t>(samples[index + 1].decode_time -
                                                                 samples[index].decode_time);
        }
        auto& last = samples.back();
        if (segment + 1 < starts.size()) {
            const auto end = static_cast<std::uint64_t>(start_ticks(segment + 1));
            last.duration =
                static_cast<std::uint32_t>(end > last.decode_time ? end - last.decode_time : 1);
        } else {
            const auto default_ticks = frames.info.default_duration_ns / layout.timestamp_scale;
            last.duration = static_cast<std::uint32_t>(
                frames.last_block_duration ? *frames.last_block_duration
                : default_ticks            ? default_ticks
                : samples.size() > 1       ? samples[samples.size() - 2].duration
                                           : 1);
        }
        return samples;
    }

    /// Decode time of a segment's first audio frame: its block time, snapped
    /// to the frame grid from the first segment's first frame when within the
    /// TimestampScale rounding. A segment and its predecessor both compute it,
    /// so they agree.
    std::uint64_t first_audio_time(std::int64_t ticks) const {
        const auto rate = metadata.tracks[1].timescale;
        const auto rounding = scale_time(layout.timestamp_scale / 2, rate) + 1;
        const auto block = scale_time(to_nanoseconds(ticks, layout.timestamp_scale), rate);
        const auto steps =
            block >= audio_origin
                ? (block - audio_origin + audio_frame_samples / 2) / audio_frame_samples
                : 0;
        const auto snapped = audio_origin + steps * audio_frame_samples;
        const auto distance = block > snapped ? block - snapped : snapped - block;
        return distance <= rounding ? snapped : block;
    }

    /// Audio samples of the frames in the segment's window. The timeline
    /// follows block times beyond the rounding, as read_mkv() does; the last
    /// sample lasts until `next_ticks`, the next segment's first audio frame,
    /// so consecutive segments' decode times join exactly (Apple HLS 7.3).
    std::vector<Mp4Sample> audio_samples(const SelectedTrack& frames,
                                         std::optional<std::int64_t> next_ticks) const {
        const auto rate = metadata.tracks[1].timescale;
        const auto rounding = scale_time(layout.timestamp_scale / 2, rate) + 1;
        std::vector<Mp4Sample> samples(frames.frames.size());
        std::uint64_t expected = 0;
        for (std::size_t index = 0; index < frames.frames.size(); ++index) {
            const auto& frame = frames.frames[index];
            if (frame.first_in_block) {
                if (index == 0) {
                    expected = first_audio_time(frame.timestamp);
                } else {
                    const auto block =
                        scale_time(to_nanoseconds(frame.timestamp, layout.timestamp_scale), rate);
                    const auto distance = block > expected ? block - expected : expected - block;
                    if (distance > rounding) {
                        expected = block;
                    }
                }
            }
            auto& sample = samples[index];
            sample.offset = frame.offset;
            sample.size = frame.size;
            sample.sync = true;
            sample.decode_time = expected;
            expected += audio_frame_samples;
        }
        for (std::size_t index = 0; index + 1 < samples.size(); ++index) {
            samples[index].duration = static_cast<std::uint32_t>(samples[index + 1].decode_time -
                                                                 samples[index].decode_time);
        }
        if (!samples.empty()) {
            auto& last = samples.back();
            const auto next = next_ticks ? first_audio_time(*next_ticks) : 0;
            last.duration = static_cast<std::uint32_t>(
                next > last.decode_time ? next - last.decode_time : audio_frame_samples);
        }
        return samples;
    }
};

std::unique_ptr<MkvIndex> MkvIndex::open(const RandomReader& read, std::uint64_t file_size) {
    auto impl = std::make_unique<Impl>();
    impl->file_size = file_size;
    WindowReader reader(read, file_size);
    impl->layout = read_layout(reader, file_size);
    auto& layout = impl->layout;
    if (!layout.cues || !layout.first_cluster) {
        return nullptr;
    }
    impl->points = read_cue_points(reader, layout, layout.selected[0].number);
    if (impl->points.empty()) {
        return nullptr;
    }
    // Segments on a 6 s grid of cue times, as plan_segments() cuts sync samples.
    const auto target = static_cast<std::int64_t>(segment_target_ns / layout.timestamp_scale);
    const auto first = impl->points.front().ticks;
    impl->starts.push_back(0);
    for (std::size_t index = 1; index < impl->points.size(); ++index) {
        if (impl->points[index].ticks - first >=
            static_cast<std::int64_t>(impl->starts.size()) * target) {
            impl->starts.push_back(index);
        }
    }
    const auto longest = static_cast<std::int64_t>(max_indexed_segment_ns / layout.timestamp_scale);
    for (std::size_t segment = 0; segment + 1 < impl->starts.size(); ++segment) {
        if (impl->start_ticks(segment + 1) - impl->start_ticks(segment) > longest) {
            return nullptr; // Sparse cues: the full scan plans better segments.
        }
    }
    // A selective index (RFC 9559 22) can also leave a long tail after its
    // last cue; Info Duration shows it before the tail is scanned.
    const auto last_start = impl->start_ticks(impl->starts.size() - 1);
    if (layout.duration &&
        *layout.duration - static_cast<double>(last_start) > static_cast<double>(longest)) {
        return nullptr;
    }

    // The first segment fixes the formats, the reorder delay and the audio
    // grid; the last one fixes the end time.
    impl->end_ticks = std::numeric_limits<std::int64_t>::max();
    const auto first_scan = impl->scan(0, read);
    // A selective index may begin after the first video frames; segment 0
    // would then drop them, so the full scan reads such a file.
    if (first_scan[0].frames.empty() ||
        first_scan[0].frames.front().timestamp < impl->start_ticks(0)) {
        return nullptr;
    }
    const auto first_video = impl->video_frames(first_scan[0], 0);
    auto video = video_track(first_video, layout.timestamp_scale);
    impl->reorder_delay = video.edits.empty() ? 0 : video.edits.front().media_time;
    const auto span = video.samples.back().decode_time + video.samples.back().duration -
                      video.samples.front().decode_time;
    impl->frame_rate_milli =
        span ? (video.samples.size() * std::uint64_t{video.timescale} * 1000 + span / 2) / span : 0;
    video.samples.clear();
    impl->metadata.timescale = movie_timescale;
    impl->metadata.tracks.push_back(std::move(video));
    std::size_t next = 1;
    if (layout.selected.size() > 1 && layout.selected[1].type == track_type_audio) {
        const auto first_audio = frames_in(first_scan[1], impl->start_ticks(0), impl->end_of(0));
        if (first_audio.frames.empty()) {
            return nullptr; // No audio in the first window: leave it to the full scan.
        }
        auto audio = audio_track(first_audio, layout.timestamp_scale, reader);
        impl->audio_frame_samples = audio.samples.back().duration;
        impl->audio_origin = audio.samples.front().decode_time;
        audio.samples.clear();
        impl->metadata.tracks.push_back(std::move(audio));
        next = 2;
    }
    for (std::size_t index = next; index < layout.selected.size(); ++index) {
        const auto& info = layout.selected[index];
        TextTrack text;
        text.name = info.name;
        text.language = language_tag(info.language, info.language_bcp47);
        text.default_track = info.default_track;
        text.forced = info.forced;
        text.hearing_impaired = info.hearing_impaired;
        impl->metadata.text_tracks.push_back(std::move(text));
    }
    const auto last = impl->starts.size() - 1;
    const auto last_samples =
        impl->video_samples(impl->video_frames(impl->scan(last, read)[0], last), last);
    impl->end_ticks =
        static_cast<std::int64_t>(last_samples.back().decode_time + last_samples.back().duration);
    if (impl->end_ticks - last_start > longest) {
        return nullptr; // The last segment would exceed 20 s.
    }
    return std::unique_ptr<MkvIndex>(new MkvIndex(std::move(impl)));
}

MkvIndex::MkvIndex(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
MkvIndex::~MkvIndex() = default;

const Mp4Movie& MkvIndex::metadata() const {
    return impl_->metadata;
}
std::size_t MkvIndex::segment_count() const {
    return impl_->starts.size();
}
std::uint64_t MkvIndex::segment_start_us(std::size_t segment) const {
    return impl_->ticks_to_us(impl_->start_ticks(segment));
}
std::uint64_t MkvIndex::segment_duration_us(std::size_t segment) const {
    return impl_->ticks_to_us(impl_->end_of(segment)) -
           impl_->ticks_to_us(impl_->start_ticks(segment));
}
std::uint64_t MkvIndex::frame_rate_milli() const {
    return impl_->frame_rate_milli;
}
std::uint64_t MkvIndex::estimated_segment_bytes(std::size_t segment) const {
    const auto begin = impl_->points[impl_->starts[segment]].cluster;
    const auto end = segment + 1 < impl_->starts.size()
                         ? impl_->points[impl_->starts[segment + 1]].cluster
                         : impl_->layout.segment.end();
    return end > begin ? end - begin : 0;
}

MkvIndex::Segment MkvIndex::read_segment(std::size_t segment, const RandomReader& read) const {
    const auto& impl = *impl_;
    if (segment >= impl.starts.size()) {
        throw std::out_of_range("segment index");
    }
    const auto tracks = impl.scan(segment, read);
    WindowReader reader(read, impl.file_size);
    const auto start = impl.start_ticks(segment);
    const auto end = segment + 1 < impl.starts.size() ? impl.start_ticks(segment + 1)
                                                      : std::numeric_limits<std::int64_t>::max();
    Segment result;
    result.samples.push_back(impl.video_samples(impl.video_frames(tracks[0], segment), segment));
    std::size_t next = 1;
    if (impl.metadata.tracks.size() > 1) {
        // The scan reaches past the window, so the next segment's first audio
        // frame is in it unless this is the last segment.
        std::optional<std::int64_t> next_audio;
        for (const auto& frame : tracks[1].frames) {
            if (frame.timestamp >= end && (!next_audio || frame.timestamp < *next_audio)) {
                next_audio = frame.timestamp;
            }
        }
        result.samples.push_back(impl.audio_samples(frames_in(tracks[1], start, end), next_audio));
        next = 2;
    }
    for (std::size_t index = next; index < tracks.size(); ++index) {
        // Keep the first frame past the window, when the scan reached it, so a
        // cue without a duration can end at its successor as in the full scan.
        auto window = frames_in(tracks[index], start, end);
        const SelectedTrack* source = &tracks[index];
        for (std::size_t frame = 0; frame < source->frames.size(); ++frame) {
            if (source->frames[frame].timestamp >= end) {
                window.frames.push_back(source->frames[frame]);
                window.durations.push_back(
                    frame < source->durations.size() ? source->durations[frame] : 0);
                break;
            }
        }
        auto text = text_track(window, impl.layout.timestamp_scale, reader);
        std::vector<TextCue> cues = text ? std::move(text->cues) : std::vector<TextCue>{};
        const auto end_us = end == std::numeric_limits<std::int64_t>::max()
                                ? std::numeric_limits<std::uint64_t>::max()
                                : impl.ticks_to_us(end);
        cues.erase(std::remove_if(cues.begin(), cues.end(),
                                  [end_us](const TextCue& cue) { return cue.start_us >= end_us; }),
                   cues.end());
        result.cues.push_back(std::move(cues));
    }
    return result;
}

bool starts_like_matroska(const std::uint8_t* data, std::size_t size) {
    return size >= 4 && data[0] == 0x1A && data[1] == 0x45 && data[2] == 0xDF && data[3] == 0xA3;
}

Mp4Movie read_mkv(const RandomReader& read, std::uint64_t file_size) {
    WindowReader reader(read, file_size);
    const auto ebml = read_element(reader, 0, file_size);
    if (ebml.id != id_ebml) {
        malformed("no EBML header");
    }
    const auto header = load(reader, ebml);
    std::string doc_type = "matroska";
    for (const auto& field : children(header, 0, header.size())) {
        if (field.id == id_doc_type) {
            doc_type = string_value(header, field);
        }
    }
    if (doc_type != "matroska" && doc_type != "webm") {
        unsupported("EBML document type is not Matroska");
    }
    const auto segment = read_element(reader, ebml.end(), file_size);
    if (segment.id != id_segment) {
        malformed("no Segment after the EBML header");
    }

    std::uint64_t timestamp_scale = default_timestamp_scale;
    std::vector<TrackInfo> infos;
    bool have_tracks = false;
    std::vector<SelectedTrack> selected;
    std::vector<SelectedTrack*> pointers;
    auto position = segment.data;
    while (position < segment.end()) {
        const auto element = read_element(reader, position, segment.end());
        if (element.id == id_info) {
            const auto data = load(reader, element);
            for (const auto& field : children(data, 0, data.size())) {
                if (field.id == id_timestamp_scale) {
                    timestamp_scale = unsigned_value(data, field);
                }
            }
            if (timestamp_scale == 0 || nanoseconds_per_second % timestamp_scale != 0) {
                unsupported("TimestampScale that does not divide one second");
            }
        } else if (element.id == id_tracks) {
            const auto data = load(reader, element);
            for (const auto& entry : children(data, 0, data.size())) {
                if (entry.id == id_track_entry) {
                    infos.push_back(read_track_entry(data, entry));
                }
            }
            have_tracks = true;
            // Choose the tracks before any cluster, so blocks of others are skipped.
            for (auto& info : select_tracks(infos)) {
                selected.push_back({std::move(info), {}, std::nullopt, {}});
            }
            for (auto& track : selected) {
                pointers.push_back(&track);
            }
        } else if (element.id == id_cluster) {
            if (!have_tracks) {
                malformed("cluster before the Tracks element");
            }
            position = read_cluster(reader, element, pointers);
            continue;
        }
        if (element.unknown_size) {
            malformed("unknown-size element other than Segment or Cluster");
        }
        position = element.end();
    }
    if (!have_tracks) {
        malformed("no Tracks element");
    }
    Mp4Movie movie;
    movie.timescale = movie_timescale;
    movie.tracks.push_back(video_track(selected[0], timestamp_scale));
    for (std::size_t index = 1; index < selected.size(); ++index) {
        if (selected[index].info.type == track_type_audio) {
            movie.tracks.push_back(audio_track(selected[index], timestamp_scale, reader));
        } else if (auto text = text_track(selected[index], timestamp_scale, reader)) {
            movie.text_tracks.push_back(std::move(*text));
        }
    }
    return movie;
}
} // namespace send_airplay2::detail
