// SPDX-License-Identifier: Apache-2.0
#include "hls_remux.h"
#include "hls_variant.h"
#include "mkv_demux.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

namespace send_airplay2::detail {
namespace {
constexpr std::uint64_t microseconds_per_second = 1'000'000;
constexpr std::chrono::seconds demux_read_deadline{60};
constexpr std::uint64_t max_segment_bytes =
    static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max()); // trun data offsets.
constexpr const char* playlist_name = "index.m3u8";
constexpr const char* multivariant_name = "main.m3u8";
constexpr const char* text_type = "text/vtt";
constexpr std::size_t tkhd_size_fields = 8; // width and height (16.16) end the tkhd.
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

std::string text_playlist_name(std::size_t track) {
    return "t" + std::to_string(track) + ".m3u8";
}

std::string text_segment_name(std::size_t track, std::size_t index) {
    return "t" + std::to_string(track) + "s" + std::to_string(index) + ".vtt";
}

/// The EXTINF lines of every segment, each followed by its name.
template <class Name>
std::string segment_lines(const std::vector<std::uint64_t>& durations_us, Name name) {
    std::string lines;
    for (std::size_t index = 0; index < durations_us.size(); ++index) {
        lines += "#EXTINF:" + seconds_text(durations_us[index]) + ",\n" + name(index) + "\n";
    }
    return lines;
}

std::uint32_t target_from(const std::vector<std::uint64_t>& durations_us) {
    std::uint32_t target = 1;
    for (const auto duration : durations_us) {
        target = std::max(target, rounded_seconds(duration));
    }
    return target;
}

std::string media_playlist_text(const std::vector<std::uint64_t>& durations_us) {
    return "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:" +
           std::to_string(target_from(durations_us)) +
           "\n#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PLAYLIST-TYPE:VOD\n#EXT-X-INDEPENDENT-SEGMENTS\n"
           "#EXT-X-MAP:URI=\"" +
           std::string(init_name) + "\"\n" + segment_lines(durations_us, segment_name) +
           "#EXT-X-ENDLIST\n";
}

std::string subtitle_playlist_text(const std::vector<std::uint64_t>& durations_us,
                                   std::size_t track) {
    return "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:" +
           std::to_string(target_from(durations_us)) +
           "\n#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PLAYLIST-TYPE:VOD\n" +
           segment_lines(durations_us,
                         [track](std::size_t index) { return text_segment_name(track, index); }) +
           "#EXT-X-ENDLIST\n";
}

/// Presentation windows' starts: `origin_us` plus the cumulative durations.
std::vector<std::uint64_t> window_starts(std::uint64_t origin_us,
                                         const std::vector<std::uint64_t>& durations_us) {
    std::vector<std::uint64_t> starts;
    auto elapsed = origin_us;
    for (const auto duration : durations_us) {
        starts.push_back(elapsed);
        elapsed += duration;
    }
    return starts;
}

/// The video track's display size from its tkhd (16.16 width and height).
std::pair<std::uint32_t, std::uint32_t> display_size(const Mp4Track& video) {
    const auto& tkhd = video.track_header;
    if (tkhd.size() < tkhd_size_fields) {
        return {0, 0};
    }
    const auto field = [&tkhd](std::size_t at) {
        return (static_cast<std::uint32_t>(tkhd[at]) << 8) | tkhd[at + 1]; // Integer part.
    };
    return {field(tkhd.size() - 8), field(tkhd.size() - 4)};
}

/// Peak and average bit rate (bits per second) of segments of these sizes on
/// the video segment durations.
std::pair<std::uint64_t, std::uint64_t> bit_rates(const std::vector<std::uint64_t>& durations_us,
                                                  const std::vector<std::uint64_t>& sizes) {
    std::uint64_t peak = 0;
    std::uint64_t total_bytes = 0;
    std::uint64_t total_us = 0;
    for (std::size_t index = 0; index < durations_us.size(); ++index) {
        const auto duration = std::max<std::uint64_t>(durations_us[index], 1);
        peak = std::max(peak, sizes[index] * 8 * microseconds_per_second / duration);
        total_bytes += sizes[index];
        total_us += duration;
    }
    return {peak, total_us ? total_bytes * 8 * microseconds_per_second / total_us : 0};
}

/// EXT-X-STREAM-INF values: the media segments' peak and average bit rate
/// plus the largest subtitle rendition's (one plays at a time; RFC 8216bis:
/// the largest sum over playable combinations), display size, frame rate and
/// codecs.
VariantStream variant_stream(const Mp4Movie& movie, const std::vector<std::uint64_t>& durations_us,
                             const std::vector<std::uint64_t>& sizes,
                             const std::vector<std::vector<std::uint64_t>>& subtitle_sizes,
                             std::uint64_t frame_rate_milli) {
    VariantStream variant;
    variant.uri = playlist_name;
    std::tie(variant.peak_bits_per_second, variant.average_bits_per_second) =
        bit_rates(durations_us, sizes);
    std::uint64_t subtitle_peak = 0;
    std::uint64_t subtitle_average = 0;
    for (const auto& track : subtitle_sizes) {
        const auto [peak, average] = bit_rates(durations_us, track);
        subtitle_peak = std::max(subtitle_peak, peak);
        subtitle_average = std::max(subtitle_average, average);
    }
    variant.peak_bits_per_second += subtitle_peak;
    variant.average_bits_per_second += subtitle_average;
    std::tie(variant.width, variant.height) = display_size(movie.tracks.at(0));
    variant.frame_rate_milli = frame_rate_milli;
    for (const auto& track : movie.tracks) {
        variant.codecs += (variant.codecs.empty() ? "" : ",") + codec_string(track);
    }
    return variant;
}

/// Frames per 1000 s of a video track, rounded: samples x timescale x 1000 / ticks.
std::uint64_t video_frame_rate_milli(const Mp4Track& video) {
    const auto ticks = track_end_time(video) - video.samples.front().decode_time;
    return ticks ? (static_cast<std::uint64_t>(video.samples.size()) * video.timescale * 1000 +
                    ticks / 2) /
                       ticks
                 : 0;
}

/// One rendition per text track; names are made unique within the group,
/// and only the first default-flagged, non-forced track is DEFAULT=YES.
std::vector<SubtitleRendition> subtitle_renditions(const Mp4Movie& movie) {
    std::vector<SubtitleRendition> renditions;
    bool default_taken = false;
    for (std::size_t index = 0; index < movie.text_tracks.size(); ++index) {
        const auto& track = movie.text_tracks[index];
        SubtitleRendition rendition;
        rendition.uri = text_playlist_name(index);
        rendition.language = track.language;
        rendition.forced = track.forced;
        rendition.hearing_impaired = track.hearing_impaired;
        rendition.default_track = track.default_track && !track.forced && !default_taken;
        default_taken = default_taken || rendition.default_track;
        const auto base = !track.name.empty() ? track.name : track.language;
        rendition.name = base;
        for (int copy = 2;
             std::any_of(renditions.begin(), renditions.end(),
                         [&](const auto& other) { return other.name == rendition.name; });
             ++copy) {
            rendition.name = base + " (" + std::to_string(copy) + ")";
        }
        renditions.push_back(std::move(rendition));
    }
    return renditions;
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
std::size_t read_payload(const MediaSource& input, const Mp4Movie& movie,
                         const SegmentLayout& segment, std::uint64_t offset, std::uint8_t* output,
                         std::size_t capacity, const MediaReadContext& context) {
    std::size_t written = 0;
    std::uint64_t position = 0; // Payload offset of the current sample.
    for (std::size_t track = 0; track < segment.ranges.size() && written < capacity; ++track) {
        const auto& samples = movie.tracks[track].samples;
        for (auto index = segment.ranges[track].first;
             index < segment.ranges[track].end && written < capacity; ++index) {
            const auto& sample = samples[index];
            const auto sample_end = position + sample.size;
            if (offset + written < sample_end) {
                const auto within = offset + written - position;
                const auto wanted = static_cast<std::size_t>(
                    std::min<std::uint64_t>(sample.size - within, capacity - written));
                const auto count =
                    input.read_at(sample.offset + within, output + written, wanted, context);
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
                return read_payload(presentation->input, presentation->movie, segment,
                                    offset - header_size, output, capacity, context);
            }};
}

/// Reads through MediaSource::read_at until `length` bytes arrive, under a
/// fixed deadline, for the demuxer's moov and box headers.
RandomReader source_reader(const MediaSource& input, const std::atomic_bool* cancelled) {
    return [&input, cancelled](std::uint64_t offset, std::uint8_t* output, std::size_t length) {
        const std::atomic_bool not_stopped{false};
        const std::atomic_bool not_cancelled{false};
        const MediaReadContext context{&not_stopped, cancelled ? cancelled : &not_cancelled,
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
/// Reads through MediaSource::read_at until `length` bytes arrive, under one
/// request's context: for indexed segment scans on that request's thread.
RandomReader request_reader(const MediaSource& input, const MediaReadContext& context) {
    return [&input, &context](std::uint64_t offset, std::uint8_t* output, std::size_t length) {
        constexpr std::size_t max_read = 64 * 1024; // MediaSource's read capacity bound.
        while (length) {
            if (context.should_stop()) {
                throw std::runtime_error("request stopped while reading an indexed segment");
            }
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

/// One indexed segment, built on its first request and then kept: its
/// samples as a one-segment movie, and the text cues that start in it.
struct IndexedSegment {
    Mp4Movie movie;
    SegmentLayout layout;
    std::uint64_t header_size = 0;
    std::uint64_t size = 0;
    std::vector<std::vector<TextCue>> cues;
};

/// What an indexed MKV presentation's resources share.
struct IndexedPresentation {
    MediaSource input;
    std::unique_ptr<MkvIndex> index;
    std::vector<std::uint64_t> durations_us;
    std::mutex mutex; // Guards the caches; building runs outside it.
    std::vector<std::shared_ptr<const IndexedSegment>> segments;
    std::vector<std::vector<std::shared_ptr<const std::string>>> webvtt; // [track][segment].

    std::shared_ptr<const IndexedSegment> segment(std::size_t index_of,
                                                  const MediaReadContext& context) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (segments[index_of]) {
                return segments[index_of];
            }
        }
        const auto read = index->read_segment(index_of, request_reader(input, context));
        auto built = std::make_shared<IndexedSegment>();
        built->movie = index->metadata();
        built->movie.text_tracks.clear();
        built->layout.sequence = static_cast<std::uint32_t>(index_of + 1);
        for (std::size_t track = 0; track < built->movie.tracks.size(); ++track) {
            built->movie.tracks[track].samples = read.samples[track];
            built->layout.ranges.push_back({0, read.samples[track].size()});
        }
        built->header_size =
            moof_size(built->movie, built->layout) + mdat_header_size(built->movie, built->layout);
        built->size = built->header_size + mdat_payload_size(built->movie, built->layout);
        if (built->size > max_segment_bytes) {
            throw RemuxException(RemuxFailure::too_large, "MKV: a segment exceeds 2 GiB");
        }
        built->cues = read.cues;
        std::lock_guard<std::mutex> lock(mutex);
        if (!segments[index_of]) {
            segments[index_of] = std::move(built);
        }
        return segments[index_of];
    }

    /// WebVTT segment of `track` for window `index_of`: the cues starting in
    /// this segment and the previous one (a cue can run across one cut).
    std::shared_ptr<const std::string> webvtt_segment_text(std::size_t track, std::size_t index_of,
                                                           const MediaReadContext& context) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (webvtt[track][index_of]) {
                return webvtt[track][index_of];
            }
        }
        TextTrack cues;
        if (index_of > 0) {
            const auto& previous = segment(index_of - 1, context)->cues.at(track);
            cues.cues.insert(cues.cues.end(), previous.begin(), previous.end());
        }
        const auto& current = segment(index_of, context)->cues.at(track);
        cues.cues.insert(cues.cues.end(), current.begin(), current.end());
        const auto start = index->segment_start_us(index_of);
        const auto end = index_of + 1 < durations_us.size()
                             ? index->segment_start_us(index_of + 1)
                             : std::numeric_limits<std::uint64_t>::max();
        auto text = std::make_shared<const std::string>(webvtt_segment(cues, start, end));
        std::lock_guard<std::mutex> lock(mutex);
        if (!webvtt[track][index_of]) {
            webvtt[track][index_of] = std::move(text);
        }
        return webvtt[track][index_of];
    }
};

/// Copies a built segment's bytes: its moof and mdat header, then samples.
std::size_t read_indexed_segment(const IndexedPresentation& presentation,
                                 const IndexedSegment& segment, std::uint64_t offset,
                                 std::uint8_t* output, std::size_t capacity,
                                 const MediaReadContext& context) {
    if (offset < segment.header_size) {
        const auto header = write_segment_header(segment.movie, segment.layout);
        const auto count =
            static_cast<std::size_t>(std::min<std::uint64_t>(capacity, header.size() - offset));
        std::memcpy(output, header.data() + offset, count);
        return count;
    }
    return read_payload(presentation.input, segment.movie, segment.layout,
                        offset - segment.header_size, output, capacity, context);
}

/// The resources of an indexed MKV: playlists and init segment built now;
/// every media and WebVTT segment sized and built on its first request.
RemuxedHls remux_indexed(MediaSource input, std::unique_ptr<MkvIndex> index,
                         std::uint64_t frame_rate_milli) {
    auto presentation = std::make_shared<IndexedPresentation>();
    presentation->input = std::move(input);
    presentation->index = std::move(index);
    const auto& index_ref = *presentation->index;
    const auto& metadata = index_ref.metadata();
    const auto count = index_ref.segment_count();
    for (std::size_t segment = 0; segment < count; ++segment) {
        presentation->durations_us.push_back(index_ref.segment_duration_us(segment));
    }
    presentation->segments.resize(count);
    presentation->webvtt.assign(metadata.text_tracks.size(),
                                std::vector<std::shared_ptr<const std::string>>(count));
    const auto& durations = presentation->durations_us;
    const auto text = [](const std::string& value) {
        return memory_source(std::make_shared<const Bytes>(value.begin(), value.end()));
    };

    RemuxedHls result;
    result.playlist_name = playlist_name;
    result.segment_count = count;
    result.text_track_count = metadata.text_tracks.size();
    result.target_duration_seconds = target_from(durations);
    result.resources.push_back(
        {playlist_name, playlist_type, text(media_playlist_text(durations)), {}});
    result.resources.push_back(
        {init_name,
         segment_type,
         memory_source(std::make_shared<const Bytes>(write_init_segment(metadata))),
         {}});
    for (std::size_t segment = 0; segment < count; ++segment) {
        MediaResource resource{segment_name(segment), segment_type, {}, {}};
        resource.source.read_at = [presentation,
                                   segment](std::uint64_t offset, std::uint8_t* output,
                                            std::size_t capacity,
                                            const MediaReadContext& context) -> std::size_t {
            if (context.should_stop()) {
                return 0;
            }
            const auto built = presentation->segment(segment, context);
            return read_indexed_segment(*presentation, *built, offset, output, capacity, context);
        };
        resource.size_on_request = [presentation, segment](const MediaReadContext& context) {
            return presentation->segment(segment, context)->size;
        };
        result.resources.push_back(std::move(resource));
    }
    if (metadata.text_tracks.empty()) {
        return result;
    }
    // Advertised bit rates use the index's cluster spans: segment sizes are
    // not known before their first request.
    std::vector<std::uint64_t> estimates;
    for (std::size_t segment = 0; segment < count; ++segment) {
        estimates.push_back(index_ref.estimated_segment_bytes(segment));
    }
    result.playlist_name = multivariant_name;
    result.resources.push_back(
        {multivariant_name,
         playlist_type,
         text(multivariant_playlist(
             variant_stream(metadata, durations, estimates, {}, frame_rate_milli),
             subtitle_renditions(metadata))),
         {}});
    for (std::size_t track = 0; track < metadata.text_tracks.size(); ++track) {
        result.resources.push_back({text_playlist_name(track),
                                    playlist_type,
                                    text(subtitle_playlist_text(durations, track)),
                                    {}});
        for (std::size_t segment = 0; segment < count; ++segment) {
            MediaResource resource{text_segment_name(track, segment), text_type, {}, {}};
            resource.source.read_at = [presentation, track,
                                       segment](std::uint64_t offset, std::uint8_t* output,
                                                std::size_t capacity,
                                                const MediaReadContext& context) -> std::size_t {
                const auto webvtt = presentation->webvtt_segment_text(track, segment, context);
                if (offset >= webvtt->size()) {
                    return 0;
                }
                const auto copied = static_cast<std::size_t>(
                    std::min<std::uint64_t>(capacity, webvtt->size() - offset));
                std::memcpy(output, webvtt->data() + offset, copied);
                return copied;
            };
            resource.size_on_request = [presentation, track,
                                        segment](const MediaReadContext& context) {
                return static_cast<std::uint64_t>(
                    presentation->webvtt_segment_text(track, segment, context)->size());
            };
            result.resources.push_back(std::move(resource));
        }
    }
    return result;
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

namespace {
std::vector<std::uint64_t> durations_of(const Mp4Movie& movie,
                                        const std::vector<SegmentLayout>& segments) {
    std::vector<std::uint64_t> durations;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        durations.push_back(segment_duration_us(movie, segments, index));
    }
    return durations;
}
} // namespace

std::uint32_t target_duration_seconds(const Mp4Movie& movie,
                                      const std::vector<SegmentLayout>& segments) {
    return target_from(durations_of(movie, segments));
}

std::string media_playlist(const Mp4Movie& movie, const std::vector<SegmentLayout>& segments) {
    return media_playlist_text(durations_of(movie, segments));
}

std::string subtitle_playlist(const Mp4Movie& movie, const std::vector<SegmentLayout>& segments,
                              std::size_t track) {
    return subtitle_playlist_text(durations_of(movie, segments), track);
}

RemuxedHls remux_to_hls(MediaSource input, const std::atomic_bool* cancelled) {
    if (!input.size || !input.read_at) {
        throw std::invalid_argument("remux requires a media source");
    }
    auto presentation = std::make_shared<Presentation>();
    presentation->input = std::move(input);
    const auto file_size = presentation->input.size();
    const auto reader = source_reader(presentation->input, cancelled);
    std::array<std::uint8_t, 4> magic{};
    if (file_size >= magic.size()) {
        reader(0, magic.data(), magic.size());
    }
    const bool matroska = starts_like_matroska(magic.data(), magic.size());
    if (matroska) {
        if (auto index = MkvIndex::open(reader, file_size)) {
            const auto rate = index->frame_rate_milli();
            return remux_indexed(std::move(presentation->input), std::move(index), rate);
        }
    }
    presentation->movie = matroska ? read_mkv(reader, file_size) : read_mp4(reader, file_size);
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
    result.resources.push_back({playlist_name, playlist_type, memory_source(playlist), {}});
    result.resources.push_back(
        {init_name,
         segment_type,
         memory_source(std::make_shared<const Bytes>(write_init_segment(presentation->movie))),
         {}});
    std::shared_ptr<const Presentation> shared = presentation;
    for (std::size_t index = 0; index < sizes.size(); ++index) {
        result.resources.push_back(
            {segment_name(index), segment_type, segment_source(shared, index, sizes[index]), {}});
    }

    const auto& movie = presentation->movie;
    const auto& segments = presentation->segments;
    result.text_track_count = movie.text_tracks.size();
    if (movie.text_tracks.empty()) {
        return result;
    }
    const auto text = [](const std::string& value) {
        return memory_source(std::make_shared<const Bytes>(value.begin(), value.end()));
    };
    // Each WebVTT segment covers the presentation window of its video segment:
    // cumulative EXTINF time from the first video timestamp, which is the
    // timeline origin cues share (not necessarily 0); the last runs to the end.
    const auto& video = movie.tracks.at(0);
    const auto durations = durations_of(movie, segments);
    const auto starts =
        window_starts(ticks_to_us(video.samples.front().decode_time, video.timescale), durations);
    std::vector<std::vector<std::string>> webvtt(movie.text_tracks.size());
    std::vector<std::vector<std::uint64_t>> webvtt_sizes(movie.text_tracks.size());
    for (std::size_t track = 0; track < movie.text_tracks.size(); ++track) {
        for (std::size_t index = 0; index < segments.size(); ++index) {
            const auto end = index + 1 < segments.size()
                                 ? starts[index + 1]
                                 : std::numeric_limits<std::uint64_t>::max();
            webvtt[track].push_back(webvtt_segment(movie.text_tracks[track], starts[index], end));
            webvtt_sizes[track].push_back(webvtt[track].back().size());
        }
    }
    result.playlist_name = multivariant_name;
    result.resources.push_back(
        {multivariant_name,
         playlist_type,
         text(multivariant_playlist(
             variant_stream(movie, durations, sizes, webvtt_sizes, video_frame_rate_milli(video)),
             subtitle_renditions(movie))),
         {}});
    for (std::size_t track = 0; track < movie.text_tracks.size(); ++track) {
        result.resources.push_back({text_playlist_name(track),
                                    playlist_type,
                                    text(subtitle_playlist(movie, segments, track)),
                                    {}});
        for (std::size_t index = 0; index < segments.size(); ++index) {
            result.resources.push_back(
                {text_segment_name(track, index), text_type, text(webvtt[track][index]), {}});
        }
    }
    return result;
}
} // namespace send_airplay2::detail
