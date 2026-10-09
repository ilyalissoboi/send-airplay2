// SPDX-License-Identifier: Apache-2.0
// Synthetic in-memory Matroska files and codec headers only: no receiver,
// network or private media.
#include "hls_remux.h"
#include "mkv_demux.h"
#include "sample_entries.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace send_airplay2;
using namespace send_airplay2::detail;

int failures = 0;
const char* group = "setup";
void check(bool passed, const std::string& scenario) {
    if (!passed) {
        std::cerr << "FAIL [" << group << "]: " << scenario << '\n';
        ++failures;
    }
}

Bytes join(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const auto& part : parts) {
        out.insert(out.end(), part.begin(), part.end());
    }
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

// --- Codec headers with hand-computed fields ---------------------------------

// AC-3, 48 kHz, 384 kbit/s (frmsizecod 0x1c), bsid 8, bsmod 0, acmod 7 (3/2)
// with LFE: 0b77 | crc 0000 | 00 011100 | 01000 000 | 111 00 00 1.
const Bytes ac3_header{0x0b, 0x77, 0x00, 0x00, 0x1c, 0x40, 0xe1, 0, 0, 0, 0, 0, 0, 0, 0, 0};
// E-AC-3 independent, 1024-byte frame (frmsiz 511), 48 kHz, 6 blocks, acmod 2
// (2/0), no LFE, bsid 16: 0b77 | 00 000 00111111111 | 00 11 010 0 | 10000 ...
const Bytes eac3_header{0x0b, 0x77, 0x01, 0xff, 0x34, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

void sample_entry_tests() {
    group = "sample entries";
    const auto ac3 = read_dolby_frame(ac3_header.data(), ac3_header.size());
    check(!ac3.enhanced && ac3.sample_rate == 48000 && ac3.frame_bytes == 1536 &&
              ac3.samples == 1536 && ac3.channels == 6 && ac3.bit_rate_code == 14 &&
              ac3.bsid == 8 && ac3.acmod == 7 && ac3.lfeon == 1,
          "AC-3 header fields");
    // dac3: 00 01000 000 111 1 01110 00000.
    const Bytes ac3_entry = join({{0, 0, 0, 0x2f, 'a', 'c', '-', '3', 0, 0, 0, 0, 0, 0, 0, 1},
                                  {0, 0, 0, 0, 0, 0, 0, 0, 0, 6, 0, 16, 0, 0, 0, 0},
                                  {0xbb, 0x80, 0, 0},
                                  {0, 0, 0, 0x0b, 'd', 'a', 'c', '3', 0x10, 0x3d, 0xc0}});
    check(ac3_sample_entry(ac3) == ac3_entry, "ac-3 entry with dac3 known answer");

    const auto eac3 = read_dolby_frame(eac3_header.data(), eac3_header.size());
    check(eac3.enhanced && eac3.sample_rate == 48000 && eac3.frame_bytes == 1024 &&
              eac3.samples == 1536 && eac3.channels == 2 && eac3.bsid == 16 &&
              eac3.stream_type == 0,
          "E-AC-3 header fields");
    // dec3: data_rate 256 (13 bits), num_ind_sub 0, fscod 0, bsid 16, asvc 0,
    // bsmod 0, acmod 2, lfeon 0, num_dep_sub 0.
    const Bytes eac3_entry =
        join({{0, 0, 0, 0x31, 'e', 'c', '-', '3', 0, 0, 0, 0, 0, 0, 0, 1},
              {0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 16, 0, 0, 0, 0},
              {0xbb, 0x80, 0, 0},
              {0, 0, 0, 0x0d, 'd', 'e', 'c', '3', 0x08, 0x00, 0x20, 0x04, 0x00}});
    check(eac3_sample_entry(eac3) == eac3_entry, "ec-3 entry with dec3 known answer");
    auto dependent = eac3;
    dependent.stream_type = 1;
    expect_failure(RemuxFailure::unsupported, "dependent substream entry",
                   [&] { (void)eac3_sample_entry(dependent); });
    const Bytes no_sync{0x0b, 0x78, 0, 0, 0, 0, 0, 0};
    expect_failure(RemuxFailure::malformed, "no sync word",
                   [&] { (void)read_dolby_frame(no_sync.data(), no_sync.size()); });

    // AudioSpecificConfig 0x1190: AAC-LC (2), index 3 (48 kHz), 2 channels.
    const Bytes asc{0x11, 0x90};
    const auto aac = read_aac_config(asc);
    check(aac.object_type == 2 && aac.sample_rate == 48000 && aac.channels == 2, "ASC fields");
    check(aac.frame_samples == 1024, "1024-sample frames without frameLengthFlag");
    check(aac_config(2, 48000, 2) == asc, "ASC built from a legacy codec ID");
    // 0x1194: the same fields with frameLengthFlag set (bit 13).
    check(read_aac_config({0x11, 0x94}).frame_samples == 960, "frameLengthFlag: 960 samples");
    // HE-AAC with explicit SBR: object type 5, core index 6 (24 kHz), 2
    // channels, extension index 3 (48 kHz), core type 2, frameLengthFlag 0:
    // 00101 0110 0010 0011 00010 0 -> 0x2b 0x11 0x88.
    const auto he_aac = read_aac_config({0x2b, 0x11, 0x88});
    check(he_aac.object_type == 2 && he_aac.sample_rate == 24000 && he_aac.frame_samples == 1024,
          "explicit SBR: core LC at 24 kHz, 1024-sample frames");
    expect_failure(RemuxFailure::unsupported, "AAC-LD (object type 23)",
                   [] { (void)read_aac_config({0xb9, 0x90}); });
    expect_failure(RemuxFailure::unsupported, "AAC rate without an index",
                   [] { (void)aac_config(2, 50000, 2); });
    // esds: ES (3, 25 bytes): ES_ID 0, flags 0; DecoderConfig (4, 17 bytes):
    // 0x40, audio stream, zero buffer size and bit rates, then its specific
    // info (5): the ASC; SLConfig (6): predefined 2. Box: 8 + 4 + 27 = 39.
    const Bytes mp4a = join({{0, 0, 0, 0x4b, 'm', 'p', '4', 'a', 0, 0, 0, 0, 0, 0, 0, 1},
                             {0, 0, 0, 0, 0, 0, 0, 0, 0, 2, 0, 16, 0, 0, 0, 0},
                             {0xbb, 0x80, 0, 0},
                             {0, 0, 0, 0x27, 'e', 's', 'd', 's', 0, 0, 0, 0},
                             {0x03, 0x19, 0, 0, 0},
                             {0x04, 0x11, 0x40, 0x15, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
                             {0x05, 0x02, 0x11, 0x90},
                             {0x06, 0x01, 0x02}});
    check(aac_sample_entry(2, 48000, asc) == mp4a, "mp4a entry with esds known answer");

    const Bytes avcc{1, 0x64, 0, 0x1f, 0xff};
    const auto square = visual_sample_entry("avc1", 1280, 720, 1280, 720, avcc);
    check(square.size() == 8 + 78 + 8 + avcc.size(), "square pixels: no pasp");
    check(std::equal(avcc.begin(), avcc.end(),
                     square.end() - static_cast<std::ptrdiff_t>(avcc.size())),
          "avcC copied verbatim");
    const auto anamorphic = visual_sample_entry("hvc1", 720, 480, 853, 480, avcc);
    // Pixel aspect (853 x 480) : (720 x 480) reduces to 853 : 720.
    const Bytes pasp{0, 0, 0, 16, 'p', 'a', 's', 'p', 0, 0, 0x03, 0x55, 0, 0, 0x02, 0xd0};
    check(anamorphic.size() >= pasp.size() &&
              std::equal(pasp.begin(), pasp.end(), anamorphic.end() - 16),
          "anamorphic: pasp 853:720");
    check(packed_language("eng") == 0x15c7 && packed_language("und") == 0x55c4 &&
              packed_language("english") == 0x55c4,
          "packed languages");
    const auto tkhd = track_header_box(TrackKind::video, 1, 1920, 1080);
    check(tkhd.size() == 92 && tkhd[tkhd.size() - 8] == 0x07 && tkhd[tkhd.size() - 7] == 0x80 &&
              tkhd[tkhd.size() - 4] == 0x04 && tkhd[tkhd.size() - 3] == 0x38,
          "tkhd display size 1920 x 1080 (16.16)");
}

// --- An independent EBML builder for test input ------------------------------

/// Element ID bytes as written in the specification (length marker kept).
Bytes id_bytes(std::uint32_t id) {
    Bytes out;
    for (int shift = 24; shift >= 0; shift -= 8) {
        const auto byte = static_cast<std::uint8_t>(id >> shift);
        if (byte || !out.empty() || shift == 0) {
            out.push_back(byte);
        }
    }
    return out;
}
/// Element with an 8-byte data size (0x01 marker), which EBML allows for any size.
Bytes element(std::uint32_t id, const Bytes& data) {
    Bytes out = id_bytes(id);
    out.push_back(0x01);
    for (int shift = 48; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(data.size() >> shift));
    }
    out.insert(out.end(), data.begin(), data.end());
    return out;
}
Bytes unknown_size_element(std::uint32_t id, const Bytes& data) {
    Bytes out = id_bytes(id);
    out.insert(out.end(), {0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff});
    out.insert(out.end(), data.begin(), data.end());
    return out;
}
Bytes uint_element(std::uint32_t id, std::uint64_t value) {
    Bytes data;
    for (int shift = 56; shift >= 0; shift -= 8) {
        if ((value >> shift) || !data.empty() || shift == 0) {
            data.push_back(static_cast<std::uint8_t>(value >> shift));
        }
    }
    return element(id, data);
}
Bytes string_element(std::uint32_t id, const std::string& value) {
    return element(id, Bytes(value.begin(), value.end()));
}
Bytes float_element(std::uint32_t id, float value) {
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof bits);
    return element(id,
                   {static_cast<std::uint8_t>(bits >> 24), static_cast<std::uint8_t>(bits >> 16),
                    static_cast<std::uint8_t>(bits >> 8), static_cast<std::uint8_t>(bits)});
}

/// Frame contents unique to each frame id, so the test finds frames in the
/// file by content instead of trusting the demuxer's arithmetic.
Bytes frame_bytes(std::uint8_t id, std::size_t size) {
    Bytes out(size, id);
    out[0] = 0xfe; // No frame content looks like the start of another frame.
    return out;
}

constexpr std::uint8_t keyframe_flag = 0x80;
enum class Lacing { none, xiph, ebml, fixed };

/// A SimpleBlock or Block body (RFC 9559 10): track, relative timestamp,
/// flags and the frames, laced as asked.
Bytes block(std::uint8_t track, std::int16_t relative, std::uint8_t flags, Lacing lacing,
            const std::vector<Bytes>& frames) {
    Bytes out{static_cast<std::uint8_t>(0x80 | track), static_cast<std::uint8_t>(relative >> 8),
              static_cast<std::uint8_t>(relative)};
    const std::uint8_t lacing_bits = lacing == Lacing::xiph    ? 0x02
                                     : lacing == Lacing::ebml  ? 0x06
                                     : lacing == Lacing::fixed ? 0x04
                                                               : 0x00;
    out.push_back(static_cast<std::uint8_t>(flags | lacing_bits));
    if (lacing != Lacing::none) {
        out.push_back(static_cast<std::uint8_t>(frames.size() - 1));
    }
    if (lacing == Lacing::xiph) {
        for (std::size_t index = 0; index + 1 < frames.size(); ++index) {
            auto size = frames[index].size();
            for (; size >= 255; size -= 255) {
                out.push_back(255);
            }
            out.push_back(static_cast<std::uint8_t>(size));
        }
    } else if (lacing == Lacing::ebml) {
        out.push_back(static_cast<std::uint8_t>(0x80 | frames[0].size())); // Sizes below 127.
        for (std::size_t index = 1; index + 1 < frames.size(); ++index) {
            // One-byte signed vint: delta + 63 under the 0x80 marker.
            const auto delta =
                static_cast<int>(frames[index].size()) - static_cast<int>(frames[index - 1].size());
            out.push_back(static_cast<std::uint8_t>(0x80 | (delta + 63)));
        }
    }
    for (const auto& frame : frames) {
        out.insert(out.end(), frame.begin(), frame.end());
    }
    return out;
}

constexpr std::uint32_t ebml_id = 0x1A45DFA3;
constexpr std::uint32_t segment_id = 0x18538067;
constexpr std::uint32_t info_id = 0x1549A966;
constexpr std::uint32_t tracks_id = 0x1654AE6B;
constexpr std::uint32_t track_entry_id = 0xAE;
constexpr std::uint32_t cluster_id = 0x1F43B675;
constexpr std::uint32_t cues_id = 0x1C53BB6B;
constexpr std::uint32_t simple_block_id = 0xA3;
constexpr std::uint32_t block_group_id = 0xA0;

struct TrackSpec {
    std::uint64_t number = 1;
    std::uint64_t type = 1;
    std::string codec = "V_MPEG4/ISO/AVC";
    Bytes codec_private{1, 0x64, 0, 0x1f, 0xff};
    bool flag_default = true;
    bool encoded = false;
    std::uint64_t codec_delay_ns = 0;
    std::uint64_t display_width = 0; // Omitted when zero.
    // Subtitle tracks (type 17): written when not empty / true.
    std::string name;
    std::string language;
    bool forced = false;
    bool hearing_impaired = false;
};

Bytes track_entry(const TrackSpec& spec) {
    Bytes data =
        join({uint_element(0xD7, spec.number), uint_element(0x83, spec.type),
              string_element(0x86, spec.codec), uint_element(0x88, spec.flag_default ? 1 : 0)});
    if (!spec.codec_private.empty()) {
        data = join({data, element(0x63A2, spec.codec_private)});
    }
    if (spec.codec_delay_ns) {
        data = join({data, uint_element(0x56AA, spec.codec_delay_ns)});
    }
    if (spec.encoded) {
        data = join({data, element(0x6D80, element(0x6240, uint_element(0x5031, 0)))});
    }
    if (!spec.name.empty()) {
        data = join({data, string_element(0x536E, spec.name)});
    }
    if (!spec.language.empty()) {
        data = join({data, string_element(0x22B59C, spec.language)});
    }
    if (spec.forced) {
        data = join({data, uint_element(0x55AA, 1)});
    }
    if (spec.hearing_impaired) {
        data = join({data, uint_element(0x55AB, 1)});
    }
    if (spec.type == 17) {
        // Subtitle tracks have neither a Video nor an Audio element.
    } else if (spec.type == 1) {
        Bytes video = join({uint_element(0xB0, 1280), uint_element(0xBA, 720)});
        if (spec.display_width) {
            video = join({video, uint_element(0x54B0, spec.display_width)});
        }
        data = join({data, element(0xE0, video)});
    } else {
        data = join(
            {data, element(0xE1, join({float_element(0xB5, 48000.0F), uint_element(0x9F, 2)}))});
    }
    return element(track_entry_id, data);
}

TrackSpec video_spec() {
    return {};
}
TrackSpec aac_spec(std::uint64_t number = 2) {
    TrackSpec spec;
    spec.number = number;
    spec.type = 2;
    spec.codec = "A_AAC";
    spec.codec_private = {0x11, 0x90}; // AAC-LC, 48 kHz, stereo.
    spec.codec_delay_ns = 21'333'333;  // 1024 samples at 48 kHz.
    return spec;
}

Bytes mkv_file(const std::vector<TrackSpec>& tracks, const Bytes& clusters,
               std::uint64_t timestamp_scale = 1'000'000,
               const std::string& doc_type = "matroska") {
    Bytes track_data;
    for (const auto& track : tracks) {
        track_data = join({track_data, track_entry(track)});
    }
    const auto header =
        element(ebml_id, join({uint_element(0x4286, 1), string_element(0x4282, doc_type)}));
    const auto segment = join({element(info_id, uint_element(0x2AD7B1, timestamp_scale)),
                               element(tracks_id, track_data), clusters});
    return join({header, element(segment_id, segment)});
}

// The main fixture: video PTS (ms) 0 (key), 67, 33, 100 (BlockGroup with a
// ReferenceBlock and BlockDuration 33), 200 (key, in an unknown-size cluster
// ended by Cues). Audio frames of 1024 samples at 48 kHz (21.333 ms):
// 0-2 Xiph-laced at 0 ms, 3 at 64 ms, 4-5 EBML-laced at 85 ms (16 samples
// before the continuous 4096, within rounding), 6-7 fixed-laced at 128 ms.
struct Fixture {
    Bytes file;
    std::vector<Bytes> video;
    std::vector<Bytes> audio;
};

Fixture main_fixture() {
    Fixture fixture;
    for (std::uint8_t id = 0; id < 5; ++id) {
        fixture.video.push_back(frame_bytes(static_cast<std::uint8_t>(0x10 + id), 20 + id));
    }
    const std::size_t audio_sizes[8] = {4, 5, 6, 7, 8, 9, 3, 3};
    for (std::uint8_t id = 0; id < 8; ++id) {
        fixture.audio.push_back(frame_bytes(static_cast<std::uint8_t>(0x30 + id), audio_sizes[id]));
    }
    const auto& v = fixture.video;
    const auto& a = fixture.audio;
    const auto cluster1 = element(
        cluster_id,
        join(
            {uint_element(0xE7, 0),
             element(simple_block_id, block(1, 0, keyframe_flag, Lacing::none, {v[0]})),
             element(simple_block_id, block(2, 0, keyframe_flag, Lacing::xiph, {a[0], a[1], a[2]})),
             element(simple_block_id, block(1, 67, 0, Lacing::none, {v[1]})),
             element(simple_block_id, block(1, 33, 0, Lacing::none, {v[2]})),
             element(simple_block_id, block(2, 64, keyframe_flag, Lacing::none, {a[3]}))}));
    const auto cluster2 = element(
        cluster_id,
        join({uint_element(0xE7, 100),
              element(block_group_id,
                      join({element(0xA1, block(1, 0, 0, Lacing::none, {v[3]})),
                            uint_element(0xFB, 0x21), // ReferenceBlock: not a keyframe.
                            uint_element(0x9B, 33)})),
              element(simple_block_id, block(2, -15, keyframe_flag, Lacing::ebml, {a[4], a[5]})),
              element(simple_block_id, block(2, 28, keyframe_flag, Lacing::fixed, {a[6], a[7]}))}));
    const auto cluster3 = unknown_size_element(
        cluster_id,
        join({uint_element(0xE7, 200),
              element(simple_block_id, block(1, 0, keyframe_flag, Lacing::none, {v[4]}))}));
    const auto cues = element(cues_id, Bytes(4, 0));
    fixture.file = mkv_file({video_spec(), aac_spec()}, join({cluster1, cluster2, cluster3, cues}));
    return fixture;
}

RandomReader memory_reader(const Bytes& file) {
    return [&file](std::uint64_t offset, std::uint8_t* output, std::size_t length) {
        if (offset > file.size() || length > file.size() - offset) {
            throw std::out_of_range("read past the synthetic file");
        }
        std::memcpy(output, file.data() + offset, length);
    };
}

std::uint64_t offset_of(const Bytes& file, const Bytes& frame) {
    const auto found = std::search(file.begin(), file.end(), frame.begin(), frame.end());
    return static_cast<std::uint64_t>(found - file.begin());
}

void demux_tests() {
    group = "MKV demux";
    const auto fixture = main_fixture();
    check(starts_like_matroska(fixture.file.data(), fixture.file.size()), "EBML magic detected");
    const auto movie = read_mkv(memory_reader(fixture.file), fixture.file.size());
    check(movie.tracks.size() == 2, "video and audio selected");
    if (movie.tracks.size() != 2) {
        return;
    }
    const auto& video = movie.tracks[0];
    check(video.codec == "avc1" && video.timescale == 1000,
          "video codec and millisecond timescale");
    check(video.sample_entry ==
              visual_sample_entry("avc1", 1280, 720, 1280, 720, {1, 0x64, 0, 0x1f, 0xff}),
          "avc1 entry from CodecPrivate");
    check(video.language == packed_language("eng"), "Matroska default language eng");
    // PTS 0, 67, 33, 100, 200 -> decode 0, 33, 67, 100, 200; reorder delay 34.
    const std::uint64_t decode[5] = {0, 33, 67, 100, 200};
    const std::int32_t composition[5] = {34, 68, 0, 34, 34};
    const std::uint32_t duration[5] = {33, 34, 33, 100, 100};
    const bool sync[5] = {true, false, false, false, true};
    check(video.samples.size() == 5, "five video frames");
    for (std::size_t index = 0; index < video.samples.size() && index < 5; ++index) {
        const auto& sample = video.samples[index];
        const auto at = " (video " + std::to_string(index) + ")";
        check(sample.offset == offset_of(fixture.file, fixture.video[index]) &&
                  sample.size == fixture.video[index].size(),
              "frame location" + at);
        check(sample.decode_time == decode[index], "decode time" + at);
        check(sample.composition_offset == composition[index], "composition offset" + at);
        check(sample.duration == duration[index], "duration" + at);
        check(sample.sync == sync[index], "keyframe flag" + at);
    }
    check(video.edits.size() == 1 && video.edits[0].media_time == 34 &&
              video.edits[0].segment_duration == 0,
          "edit list removes the 34 ms reorder delay");

    const auto& audio = movie.tracks[1];
    check(audio.codec == "mp4a" && audio.timescale == 48000,
          "audio codec and sample-rate timescale");
    check(audio.sample_entry == aac_sample_entry(2, 48000, {0x11, 0x90}),
          "mp4a entry from CodecPrivate");
    check(audio.edits.size() == 1 && audio.edits[0].media_time == 1024,
          "CodecDelay becomes a 1024-sample edit");
    check(audio.samples.size() == 8, "eight audio frames from three lacing modes");
    for (std::size_t index = 0; index < audio.samples.size() && index < 8; ++index) {
        const auto& sample = audio.samples[index];
        const auto at = " (audio " + std::to_string(index) + ")";
        check(sample.offset == offset_of(fixture.file, fixture.audio[index]) &&
                  sample.size == fixture.audio[index].size(),
              "frame location" + at);
        check(sample.decode_time == 1024 * index && sample.duration == 1024 && sample.sync,
              "continuous 1024-sample timeline" + at);
    }
}

void selection_and_refusal_tests() {
    group = "MKV selection and refusals";
    // One video frame and one frame for each audio track number 2 and 3.
    const auto one_frame =
        element(cluster_id,
                join({uint_element(0xE7, 0),
                      element(simple_block_id,
                              block(1, 0, keyframe_flag, Lacing::none, {frame_bytes(0x10, 8)})),
                      element(simple_block_id,
                              block(2, 0, keyframe_flag, Lacing::none, {frame_bytes(0x20, 4)})),
                      element(simple_block_id,
                              block(3, 0, keyframe_flag, Lacing::none, {frame_bytes(0x21, 4)}))}));
    const auto refuse = [](RemuxFailure expected, const std::string& scenario, const Bytes& file) {
        expect_failure(expected, scenario,
                       [&] { (void)read_mkv(memory_reader(file), file.size()); });
    };

    auto first = aac_spec(2);
    first.flag_default = false;
    auto preferred = aac_spec(3);
    preferred.codec_private = {0x12, 0x10}; // 44.1 kHz marks the chosen track.
    const auto two_audio = mkv_file({video_spec(), first, preferred}, one_frame);
    const auto chosen = read_mkv(memory_reader(two_audio), two_audio.size());
    check(chosen.tracks.size() == 2 && chosen.tracks[1].timescale == 44100,
          "default-flagged audio track preferred");
    auto dts = aac_spec(2);
    dts.codec = "A_DTS";
    const auto with_dts = mkv_file({video_spec(), dts, aac_spec(3)}, one_frame);
    check(read_mkv(memory_reader(with_dts), with_dts.size()).tracks.size() == 2,
          "remuxable audio chosen over an unremuxable default track");

    refuse(RemuxFailure::unsupported, "only DTS audio", mkv_file({video_spec(), dts}, one_frame));
    auto anamorphic = video_spec();
    anamorphic.display_width = 1707; // DisplayHeight omitted: it defaults to 720.
    const auto wide = mkv_file({anamorphic}, one_frame);
    check(read_mkv(memory_reader(wide), wide.size()).tracks[0].sample_entry ==
              visual_sample_entry("avc1", 1280, 720, 1707, 720, {1, 0x64, 0, 0x1f, 0xff}),
          "DisplayWidth alone keeps its aspect ratio (pasp)");
    auto mpeg4 = video_spec();
    mpeg4.codec = "V_MPEG4/ISO/ASP";
    refuse(RemuxFailure::unsupported, "MPEG-4 Part 2 video", mkv_file({mpeg4}, one_frame));
    auto encoded = video_spec();
    encoded.encoded = true;
    refuse(RemuxFailure::unsupported, "ContentEncodings", mkv_file({encoded}, one_frame));
    auto no_config = video_spec();
    no_config.codec_private.clear();
    refuse(RemuxFailure::malformed, "AVC without CodecPrivate", mkv_file({no_config}, one_frame));
    refuse(RemuxFailure::unsupported, "TimestampScale not dividing 10^9",
           mkv_file({video_spec()}, one_frame, 3));
    refuse(RemuxFailure::unsupported, "foreign EBML document type",
           mkv_file({video_spec()}, one_frame, 1'000'000, "notmatroska"));
    const auto laced_video =
        element(cluster_id,
                join({uint_element(0xE7, 0),
                      element(simple_block_id, block(1, 0, keyframe_flag, Lacing::fixed,
                                                     {frame_bytes(1, 4), frame_bytes(2, 4)}))}));
    refuse(RemuxFailure::unsupported, "laced video", mkv_file({video_spec()}, laced_video));
    const auto negative =
        element(cluster_id, join({uint_element(0xE7, 0),
                                  element(simple_block_id, block(1, -1, keyframe_flag, Lacing::none,
                                                                 {frame_bytes(1, 4)}))}));
    refuse(RemuxFailure::unsupported, "negative timestamp", mkv_file({video_spec()}, negative));
    Bytes bad_xiph =
        block(2, 0, keyframe_flag, Lacing::xiph, {frame_bytes(1, 4), frame_bytes(2, 4)});
    bad_xiph[5] = 200; // The first lace now claims more bytes than the block holds.
    refuse(RemuxFailure::malformed, "Xiph lace past its block",
           mkv_file({video_spec(), aac_spec()},
                    element(cluster_id,
                            join({uint_element(0xE7, 0),
                                  element(simple_block_id, block(1, 0, keyframe_flag, Lacing::none,
                                                                 {frame_bytes(0x10, 8)})),
                                  element(simple_block_id, bad_xiph)}))));
    auto truncated = mkv_file({video_spec()}, one_frame);
    truncated.resize(truncated.size() - 4);
    refuse(RemuxFailure::malformed, "truncated cluster", truncated);

    // E-AC-3 whose first block holds an independent and a dependent frame.
    Bytes independent = eac3_header;
    independent[2] = 0x00; // strmtyp 0, frmsiz 7: a 16-byte frame.
    independent[3] = 0x07;
    Bytes dependent_frame = independent;
    dependent_frame[2] = 0x40; // strmtyp 1: dependent.
    auto eac3 = aac_spec(2);
    eac3.codec = "A_EAC3";
    eac3.codec_private.clear();
    eac3.codec_delay_ns = 0;
    const auto dependent_block =
        element(cluster_id,
                join({uint_element(0xE7, 0),
                      element(simple_block_id,
                              block(1, 0, keyframe_flag, Lacing::none, {frame_bytes(0x10, 8)})),
                      element(simple_block_id, block(2, 0, keyframe_flag, Lacing::none,
                                                     {join({independent, dependent_frame})}))}));
    refuse(RemuxFailure::unsupported, "E-AC-3 with a dependent substream",
           mkv_file({video_spec(), eac3}, dependent_block));
    const auto single_block = element(
        cluster_id,
        join({uint_element(0xE7, 0),
              element(simple_block_id,
                      block(1, 0, keyframe_flag, Lacing::none, {frame_bytes(0x10, 8)})),
              element(simple_block_id, block(2, 0, keyframe_flag, Lacing::none, {independent}))}));
    const auto eac3_movie = read_mkv(memory_reader(mkv_file({video_spec(), eac3}, single_block)),
                                     mkv_file({video_spec(), eac3}, single_block).size());
    check(eac3_movie.tracks.size() == 2 && eac3_movie.tracks[1].codec == "ec-3" &&
              eac3_movie.tracks[1].timescale == 48000 &&
              eac3_movie.tracks[1].samples[0].duration == 1536,
          "E-AC-3 track from its first frame");
}

void presentation_tests() {
    group = "MKV remuxed presentation";
    const auto fixture = main_fixture();
    const auto file = std::make_shared<const Bytes>(fixture.file);
    MediaSource source{
        [file] { return static_cast<std::uint64_t>(file->size()); },
        [file](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
               const MediaReadContext&) -> std::size_t {
            if (offset >= file->size()) {
                return 0;
            }
            const auto count = std::min<std::uint64_t>(capacity, file->size() - offset);
            std::memcpy(output, file->data() + offset, static_cast<std::size_t>(count));
            return static_cast<std::size_t>(count);
        }};
    const auto remuxed = remux_to_hls(std::move(source));
    check(remuxed.segment_count == 1 && remuxed.resources.size() == 3,
          "keyframes at 0 and 0.2 s: one segment");
    if (remuxed.resources.size() != 3) {
        return;
    }
    std::atomic_bool stopped{false};
    std::atomic_bool cancelled{false};
    const MediaReadContext context{&stopped, &cancelled,
                                   std::chrono::steady_clock::now() + std::chrono::minutes(1)};
    const auto& segment = remuxed.resources[2];
    Bytes bytes(static_cast<std::size_t>(segment.source.size()));
    std::size_t position = 0;
    while (position < bytes.size()) {
        const auto count = segment.source.read_at(position, bytes.data() + position,
                                                  bytes.size() - position, context);
        if (count == 0) {
            break;
        }
        position += count;
    }
    Bytes payload;
    for (const auto& frame : fixture.video) {
        payload = join({payload, frame});
    }
    for (const auto& frame : fixture.audio) {
        payload = join({payload, frame});
    }
    check(position == bytes.size() && bytes.size() > payload.size() &&
              std::equal(payload.begin(), payload.end(),
                         bytes.end() - static_cast<std::ptrdiff_t>(payload.size())),
          "segment ends with the video frames, then the audio frames, in order");
}
} // namespace

Bytes text(const std::string& value) {
    return Bytes(value.begin(), value.end());
}

/// Video key frames at 0 and 7 s (two segments on the 6 s grid) and three
/// subtitle tracks: SubRip (number 3, English, with BlockGroup durations and
/// one SimpleBlock without), ASS (number 4, forced, hearing impaired) and a
/// PGS bitmap track (number 5), which is left out.
Bytes subtitle_fixture(std::uint64_t origin_ms = 0) {
    TrackSpec srt;
    srt.number = 3;
    srt.type = 17;
    srt.codec = "S_TEXT/UTF8";
    srt.codec_private.clear();
    srt.flag_default = true;
    srt.name = "English";
    TrackSpec ass = srt;
    ass.number = 4;
    ass.codec = "S_TEXT/ASS";
    ass.codec_private = text("[Script Info]");
    ass.flag_default = false;
    ass.name.clear();
    ass.language = "fre";
    ass.forced = true;
    ass.hearing_impaired = true;
    TrackSpec pgs = srt;
    pgs.number = 5;
    pgs.codec = "S_HDMV/PGS";
    pgs.name = "Bitmap";
    const auto timed_block = [](std::uint8_t track, std::int16_t relative,
                                const std::string& payload, std::uint64_t duration) {
        return element(
            block_group_id,
            join({element(0xA1, block(track, relative, 0, Lacing::none, {text(payload)})),
                  uint_element(0x9B, duration)}));
    };
    const auto cluster1 =
        element(cluster_id,
                join({uint_element(0xE7, origin_ms),
                      element(simple_block_id,
                              block(1, 0, keyframe_flag, Lacing::none, {frame_bytes(0x10, 8)})),
                      timed_block(3, 1000, "<i>Hello</i> & welcome", 1500),
                      timed_block(4, 2000, "0,0,Default,,0,0,0,,{\\an8}Bonjour\\Nmonde", 1000),
                      timed_block(5, 2500, "bitmap bytes", 1000),
                      timed_block(3, 5500, "Across the cut", 1000),
                      element(simple_block_id,
                              block(1, 7000, keyframe_flag, Lacing::none, {frame_bytes(0x11, 8)})),
                      element(simple_block_id, block(3, 9000, keyframe_flag, Lacing::none,
                                                     {text("No duration")}))}));
    return mkv_file({video_spec(), srt, ass, pgs}, cluster1);
}

/// Remuxes `file` and returns every resource's bytes by name, and the entry point.
std::map<std::string, std::string> remux_texts(const Bytes& file, std::string& entry) {
    const auto shared = std::make_shared<const Bytes>(file);
    MediaSource source{
        [shared] { return static_cast<std::uint64_t>(shared->size()); },
        [shared](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
                 const MediaReadContext&) -> std::size_t {
            if (offset >= shared->size()) {
                return 0;
            }
            const auto count = std::min<std::uint64_t>(capacity, shared->size() - offset);
            std::memcpy(output, shared->data() + offset, static_cast<std::size_t>(count));
            return static_cast<std::size_t>(count);
        }};
    const auto remuxed = remux_to_hls(std::move(source));
    entry = remuxed.playlist_name;
    std::atomic_bool stopped{false};
    std::atomic_bool cancelled{false};
    const MediaReadContext context{&stopped, &cancelled,
                                   std::chrono::steady_clock::now() + std::chrono::minutes(1)};
    std::map<std::string, std::string> texts;
    for (const auto& resource : remuxed.resources) {
        std::string out(static_cast<std::size_t>(media_resource_size(resource, context)), '\0');
        std::size_t position = 0;
        while (position < out.size()) {
            const auto count =
                resource.source.read_at(position, reinterpret_cast<std::uint8_t*>(&out[position]),
                                        out.size() - position, context);
            if (count == 0) {
                break;
            }
            position += count;
        }
        texts[resource.name] = out;
    }
    return texts;
}

/// Bits per second of `bytes` over `microseconds`, as the remux computes it.
std::uint64_t rate(std::uint64_t bytes, std::uint64_t microseconds) {
    return bytes * 8 * 1'000'000 / microseconds;
}

void subtitle_tests() {
    group = "MKV text subtitles";
    const auto file = subtitle_fixture();
    const auto movie = read_mkv(memory_reader(file), file.size());
    check(movie.tracks.size() == 1, "video only among the media tracks");
    check(movie.text_tracks.size() == 2, "SubRip and ASS kept, PGS left out");
    if (movie.text_tracks.size() != 2) {
        return;
    }
    const auto& srt = movie.text_tracks[0];
    check(srt.name == "English" && srt.language == "en" && srt.default_track && !srt.forced &&
              !srt.hearing_impaired,
          "SubRip track: name, default language eng -> en, flags");
    check(srt.cues.size() == 3, "three SubRip cues");
    if (srt.cues.size() == 3) {
        check(srt.cues[0].start_us == 1'000'000 && srt.cues[0].end_us == 2'500'000 &&
                  srt.cues[0].text == "<i>Hello</i> &amp; welcome",
              "BlockGroup cue: BlockDuration, converted text");
        check(srt.cues[1].start_us == 5'500'000 && srt.cues[1].end_us == 6'500'000,
              "cue across the segment cut");
        check(srt.cues[2].start_us == 9'000'000 && srt.cues[2].end_us == 14'000'000 &&
                  srt.cues[2].text == "No duration",
              "SimpleBlock cue without a duration or successor: 5 s");
    }
    const auto& ass = movie.text_tracks[1];
    check(ass.name.empty() && ass.language == "fr" && !ass.default_track && ass.forced &&
              ass.hearing_impaired,
          "ASS track: language fre -> fr, forced, hearing impaired");
    check(ass.cues.size() == 1 && ass.cues[0].text == "Bonjour\nmonde" &&
              ass.cues[0].start_us == 2'000'000 && ass.cues[0].end_us == 3'000'000,
          "ASS cue: Text field without overrides, \\N a line break");

    std::string entry;
    auto texts = remux_texts(file, entry);
    check(entry == "main.m3u8", "subtitles make main.m3u8 the entry point");
    const auto resource_text = [&texts](const std::string& name) {
        const auto found = texts.find(name);
        return found == texts.end() ? std::string("(missing)") : found->second;
    };
    const auto main = resource_text("main.m3u8");
    check(main.find("NAME=\"English\",LANGUAGE=\"en\",DEFAULT=YES,AUTOSELECT=YES,FORCED=NO,"
                    "URI=\"t0.m3u8\"") != std::string::npos,
          "English rendition, default");
    check(main.find("NAME=\"fr\",LANGUAGE=\"fr\",DEFAULT=NO,AUTOSELECT=YES,FORCED=YES,"
                    "CHARACTERISTICS=") != std::string::npos,
          "unnamed rendition named by its language, forced, accessibility characteristics");
    check(main.find("CODECS=\"avc1.64001f\",RESOLUTION=1280x720") != std::string::npos &&
              main.find("SUBTITLES=\"subs\"\nindex.m3u8\n") != std::string::npos,
          "stream info: codecs, resolution, subtitle group, media playlist");
    check(resource_text("t0.m3u8") ==
              "#EXTM3U\n#EXT-X-VERSION:7\n#EXT-X-TARGETDURATION:7\n#EXT-X-MEDIA-SEQUENCE:0\n"
              "#EXT-X-PLAYLIST-TYPE:VOD\n#EXTINF:7.000000,\nt0s0.vtt\n#EXTINF:7.000000,\n"
              "t0s1.vtt\n#EXT-X-ENDLIST\n",
          "subtitle playlist follows the video EXTINF values");
    check(resource_text("t0s0.vtt") ==
              "WEBVTT\nX-TIMESTAMP-MAP=MPEGTS:0,LOCAL:00:00:00.000\n"
              "\n00:00:01.000 --> 00:00:02.500\n<i>Hello</i> &amp; welcome\n"
              "\n00:00:05.500 --> 00:00:06.500\nAcross the cut\n",
          "first WebVTT segment: cues of [0, 7 s)");
    check(resource_text("t0s1.vtt") == "WEBVTT\nX-TIMESTAMP-MAP=MPEGTS:0,LOCAL:00:00:00.000\n"
                                       "\n00:00:09.000 --> 00:00:14.000\nNo duration\n",
          "last WebVTT segment runs to the end");

    // BANDWIDTH and AVERAGE-BANDWIDTH: the video segments plus the largest
    // subtitle rendition; both segments last 7 s.
    const std::uint64_t seven_s = 7'000'000;
    const auto size = [&](const std::string& name) {
        return static_cast<std::uint64_t>(resource_text(name).size());
    };
    const auto peak = std::max(rate(size("s0.m4s"), seven_s), rate(size("s1.m4s"), seven_s)) +
                      std::max({rate(size("t0s0.vtt"), seven_s), rate(size("t0s1.vtt"), seven_s),
                                rate(size("t1s0.vtt"), seven_s), rate(size("t1s1.vtt"), seven_s)});
    const auto average = rate(size("s0.m4s") + size("s1.m4s"), 2 * seven_s) +
                         std::max(rate(size("t0s0.vtt") + size("t0s1.vtt"), 2 * seven_s),
                                  rate(size("t1s0.vtt") + size("t1s1.vtt"), 2 * seven_s));
    check(main.find("BANDWIDTH=" + std::to_string(peak) +
                    ",AVERAGE-BANDWIDTH=" + std::to_string(average) + ",") != std::string::npos,
          "bandwidth includes the largest subtitle rendition");

    // A timeline that starts at 10 s: windows follow the first video
    // timestamp, so the 11 s cue still belongs to the first segment.
    std::string shifted_entry;
    const auto shifted = remux_texts(subtitle_fixture(10'000), shifted_entry);
    check(shifted.at("t0s0.vtt") == "WEBVTT\nX-TIMESTAMP-MAP=MPEGTS:0,LOCAL:00:00:00.000\n"
                                    "\n00:00:11.000 --> 00:00:12.500\n<i>Hello</i> &amp; welcome\n"
                                    "\n00:00:15.500 --> 00:00:16.500\nAcross the cut\n",
          "nonzero origin: first WebVTT segment holds the cues of [10 s, 17 s)");
    check(shifted.at("t0s1.vtt").find("00:00:19.000 --> 00:00:24.000") != std::string::npos,
          "nonzero origin: last segment holds the later cue");
}

// --- Indexed reading -----------------------------------------------------------

/// One video block: presentation time relative to its cluster, and whether
/// it is a keyframe.
struct VideoFrameSpec {
    int relative_ms = 0;
    bool keyframe = false;
};

/// One cluster: video frames (keyframe flags as given) at `video_ms` relative
/// times and AAC frames every 64 ms (three 1024-sample frames at 48 kHz each
/// block, Xiph-laced) over [start, start + span).
/// One SubRip block on track 3: a BlockGroup with BlockDuration, or a
/// SimpleBlock without any duration when duration_ms is 0.
struct TextBlockSpec {
    int relative_ms = 0;
    std::string text;
    std::uint64_t duration_ms = 0;
};

struct ClusterSpec {
    std::uint64_t start_ms = 0;
    std::vector<VideoFrameSpec> video;
    std::int16_t audio_span_ms = 0;
    std::vector<TextBlockSpec> text;
};

/// Frame contents unique per cluster and frame index.
Bytes cluster_bytes(const ClusterSpec& spec, std::uint8_t cluster_id_byte) {
    Bytes content = uint_element(0xE7, spec.start_ms);
    std::uint8_t serial = 0;
    for (const auto& frame : spec.video) {
        const auto relative = static_cast<std::int16_t>(frame.relative_ms);
        const auto key = frame.keyframe;
        content = join({content, element(simple_block_id,
                                         block(1, relative, key ? keyframe_flag : 0, Lacing::none,
                                               {frame_bytes(cluster_id_byte, 20u + serial++)}))});
    }
    for (std::int16_t at = 0; at < spec.audio_span_ms; at = static_cast<std::int16_t>(at + 64)) {
        content = join(
            {content, element(simple_block_id, block(2, at, keyframe_flag, Lacing::xiph,
                                                     {frame_bytes(0x80, 4), frame_bytes(0x81, 5),
                                                      frame_bytes(0x82, 6)}))});
    }
    for (const auto& cue : spec.text) {
        const auto body = block(3, static_cast<std::int16_t>(cue.relative_ms), keyframe_flag,
                                Lacing::none, {Bytes(cue.text.begin(), cue.text.end())});
        content =
            join({content, cue.duration_ms ? element(block_group_id,
                                                     join({element(0xA1, body),
                                                           uint_element(0x9B, cue.duration_ms)}))
                                           : element(simple_block_id, body)});
    }
    return element(cluster_id, content);
}

/// A Matroska file whose SeekHead (first in the Segment) points to Cues after
/// the clusters; one cue point per cluster that starts with a keyframe.
Bytes indexed_mkv(const std::vector<ClusterSpec>& specs, bool with_cues = true,
                  std::size_t first_cued_cluster = 0) {
    Bytes track_data = join({track_entry(video_spec()), track_entry([] {
                                 auto audio = aac_spec();
                                 audio.codec_delay_ns = 0;
                                 return audio;
                             }())});
    if (std::any_of(specs.begin(), specs.end(),
                    [](const ClusterSpec& spec) { return !spec.text.empty(); })) {
        TrackSpec subtitles;
        subtitles.number = 3;
        subtitles.type = 17;
        subtitles.codec = "S_TEXT/UTF8";
        subtitles.codec_private.clear();
        subtitles.flag_default = false;
        track_data = join({track_data, track_entry(subtitles)});
    }
    const auto info = element(info_id, uint_element(0x2AD7B1, 1'000'000));
    const auto tracks = element(tracks_id, track_data);
    std::vector<Bytes> clusters;
    for (std::size_t index = 0; index < specs.size(); ++index) {
        clusters.push_back(cluster_bytes(specs[index], static_cast<std::uint8_t>(0x10 + index)));
    }
    // SeekHead with one Seek whose 8-byte SeekPosition is patched below.
    const auto fixed_position = [](std::uint64_t value) {
        Bytes out;
        for (int shift = 56; shift >= 0; shift -= 8) {
            out.push_back(static_cast<std::uint8_t>(value >> shift));
        }
        return element(0x53AC, out);
    };
    const auto seek_head = [&](std::uint64_t cues_position) {
        return element(0x114D9B74, element(0x4DBB, join({element(0x53AB, {0x1C, 0x53, 0xBB, 0x6B}),
                                                         fixed_position(cues_position)})));
    };
    const auto seek_bytes = seek_head(0).size();
    std::uint64_t position = seek_bytes + info.size() + tracks.size(); // Segment-relative.
    Bytes cue_points;
    Bytes all_clusters;
    for (std::size_t index = 0; index < specs.size(); ++index) {
        const auto& spec = specs[index];
        if (index >= first_cued_cluster && !spec.video.empty() && spec.video.front().keyframe &&
            spec.video.front().relative_ms == 0) {
            cue_points =
                join({cue_points,
                      element(0xBB, join({uint_element(0xB3, spec.start_ms),
                                          element(0xB7, join({uint_element(0xF7, 1),
                                                              uint_element(0xF1, position)}))}))});
        }
        position += clusters[index].size();
        all_clusters = join({all_clusters, clusters[index]});
    }
    const auto cues = element(0x1C53BB6B, cue_points);
    const auto segment_data = with_cues
                                  ? join({seek_head(position), info, tracks, all_clusters, cues})
                                  : join({info, tracks, all_clusters});
    const auto header =
        element(ebml_id, join({uint_element(0x4286, 1), string_element(0x4282, "matroska")}));
    return join({header, element(segment_id, segment_data)});
}

/// Keyframe clusters at 0, 3, 6.5, 9 and 12.5 s, 15 fps (67 ms frames) with
/// one B-frame reordering per pair, and audio throughout.
std::vector<ClusterSpec> regular_clusters() {
    std::vector<ClusterSpec> specs;
    for (const std::uint64_t start : {0u, 3000u, 6500u, 9000u, 12500u}) {
        ClusterSpec spec;
        spec.start_ms = start;
        const std::int16_t span = start == 3000 || start == 9000 ? 3500 : 2500;
        spec.video.push_back({0, true});
        // Decode order I P B P B ...: P at +2 frames, B at +1, step 2 frames.
        for (std::int16_t frame = 2; frame * 67 < span;
             frame = static_cast<std::int16_t>(frame + 2)) {
            spec.video.push_back({static_cast<std::int16_t>(frame * 67), false});
            spec.video.push_back({static_cast<std::int16_t>((frame - 1) * 67), false});
        }
        spec.audio_span_ms = span;
        specs.push_back(spec);
    }
    return specs;
}

void indexed_reading_tests() {
    group = "indexed MKV";
    const auto specs = regular_clusters();
    const auto file = indexed_mkv(specs);
    const auto index = MkvIndex::open(memory_reader(file), file.size());
    check(index != nullptr, "opened from its Cues");
    if (!index) {
        return;
    }
    // 6 s grid over cue times 0, 3, 6.5, 9, 12.5: starts at 0, 6.5 and 12.5 s.
    check(index->segment_count() == 3, "three segments on the cue grid");
    check(index->segment_start_us(0) == 0 && index->segment_start_us(1) == 6'500'000 &&
              index->segment_start_us(2) == 12'500'000,
          "segments start at cue times");
    check(index->segment_duration_us(0) == 6'500'000 && index->segment_duration_us(1) == 6'000'000,
          "durations between cue times");

    // The same file without Cues is read in full; each indexed segment must
    // hold the same sample tables as the full scan's segment.
    const auto plain = indexed_mkv(specs, false);
    check(MkvIndex::open(memory_reader(plain), plain.size()) == nullptr,
          "without Cues the full scan is used");
    const auto full = read_mkv(memory_reader(plain), plain.size());
    const auto layouts = plan_segments(full);
    check(layouts.size() == 3, "the full scan plans the same three segments");
    for (std::size_t segment = 0; segment < 3 && segment < layouts.size(); ++segment) {
        const auto read = index->read_segment(segment, memory_reader(file));
        for (std::size_t track = 0; track < 2; ++track) {
            const auto range = layouts[segment].ranges[track];
            const auto& expected = full.tracks[track].samples;
            const auto& actual = read.samples[track];
            // Offsets differ by the SeekHead; everything else must match.
            bool same = actual.size() == range.count();
            for (std::size_t sample = 0; same && sample < actual.size(); ++sample) {
                const auto& a = actual[sample];
                const auto& b = expected[range.first + sample];
                same = a.size == b.size && a.decode_time == b.decode_time &&
                       a.duration == b.duration && a.composition_offset == b.composition_offset &&
                       a.sync == b.sync;
            }
            check(same, "segment " + std::to_string(segment) + " track " + std::to_string(track) +
                            ": same samples as the full scan");
        }
    }
    check(index->metadata().tracks.size() == 2 && index->metadata().tracks[0].samples.empty() &&
              index->metadata().tracks[0].edits.size() == full.tracks[0].edits.size() &&
              (full.tracks[0].edits.empty() || index->metadata().tracks[0].edits[0].media_time ==
                                                   full.tracks[0].edits[0].media_time),
          "metadata carries the full scan's video edit list, no samples");

    // Served bytes: the indexed remux and the full-scan remux of the file
    // without Cues produce the same playlist and segment bytes, offsets aside.
    std::string indexed_entry;
    std::string full_entry;
    const auto indexed_texts = remux_texts(file, indexed_entry);
    const auto full_texts = remux_texts(plain, full_entry);
    check(indexed_texts.at("index.m3u8") == full_texts.at("index.m3u8"), "same media playlist");
    check(indexed_texts.at("init.mp4") == full_texts.at("init.mp4"), "same init segment");
    for (const auto* name : {"s0.m4s", "s1.m4s", "s2.m4s"}) {
        const auto& a = indexed_texts.at(name);
        const auto& b = full_texts.at(name);
        // trun data offsets are relative to the moof, so the bytes match exactly.
        check(a == b, std::string(name) + ": same bytes as the full scan");
    }
}

void indexed_fallback_tests() {
    group = "indexed MKV fallbacks";
    // Cue points 25 s apart: a segment would exceed 20 s, so the full scan plans.
    std::vector<ClusterSpec> sparse(2);
    sparse[0].start_ms = 0;
    sparse[0].video = {{0, true}};
    sparse[0].audio_span_ms = 64;
    sparse[1].start_ms = 25'000;
    sparse[1].video = {{0, true}};
    sparse[1].audio_span_ms = 64;
    const auto file = indexed_mkv(sparse);
    check(MkvIndex::open(memory_reader(file), file.size()) == nullptr,
          "cue points more than 20 s apart fall back to the full scan");
}

void indexed_open_gop_tests() {
    group = "indexed MKV open GOP";
    // The second keyframe (at 6 s) is followed in decode order by two frames
    // presented before it (5.866 and 5.933 s): open-GOP leading frames.
    std::vector<ClusterSpec> specs(2);
    specs[0].start_ms = 0;
    specs[0].video = {{0, true}};
    for (std::int16_t frame = 1; frame < 88; ++frame) {
        specs[0].video.push_back({static_cast<std::int16_t>(frame * 67), false});
    }
    specs[0].audio_span_ms = 5900;
    specs[1].start_ms = 6000;
    specs[1].video = {{0, true}, {-134, false}, {-67, false}, {67, false}, {134, false}};
    specs[1].audio_span_ms = 256;
    const auto file = indexed_mkv(specs);
    const auto index = MkvIndex::open(memory_reader(file), file.size());
    check(index != nullptr && index->segment_count() == 2, "two segments");
    if (!index || index->segment_count() != 2) {
        return;
    }
    const auto first = index->read_segment(0, memory_reader(file));
    const auto second = index->read_segment(1, memory_reader(file));
    const auto& end_of_first = first.samples[0].back();
    check(end_of_first.decode_time + end_of_first.duration == second.samples[0].front().decode_time,
          "video decode times join at the cut");
    check(second.samples[0].front().decode_time == 6000, "the segment decodes from its cue time");
    // Presentation times stay the Matroska timestamps: decode + offset - delay.
    const auto delay = index->metadata().tracks[0].edits.empty()
                           ? 0
                           : index->metadata().tracks[0].edits.front().media_time;
    const auto presented = [&](const Mp4Sample& sample) {
        return static_cast<std::int64_t>(sample.decode_time) + sample.composition_offset - delay;
    };
    check(presented(second.samples[0][0]) == 6000 && presented(second.samples[0][1]) == 5866 &&
              presented(second.samples[0][2]) == 5933,
          "keyframe and leading frames keep their presentation times");
    const auto& last_audio = first.samples[1].back();
    check(last_audio.decode_time + last_audio.duration == second.samples[1].front().decode_time,
          "audio decode times join at the cut");
}

void indexed_review_tests() {
    group = "indexed MKV: selective indexes and subtitles";
    const auto specs = regular_clusters();
    // The index starts at the second cluster (3 s): segment 0 would drop the
    // first 3 s, so the full scan reads the file.
    const auto late = indexed_mkv(specs, true, 1);
    check(MkvIndex::open(memory_reader(late), late.size()) == nullptr,
          "an index starting after the first video frame falls back");

    // One cue at 0 s, then 25 s of video in a cluster that starts with a
    // non-keyframe (so it gets no cue): the last segment would last 25 s.
    std::vector<ClusterSpec> tail(2);
    tail[0].start_ms = 0;
    tail[0].video = {{0, true}};
    tail[0].audio_span_ms = 64;
    tail[1].start_ms = 25'000;
    tail[1].video = {{0, false}};
    tail[1].audio_span_ms = 64;
    const auto long_tail = indexed_mkv(tail);
    check(MkvIndex::open(memory_reader(long_tail), long_tail.size()) == nullptr,
          "a final segment over 20 s falls back");

    // Subtitles: a 14 s cue from 1 s spans the cuts at 6.5 and 12.5 s; a cue
    // without a duration at 6 s ends at its successor at 7 s, across a cut.
    auto with_text = specs;
    with_text[0].text = {{1000, "Long", 14'000}};
    with_text[1].text = {{3000, "Until next", 0}};
    with_text[2].text = {{500, "Next", 1000}};
    const auto file = indexed_mkv(with_text);
    check(MkvIndex::open(memory_reader(file), file.size()) != nullptr, "indexed with subtitles");
    std::string entry;
    const auto texts = remux_texts(file, entry);
    for (const auto* name : {"t0s0.vtt", "t0s1.vtt", "t0s2.vtt"}) {
        check(texts.at(name).find("00:00:01.000 --> 00:00:15.000\nLong") != std::string::npos,
              std::string(name) + ": the long cue is in every window it overlaps");
    }
    check(texts.at("t0s0.vtt").find("00:00:06.000 --> 00:00:07.000\nUntil next") !=
              std::string::npos,
          "a cue without a duration ends at its successor in the next segment");
}

int main() {
    try {
        sample_entry_tests();
        demux_tests();
        selection_and_refusal_tests();
        presentation_tests();
        subtitle_tests();
        indexed_reading_tests();
        indexed_fallback_tests();
        indexed_open_gop_tests();
        indexed_review_tests();
    } catch (const std::exception& error) {
        std::cerr << "FAIL [" << group << "]: test infrastructure exception: " << error.what()
                  << '\n';
        return 1;
    }
    return failures ? 1 : 0;
}
