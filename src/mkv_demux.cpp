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
                           std::vector<SelectedTrack*>& tracks) {
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
} // namespace

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
            selected.push_back({*video, {}, std::nullopt, {}});
            if (audio) {
                selected.push_back({*audio, {}, std::nullopt, {}});
            }
            // Text subtitles are optional extras: tracks that cannot be served
            // (bitmap formats, ContentEncodings) are left out, not refused.
            for (const auto& info : infos) {
                if (info.type == track_type_subtitle && info.enabled && !info.encoded &&
                    text_codec(info.codec_id)) {
                    selected.push_back({info, {}, std::nullopt, {}});
                }
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
