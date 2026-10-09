// SPDX-License-Identifier: Apache-2.0
#include "hls_remux.h"
#include "mkv_demux.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace send_airplay2::detail {
namespace {
constexpr std::uint64_t microseconds_per_second = 1'000'000;
constexpr std::chrono::seconds demux_read_deadline{60};
constexpr std::uint64_t max_segment_bytes =
    static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()); // trun data offsets.
constexpr const char* playlist_name = "index.m3u8";
constexpr const char* init_name = "init.mp4";
constexpr const char* playlist_type = "application/vnd.apple.mpegurl";
constexpr const char* segment_type = "video/mp4";

/// Whether a/a_scale < b/b_scale exactly. Comparing whole seconds first keeps
/// the cross-multiplied remainders below 2^64 for any 32-bit timescales.
bool earlier(std::uint64_t a, std::uint32_t a_scale, std::uint64_t b, std::uint32_t b_scale) {
    const auto a_seconds = a / a_scale;
    const auto b_seconds = b / b_scale;
    if (a_seconds != b_seconds) {
        return a_seconds < b_seconds;
    }
    return (a % a_scale) * b_scale < (b % b_scale) * a_scale;
}

std::uint64_t ticks_to_us(std::uint64_t ticks, std::uint32_t timescale) {
    return ticks / timescale * microseconds_per_second +
           ((ticks % timescale) * microseconds_per_second + timescale / 2) / timescale;
}

std::uint64_t track_end_time(const Mp4Track& track) {
    const auto& last = track.samples.back();
    return last.decode_time + last.duration;
}

/// "6.000000": whole seconds and six decimals, independent of the C locale.
std::string seconds_text(std::uint64_t microseconds) {
    auto fraction = std::to_string(microseconds % microseconds_per_second);
    fraction.insert(0, 6 - fraction.size(), '0');
    return std::to_string(microseconds / microseconds_per_second) + "." + fraction;
}

std::uint32_t rounded_seconds(std::uint64_t microseconds) {
    return static_cast<std::uint32_t>((microseconds + microseconds_per_second / 2) /
                                      microseconds_per_second);
}

std::string segment_name(std::size_t index) {
    return "s" + std::to_string(index) + ".m4s";
}

/// What every resource's read_at shares: the source and the sample tables.
struct Presentation {
    MediaSource input;
    Mp4Movie movie;
    std::vector<SegmentLayout> segments;
    std::vector<std::uint64_t> header_sizes; // moof + mdat header, per segment.
};

MediaSource memory_source(std::shared_ptr<const Bytes> bytes) {
    return {[bytes] { return static_cast<std::uint64_t>(bytes->size()); },
            [bytes](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
                    const MediaReadContext&) {
                if (offset >= bytes->size()) {
                    return std::size_t{0};
                }
                const auto count = static_cast<std::size_t>(
                    std::min<std::uint64_t>(capacity, bytes->size() - offset));
                std::memcpy(output, bytes->data() + offset, count);
                return count;
            }};
}

/// Copies up to `capacity` mdat payload bytes starting `offset` bytes into
/// the payload: each range's samples in track order, read from the source.
/// Returns fewer bytes when the source returns a short read or fails.
std::size_t read_payload(const Presentation& presentation, const SegmentLayout& segment,
                         std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
                         const MediaReadContext& context) {
    std::size_t written = 0;
    std::uint64_t position = 0; // Payload offset of the current sample.
    for (std::size_t track = 0; track < segment.ranges.size() && written < capacity; ++track) {
        const auto& samples = presentation.movie.tracks[track].samples;
        for (auto index = segment.ranges[track].first;
             index < segment.ranges[track].end && written < capacity; ++index) {
            const auto& sample = samples[index];
            const auto sample_end = position + sample.size;
            if (offset + written < sample_end) {
                const auto within = offset + written - position;
                const auto wanted = static_cast<std::size_t>(
                    std::min<std::uint64_t>(sample.size - within, capacity - written));
                const auto count = presentation.input.read_at(sample.offset + within,
                                                              output + written, wanted, context);
                if (count == 0 || count > wanted) {
                    return count > wanted ? 0 : written;
                }
                written += count;
                if (count < wanted) {
                    return written; // The server asks again at the next offset.
                }
            }
            position = sample_end;
        }
    }
    return written;
}

MediaSource segment_source(std::shared_ptr<const Presentation> presentation, std::size_t index,
                           std::uint64_t size) {
    return {[size] { return size; },
            [presentation = std::move(presentation),
             index](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
                    const MediaReadContext& context) -> std::size_t {
                const auto& segment = presentation->segments[index];
                const auto header_size = presentation->header_sizes[index];
                if (context.should_stop()) {
                    return 0;
                }
                if (offset < header_size) {
                    // Regenerated per request: it costs about 16 bytes per sample.
                    const auto header = write_segment_header(presentation->movie, segment);
                    const auto count = static_cast<std::size_t>(
                        std::min<std::uint64_t>(capacity, header.size() - offset));
                    std::memcpy(output, header.data() + offset, count);
                    return count;
                }
                return read_payload(*presentation, segment, offset - header_size, output, capacity,
                                    context);
            }};
}

/// Reads through MediaSource::read_at until `length` bytes arrive, under a
/// fixed deadline, for the demuxer's moov and box headers.
RandomReader source_reader(const MediaSource& input) {
    return [&input](std::uint64_t offset, std::uint8_t* output, std::size_t length) {
        const std::atomic_bool not_stopped{false};
        const std::atomic_bool not_cancelled{false};
        const MediaReadContext context{&not_stopped, &not_cancelled,
                                       std::chrono::steady_clock::now() + demux_read_deadline};
        constexpr std::size_t max_read = 64 * 1024; // MediaSource's read capacity bound.
        while (length) {
            const auto wanted = std::min(length, max_read);
            const auto count = input.read_at(offset, output, wanted, context);
            if (count == 0 || count > wanted) {
                throw std::runtime_error("media source read failed while remuxing");
            }
            offset += count;
            output += count;
            length -= count;
        }
    };
}
} // namespace

std::vector<SegmentLayout> plan_segments(const Mp4Movie& movie, std::uint32_t target_seconds) {
    const auto& video = movie.tracks.at(0);
    if (!video.samples.front().sync) {
        throw RemuxException(RemuxFailure::unsupported,
                             "MP4: video does not start with a sync sample");
    }
    const std::uint64_t target_ticks = static_cast<std::uint64_t>(target_seconds) * video.timescale;
    std::vector<std::size_t> starts{0};
    for (std::size_t index = 1; index < video.samples.size(); ++index) {
        const auto& sample = video.samples[index];
        // Cut on a grid of target multiples from the start, not from each
        // segment's start, so one long GOP does not delay every later cut.
        if (sample.sync && sample.decode_time >= starts.size() * target_ticks) {
            starts.push_back(index);
        }
    }
    std::vector<SegmentLayout> segments(starts.size());
    for (std::size_t segment = 0; segment < starts.size(); ++segment) {
        segments[segment].sequence = static_cast<std::uint32_t>(segment + 1);
        segments[segment].ranges.resize(movie.tracks.size());
        segments[segment].ranges[0] = {starts[segment], segment + 1 < starts.size()
                                                            ? starts[segment + 1]
                                                            : video.samples.size()};
    }
    for (std::size_t track = 1; track < movie.tracks.size(); ++track) {
        const auto& samples = movie.tracks[track].samples;
        const auto scale = movie.tracks[track].timescale;
        std::size_t next = 0;
        for (std::size_t segment = 0; segment < starts.size(); ++segment) {
            const auto first = next;
            if (segment + 1 == starts.size()) {
                next = samples.size();
            } else {
                const auto boundary = video.samples[starts[segment + 1]].decode_time;
                while (next < samples.size() &&
                       earlier(samples[next].decode_time, scale, boundary, video.timescale)) {
                    ++next;
                }
            }
            segments[segment].ranges[track] = {first, next};
        }
    }
    return segments;
}

std::uint64_t segment_duration_us(const Mp4Movie& movie, const std::vector<SegmentLayout>& segments,
                                  std::size_t index) {
    const auto& video = movie.tracks.at(0);
    const auto start = video.samples[segments.at(index).ranges[0].first].decode_time;
    const auto end = index + 1 < segments.size()
                         ? video.samples[segments[index + 1].ranges[0].first].decode_time
                         : track_end_time(video);
    // Converted from absolute times so that rounding never accumulates.
    return ticks_to_us(end, video.timescale) - ticks_to_us(start, video.timescale);
}

std::uint32_t target_duration_seconds(const Mp4Movie& movie,
                                      const std::vector<SegmentLayout>& segments) {
    std::uint32_t target = 1;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        target = std::max(target, rounded_seconds(segment_duration_us(movie, segments, index)));
    }
    return target;
}

std::string media_playlist(const Mp4Movie& movie, const std::vector<SegmentLayout>& segments) {
    const auto target = target_duration_seconds(movie, segments);
    std::string playlist = "#EXTM3U\n"
                           "#EXT-X-VERSION:7\n"
                           "#EXT-X-TARGETDURATION:" +
                           std::to_string(target) +
                           "\n"
                           "#EXT-X-MEDIA-SEQUENCE:0\n"
                           "#EXT-X-PLAYLIST-TYPE:VOD\n"
                           "#EXT-X-INDEPENDENT-SEGMENTS\n"
                           "#EXT-X-MAP:URI=\"" +
                           std::string(init_name) + "\"\n";
    for (std::size_t index = 0; index < segments.size(); ++index) {
        playlist += "#EXTINF:" + seconds_text(segment_duration_us(movie, segments, index)) + ",\n" +
                    segment_name(index) + "\n";
    }
    playlist += "#EXT-X-ENDLIST\n";
    return playlist;
}

RemuxedHls remux_to_hls(MediaSource input) {
    if (!input.size || !input.read_at) {
        throw std::invalid_argument("remux requires a media source");
    }
    auto presentation = std::make_shared<Presentation>();
    presentation->input = std::move(input);
    const auto file_size = presentation->input.size();
    const auto reader = source_reader(presentation->input);
    std::array<std::uint8_t, 4> magic{};
    if (file_size >= magic.size()) {
        reader(0, magic.data(), magic.size());
    }
    presentation->movie = starts_like_matroska(magic.data(), magic.size())
                              ? read_mkv(reader, file_size)
                              : read_mp4(reader, file_size);
    presentation->segments = plan_segments(presentation->movie);

    RemuxedHls result;
    result.playlist_name = playlist_name;
    result.segment_count = presentation->segments.size();
    std::vector<std::uint64_t> sizes;
    for (const auto& segment : presentation->segments) {
        const auto header = moof_size(presentation->movie, segment) +
                            mdat_header_size(presentation->movie, segment);
        const auto size = header + mdat_payload_size(presentation->movie, segment);
        if (size > max_segment_bytes) {
            throw RemuxException(RemuxFailure::too_large, "MP4: a segment exceeds 2 GiB");
        }
        presentation->header_sizes.push_back(header);
        sizes.push_back(size);
    }
    const auto playlist = std::make_shared<const Bytes>([&] {
        const auto text = media_playlist(presentation->movie, presentation->segments);
        return Bytes(text.begin(), text.end());
    }());
    result.target_duration_seconds =
        target_duration_seconds(presentation->movie, presentation->segments);
    result.resources.push_back({playlist_name, playlist_type, memory_source(playlist)});
    result.resources.push_back(
        {init_name, segment_type,
         memory_source(std::make_shared<const Bytes>(write_init_segment(presentation->movie)))});
    std::shared_ptr<const Presentation> shared = presentation;
    for (std::size_t index = 0; index < sizes.size(); ++index) {
        result.resources.push_back(
            {segment_name(index), segment_type, segment_source(shared, index, sizes[index])});
    }
    return result;
}
} // namespace send_airplay2::detail
