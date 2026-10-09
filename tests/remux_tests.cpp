// SPDX-License-Identifier: Apache-2.0
// Synthetic in-memory MP4 files only: no receiver, network or private media.
#include "fmp4_writer.h"
#include "hls_remux.h"
#include "mp4_demux.h"
#include "mp4_fixture.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace send_airplay2;
using namespace send_airplay2::detail;
using namespace send_airplay2::test::mp4_fixture;

int failures = 0;
const char* group = "setup";
void check(bool passed, const std::string& scenario) {
    if (!passed) {
        std::cerr << "FAIL [" << group << "]: " << scenario << '\n';
        ++failures;
    }
}

RandomReader memory_reader(const Bytes& file) {
    return [&file](std::uint64_t offset, std::uint8_t* output, std::size_t length) {
        if (offset > file.size() || length > file.size() - offset) {
            throw std::out_of_range("read past the synthetic file");
        }
        std::memcpy(output, file.data() + offset, length);
    };
}

/// A MediaSource over `file` that returns at most `read_limit` bytes per call.
MediaSource memory_media(std::shared_ptr<const Bytes> file, std::size_t read_limit = 65536) {
    return {[file] { return static_cast<std::uint64_t>(file->size()); },
            [file, read_limit](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
                               const MediaReadContext& context) -> std::size_t {
                if (context.should_stop() || offset >= file->size()) {
                    return 0;
                }
                const auto count =
                    std::min<std::uint64_t>({capacity, read_limit, file->size() - offset});
                std::memcpy(output, file->data() + offset, static_cast<std::size_t>(count));
                return static_cast<std::size_t>(count);
            }};
}

struct OpenContext {
    std::atomic_bool stopped{false};
    std::atomic_bool cancelled{false};
    MediaReadContext context{&stopped, &cancelled,
                             std::chrono::steady_clock::now() + std::chrono::minutes(1)};
};

/// Reads a whole resource through read_at with small, offset-crossing requests.
Bytes read_resource(const MediaResource& resource, std::size_t chunk = 7) {
    OpenContext open;
    Bytes out(static_cast<std::size_t>(resource.source.size()));
    std::size_t position = 0;
    while (position < out.size()) {
        const auto count = resource.source.read_at(
            position, out.data() + position, std::min(chunk, out.size() - position), open.context);
        if (count == 0) {
            break;
        }
        position += count;
    }
    out.resize(position);
    return out;
}

template <class Action>
void expect_failure(RemuxFailure expected, const std::string& scenario, Action action) {
    try {
        action();
        check(false, scenario + ": accepted");
    } catch (const RemuxException& error) {
        check(error.reason() == expected,
              scenario + ": category " + remux_failure_name(error.reason()));
    }
}

// --- Tests -------------------------------------------------------------------

void demux_tests() {
    group = "demux sample tables";
    const auto file = mp4_file({video_spec(), audio_spec()});
    const auto movie = read_mp4(memory_reader(file), file.size());
    check(movie.timescale == movie_timescale, "movie timescale");
    check(movie.tracks.size() == 2, "video then audio selected");
    if (movie.tracks.size() != 2) {
        return;
    }
    const auto& video = movie.tracks[0];
    check(video.kind == TrackKind::video && video.codec == "avc1" &&
              video.timescale == video_timescale,
          "video kind, codec, timescale");
    check(video.sample_entry == avc1_entry(), "video sample entry copied verbatim");
    check(video.edits.size() == 1 && video.edits[0].segment_duration == 8 * movie_timescale &&
              video.edits[0].media_time == 0,
          "video edit list");
    check(video.samples.size() == video_samples, "video sample count");
    for (std::size_t index = 0; index < video.samples.size() && index < video_samples; ++index) {
        const auto& sample = video.samples[index];
        const auto at = " (sample " + std::to_string(index) + ")";
        check(sample.offset == video_offsets[index], "video offset" + at);
        check(sample.size == 10 + index, "video size" + at);
        check(sample.decode_time == 1000 * index && sample.duration == 1000, "video timing" + at);
        check(sample.composition_offset == video_composition[index], "composition offset" + at);
        check(sample.sync == (index == 0 || index == 3 || index == 6), "sync flag" + at);
    }
    const auto& audio = movie.tracks[1];
    check(audio.kind == TrackKind::audio && audio.codec == "mp4a" &&
              audio.timescale == audio_timescale && audio.edits.empty(),
          "audio kind, codec, timescale, no edits");
    check(audio.samples.size() == audio_samples, "audio sample count");
    for (std::size_t index = 0; index < audio.samples.size() && index < audio_samples; ++index) {
        const auto& sample = audio.samples[index];
        const auto expected_offset = audio_chunk_offsets[index / 4] + 4 * (index % 4);
        check(sample.offset == expected_offset && sample.size == 4 && sample.sync &&
                  sample.decode_time == 50 * index,
              "audio sample " + std::to_string(index));
    }

    const auto video_only = mp4_file({video_spec()});
    check(read_mp4(memory_reader(video_only), video_only.size()).tracks.size() == 1,
          "video without audio is remuxable");
    auto disabled = audio_spec(audio_entry("Opus", {}));
    disabled.tkhd_flags = 0; // A disabled track is ignored, even if unremuxable.
    const auto with_disabled = mp4_file({video_spec(), disabled, audio_spec()});
    check(read_mp4(memory_reader(with_disabled), with_disabled.size()).tracks.size() == 2,
          "disabled tracks are skipped");
}

void refusal_tests() {
    group = "demux refusals";
    const auto refuse = [](RemuxFailure expected, const std::string& scenario, const Bytes& file) {
        expect_failure(expected, scenario,
                       [&] { (void)read_mp4(memory_reader(file), file.size()); });
    };
    refuse(RemuxFailure::malformed, "no moov",
           box("ftyp", join({Bytes{'i', 's', 'o', 'm'}, words({0})})));
    refuse(RemuxFailure::unsupported, "fragmented input", mp4_file({video_spec()}, true));
    auto mpeg4_video = video_spec();
    mpeg4_video.entry = avc1_entry("mp4v");
    refuse(RemuxFailure::unsupported, "MPEG-4 Part 2 video", mp4_file({mpeg4_video}));
    refuse(RemuxFailure::unsupported, "no video track", mp4_file({audio_spec()}));
    refuse(RemuxFailure::unsupported, "Opus audio",
           mp4_file({video_spec(), audio_spec(audio_entry("Opus", {}))}));
    refuse(RemuxFailure::unsupported, "MP3 in mp4a (object type 0x6b)",
           mp4_file({video_spec(), audio_spec(audio_entry("mp4a", esds(0x6b)))}));
    auto two_entries = video_spec();
    two_entries.stsd_entries = 2;
    refuse(RemuxFailure::unsupported, "two sample descriptions", mp4_file({two_entries}));

    auto short_stts = video_spec();
    short_stts.stbl_extra = join({full_box("stts", 0, 0, words({1, video_samples - 1, 1000})),
                                  full_box("stsc", 0, 0, words({1, 1, 8, 1})),
                                  full_box("stsz", 0, 0, words({1, video_samples})),
                                  full_box("stco", 0, 0, words({1, 24}))});
    refuse(RemuxFailure::malformed, "stts shorter than stsz", mp4_file({short_stts}));
    auto outside = video_spec();
    outside.stbl_extra = join(
        {full_box("stts", 0, 0, words({1, 2, 1000})), full_box("stsc", 0, 0, words({1, 1, 2, 1})),
         full_box("stsz", 0, 0, words({1000, 2})), full_box("stco", 0, 0, words({1, 24}))});
    refuse(RemuxFailure::malformed, "samples past the end of the file", mp4_file({outside}));
    auto missing_chunk = video_spec();
    missing_chunk.stbl_extra = join(
        {full_box("stts", 0, 0, words({1, 4, 1000})), full_box("stsc", 0, 0, words({1, 1, 1, 1})),
         full_box("stsz", 0, 0, words({1, 4})), full_box("stco", 0, 0, words({2, 24, 25}))});
    refuse(RemuxFailure::malformed, "fewer chunks than samples", mp4_file({missing_chunk}));

    auto truncated = mp4_file({video_spec()});
    truncated.resize(truncated.size() - 3);
    refuse(RemuxFailure::malformed, "truncated moov", truncated);
    Bytes oversize_box = mp4_file({video_spec()});
    constexpr std::size_t mdat_size_at = 16; // Right after the 16-byte ftyp.
    oversize_box[mdat_size_at + 1] = 0xff;   // The mdat's size now passes the end of the file.
    refuse(RemuxFailure::malformed, "box past the end of the file", oversize_box);
}

void segment_plan_tests() {
    group = "segment plan";
    const auto file = mp4_file({video_spec(), audio_spec()});
    const auto movie = read_mp4(memory_reader(file), file.size());
    // Two-second grid: cuts at the first sync samples at or after 2 s and 4 s
    // (samples 3 and 6, at 3 s and 6 s); audio follows the video boundaries.
    const auto segments = plan_segments(movie, 2);
    check(segments.size() == 3, "three segments");
    if (segments.size() != 3) {
        return;
    }
    const std::size_t video_ranges[3][2] = {{0, 3}, {3, 6}, {6, 8}};
    const std::size_t audio_ranges[3][2] = {{0, 6}, {6, 12}, {12, 16}};
    for (std::size_t index = 0; index < 3; ++index) {
        const auto at = " (segment " + std::to_string(index) + ")";
        check(segments[index].sequence == index + 1, "sequence" + at);
        check(segments[index].ranges[0].first == video_ranges[index][0] &&
                  segments[index].ranges[0].end == video_ranges[index][1],
              "video range" + at);
        check(segments[index].ranges[1].first == audio_ranges[index][0] &&
                  segments[index].ranges[1].end == audio_ranges[index][1],
              "audio range" + at);
    }
    check(segment_duration_us(movie, segments, 0) == 3'000'000 &&
              segment_duration_us(movie, segments, 2) == 2'000'000,
          "durations from video decode times and the track end");

    const std::string expected = "#EXTM3U\n"
                                 "#EXT-X-VERSION:7\n"
                                 "#EXT-X-TARGETDURATION:3\n"
                                 "#EXT-X-MEDIA-SEQUENCE:0\n"
                                 "#EXT-X-PLAYLIST-TYPE:VOD\n"
                                 "#EXT-X-INDEPENDENT-SEGMENTS\n"
                                 "#EXT-X-MAP:URI=\"init.mp4\"\n"
                                 "#EXTINF:3.000000,\n"
                                 "s0.m4s\n"
                                 "#EXTINF:3.000000,\n"
                                 "s1.m4s\n"
                                 "#EXTINF:2.000000,\n"
                                 "s2.m4s\n"
                                 "#EXT-X-ENDLIST\n";
    check(media_playlist(movie, segments) == expected, "VOD playlist text");

    check(plan_segments(movie, 6).size() == 2, "6-second grid: one cut, at sample 6");
    auto late_sync = video_spec();
    late_sync.stbl_extra = join(
        {full_box("stts", 0, 0, words({1, 2, 1000})), full_box("stss", 0, 0, words({1, 2})),
         full_box("stsc", 0, 0, words({1, 1, 2, 1})), full_box("stsz", 0, 0, words({0, 2, 10, 11})),
         full_box("stco", 0, 0, words({1, 24}))});
    const auto late = mp4_file({late_sync});
    const auto late_movie = read_mp4(memory_reader(late), late.size());
    expect_failure(RemuxFailure::unsupported, "first video sample not sync",
                   [&] { (void)plan_segments(late_movie); });
}

void init_segment_tests() {
    group = "init segment";
    const auto file = mp4_file({video_spec(), audio_spec()});
    const auto movie = read_mp4(memory_reader(file), file.size());
    const auto init = write_init_segment(movie);
    const Bytes ftyp = join({words({28}), Bytes{'f', 't', 'y', 'p', 'i', 's', 'o', '5'}, words({0}),
                             Bytes{'i', 's', 'o', '5', 'i', 's', 'o', '6', 'm', 'p', '4', '1'}});
    check(init.size() > ftyp.size() && std::equal(ftyp.begin(), ftyp.end(), init.begin()),
          "ftyp: iso5 with iso5, iso6, mp41");
    const auto contains = [&init](const Bytes& needle) {
        return std::search(init.begin(), init.end(), needle.begin(), needle.end()) != init.end();
    };
    check(contains(avc1_entry()), "video sample entry copied byte for byte");
    check(contains(audio_entry("mp4a", esds(0x40))), "audio sample entry copied byte for byte");
    // The single media edit keeps media time 0 and lasts "to the end" (0).
    check(contains(full_box("elst", 0, 0, words({1, 0, 0, 0x00010000}))),
          "last media edit duration becomes zero");
    check(contains(full_box("trex", 0, 0, words({1, 1, 0, 0, 0}))) &&
              contains(full_box("trex", 0, 0, words({2, 1, 0, 0, 0}))),
          "one trex per track, IDs 1 and 2");
    check(init[28 + 4] == 'm' && init[28 + 5] == 'o' && init[28 + 6] == 'o' && init[28 + 7] == 'v',
          "moov follows ftyp");
}

void segment_header_tests() {
    group = "segment header known answer";
    const auto file = mp4_file({video_spec(), audio_spec()});
    const auto movie = read_mp4(memory_reader(file), file.size());
    const auto segments = plan_segments(movie, 2);
    // Segment 3: video samples 6-7 (16 and 17 bytes, decode 6000), audio
    // samples 12-15 (4 bytes each, decode 600).
    // moof = 8 + mfhd 16 + video traf 96 + audio traf 100 = 220; mdat header 8.
    // Data offsets from the moof start: video 228, audio 228 + 33 = 261.
    const Bytes expected = join({
        words({220}),
        Bytes{'m', 'o', 'o', 'f'},
        words({16}),
        Bytes{'m', 'f', 'h', 'd'},
        words({0, 3}),
        words({96}),
        Bytes{'t', 'r', 'a', 'f'},
        words({16}),
        Bytes{'t', 'f', 'h', 'd'},
        words({0x00020000, 1}),
        words({20}),
        Bytes{'t', 'f', 'd', 't'},
        words({0x01000000, 0, 6000}),
        words({52}),
        Bytes{'t', 'r', 'u', 'n'},
        words({0x00000f01, 2, 228}),
        words({1000, 16, 0x02000000, 0}),    // Sample 6: sync, offset 0.
        words({1000, 17, 0x01010000, 1000}), // Sample 7: not sync, offset 1000.
        words({100}),
        Bytes{'t', 'r', 'a', 'f'},
        words({20}),
        Bytes{'t', 'f', 'h', 'd'},
        words({0x00020020, 2, 0x02000000}),
        words({20}),
        Bytes{'t', 'f', 'd', 't'},
        words({0x01000000, 0, 600}),
        words({52}),
        Bytes{'t', 'r', 'u', 'n'},
        words({0x00000301, 4, 261}),
        words({50, 4, 50, 4, 50, 4, 50, 4}),
        words({8 + 33 + 16}),
        Bytes{'m', 'd', 'a', 't'},
    });
    const auto header = write_segment_header(movie, segments.at(2));
    check(header == expected, "moof and mdat header bytes");
    check(moof_size(movie, segments[2]) == 220 && mdat_header_size(movie, segments[2]) == 8 &&
              mdat_payload_size(movie, segments[2]) == 49,
          "sizes computed without writing");

    auto negative = video_spec();
    negative.stbl_extra = video_tables(true);
    const auto negative_file = mp4_file({negative});
    const auto negative_movie = read_mp4(memory_reader(negative_file), negative_file.size());
    const auto negative_segments = plan_segments(negative_movie, 2);
    const auto negative_header = write_segment_header(negative_movie, negative_segments.at(2));
    // moof(8) + mfhd(16) + traf(8) + tfhd(16) + tfdt(20), then the trun's
    // size, type and version byte.
    constexpr std::size_t trun_version_at = 8 + 16 + 8 + 16 + 20 + 8;
    check(negative_header.at(trun_version_at) == 1, "negative composition offset uses trun v1");
    check(negative_movie.tracks[0].samples[7].composition_offset == -500,
          "ctts version 1 offsets are signed");
    check(write_segment_header(negative_movie, negative_segments.at(0)).at(trun_version_at) == 0,
          "non-negative offsets use trun v0");
}

void presentation_tests() {
    group = "remuxed presentation";
    const auto file = std::make_shared<const Bytes>(mp4_file({video_spec(), audio_spec()}));
    // The source returns at most 5 bytes per read, so every layer handles short reads.
    const auto remuxed = remux_to_hls(memory_media(file, 5));
    check(remuxed.playlist_name == "index.m3u8", "playlist name");
    check(remuxed.segment_count == 2, "6-second target: two segments");
    check(remuxed.target_duration_seconds == 6, "target duration");
    std::vector<std::string> names;
    for (const auto& resource : remuxed.resources) {
        names.push_back(resource.name);
    }
    check(names == std::vector<std::string>{"index.m3u8", "init.mp4", "s0.m4s", "s1.m4s"},
          "resource names");
    if (remuxed.resources.size() != 4) {
        return;
    }
    check(remuxed.resources[0].content_type == "application/vnd.apple.mpegurl" &&
              remuxed.resources[1].content_type == "video/mp4" &&
              remuxed.resources[2].content_type == "video/mp4",
          "content types");
    const auto playlist = read_resource(remuxed.resources[0]);
    check(std::string(playlist.begin(), playlist.end())
                  .find("#EXTINF:6.000000,\ns0.m4s\n"
                        "#EXTINF:2.000000,\ns1.m4s\n") != std::string::npos,
          "playlist segments");

    // Segment 1 (s1.m4s): video samples 6-7, then audio samples 12-15.
    const auto movie = read_mp4(memory_reader(*file), file->size());
    const auto segments = plan_segments(movie);
    Bytes expected = write_segment_header(movie, segments.at(1));
    for (const auto offset : {video_offsets[6], video_offsets[7]}) {
        const auto size = offset == video_offsets[6] ? 16 : 17;
        for (std::uint64_t byte = offset; byte < offset + size; ++byte) {
            expected.push_back(pattern(byte));
        }
    }
    for (std::uint64_t byte = 105; byte < 121; ++byte) { // Audio chunk 4.
        expected.push_back(pattern(byte));
    }
    const auto& second = remuxed.resources[3];
    check(second.source.size() == expected.size(), "segment size declared up front");
    check(read_resource(second, 7) == expected, "segment bytes across 7-byte requests");
    check(read_resource(second, 65536) == expected, "segment bytes in one request");

    OpenContext stopped;
    stopped.stopped = true;
    std::uint8_t byte = 0;
    check(second.source.read_at(0, &byte, 1, stopped.context) == 0,
          "a stopped request reads nothing");
}
} // namespace

int main() {
    try {
        demux_tests();
        refusal_tests();
        segment_plan_tests();
        init_segment_tests();
        segment_header_tests();
        presentation_tests();
    } catch (const std::exception& error) {
        std::cerr << "FAIL [" << group << "]: test infrastructure exception: " << error.what()
                  << '\n';
        return 1;
    }
    return failures ? 1 : 0;
}
