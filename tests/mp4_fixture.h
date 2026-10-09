// SPDX-License-Identifier: Apache-2.0
// A small synthetic MP4 for remux tests, built from literal boxes by code
// independent of the remux. No real media.
#ifndef SEND_AIRPLAY2_TESTS_MP4_FIXTURE_H
#define SEND_AIRPLAY2_TESTS_MP4_FIXTURE_H
#include "mp4_demux.h"
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace send_airplay2::test::mp4_fixture {
using send_airplay2::detail::Bytes;

// --- An independent box builder for test input ---------------------------

inline void put32(Bytes& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}
inline void put64(Bytes& out, std::uint64_t value) {
    put32(out, static_cast<std::uint32_t>(value >> 32));
    put32(out, static_cast<std::uint32_t>(value));
}
inline void put16(Bytes& out, std::uint16_t value) {
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value));
}
inline void put_type(Bytes& out, const char* type) {
    out.insert(out.end(), type, type + 4);
}
inline Bytes box(const char* type, const Bytes& payload) {
    Bytes out;
    put32(out, static_cast<std::uint32_t>(payload.size() + 8));
    put_type(out, type);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}
inline Bytes full_box(const char* type, std::uint8_t version, std::uint32_t flags, Bytes payload) {
    Bytes fields;
    put32(fields, (static_cast<std::uint32_t>(version) << 24) | flags);
    fields.insert(fields.end(), payload.begin(), payload.end());
    return box(type, fields);
}
inline Bytes join(std::initializer_list<Bytes> parts) {
    Bytes out;
    for (const auto& part : parts) {
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}
inline Bytes words(std::initializer_list<std::uint32_t> values) {
    Bytes out;
    for (const auto value : values) {
        put32(out, value);
    }
    return out;
}

/// Media byte at an absolute file offset, so sample contents are known
/// without asking the implementation where samples are.
inline std::uint8_t pattern(std::uint64_t offset) {
    return static_cast<std::uint8_t>(offset % 251);
}

// Fixture layout (byte offsets in the file):
//   0   ftyp (16 bytes)
//   16  mdat header (8 bytes); payload [24, 196)
//   24  video chunk 1: samples 0-2, sizes 10, 11, 12
//   57  audio chunks 1-4: four samples of 4 bytes each, at 57, 73, 89, 105
//   121 video chunk 2: samples 3-7, sizes 13..17, at 121, 134, 148, 163, 179
//   196 moov
inline constexpr std::uint64_t mdat_payload_begin = 24;
inline constexpr std::uint64_t mdat_payload_end = 196;
inline constexpr std::uint32_t video_timescale = 1000; // 1-second samples of 1000 ticks.
inline constexpr std::uint32_t audio_timescale = 100;  // 0.5-second samples of 50 ticks.
inline constexpr std::uint32_t movie_timescale = 600;
inline constexpr std::uint32_t video_samples = 8;
inline constexpr std::uint32_t audio_samples = 16;
inline const std::vector<std::uint64_t> video_offsets{24, 34, 45, 121, 134, 148, 163, 179};
inline const std::vector<std::uint64_t> audio_chunk_offsets{57, 73, 89, 105};
inline const std::vector<std::int32_t> video_composition{0, 2000, 1000, 0, 2000, 1000, 0, 1000};

// Fake sample entry payloads: the remux copies sample entries verbatim, so
// their contents only need to be recognizable.
inline Bytes avc1_entry(const char* codec = "avc1") {
    Bytes fields(78, 0); // VisualSampleEntry fields after the box header.
    fields[7] = 1;       // data_reference_index.
    return box(codec, join({fields, box("avcC", {1, 0x64, 0, 0x1f, 0xff})}));
}
inline Bytes esds(std::uint8_t object_type) {
    // ES_Descriptor (tag 3) with a DecoderConfigDescriptor (tag 4) whose
    // objectTypeIndication is `object_type`.
    return full_box("esds", 0, 0,
                    {0x03, 0x13, 0x00, 0x01, 0x00, 0x04, 0x0b, object_type, 0x15, 0,    0,   0,
                     0,    0,    0,    0,    0,    0,    0,    0x05,        0x02, 0x12, 0x10});
}
inline Bytes audio_entry(const char* codec, const Bytes& children) {
    Bytes fields(28, 0); // AudioSampleEntry fields after the box header.
    fields[7] = 1;       // data_reference_index.
    fields[17] = 2;      // channelcount.
    fields[19] = 16;     // samplesize.
    return box(codec, join({fields, children}));
}

struct TrackSpec {
    const char* handler = "vide";
    Bytes entry;
    std::uint32_t timescale = video_timescale;
    Bytes stbl_extra; // Tables other than stsd.
    std::uint32_t tkhd_flags = 3;
    std::uint32_t stsd_entries = 1;
    Bytes edts;
};

inline Bytes track(const TrackSpec& spec, std::uint32_t track_id) {
    Bytes tkhd_fields = words({0, 0, track_id, 0, 0}); // Times, ID, reserved, duration.
    tkhd_fields.resize(tkhd_fields.size() + 60, 0);    // Layer..height.
    Bytes stsd_fields = words({spec.stsd_entries});
    for (std::uint32_t entry = 0; entry < spec.stsd_entries; ++entry) {
        stsd_fields.insert(stsd_fields.end(), spec.entry.begin(), spec.entry.end());
    }
    Bytes mdhd_fields = words({0, 0, spec.timescale, 0});
    put16(mdhd_fields, 0x55c4); // "und".
    put16(mdhd_fields, 0);
    Bytes hdlr_fields = words({0});
    put_type(hdlr_fields, spec.handler);
    hdlr_fields.resize(hdlr_fields.size() + 13, 0);
    return box(
        "trak",
        join({full_box("tkhd", 0, spec.tkhd_flags, tkhd_fields), spec.edts,
              box("mdia",
                  join({full_box("mdhd", 0, 0, mdhd_fields), full_box("hdlr", 0, 0, hdlr_fields),
                        box("minf", box("stbl", join({full_box("stsd", 0, 0, stsd_fields),
                                                      spec.stbl_extra})))}))}));
}

inline Bytes video_tables(bool negative_offset = false) {
    Bytes ctts_fields = words({video_samples});
    for (std::size_t index = 0; index < video_samples; ++index) {
        const auto offset = negative_offset && index == 7 ? -500 : video_composition[index];
        put32(ctts_fields, 1);
        put32(ctts_fields, static_cast<std::uint32_t>(offset));
    }
    Bytes stsz_fields = words({0, video_samples});
    for (std::uint32_t index = 0; index < video_samples; ++index) {
        put32(stsz_fields, 10 + index);
    }
    return join({full_box("stts", 0, 0, words({1, video_samples, 1000})),
                 full_box("ctts", negative_offset ? 1 : 0, 0, ctts_fields),
                 full_box("stss", 0, 0, words({3, 1, 4, 7})),
                 full_box("stsc", 0, 0, words({2, 1, 3, 1, 2, 5, 1})),
                 full_box("stsz", 0, 0, stsz_fields), full_box("stco", 0, 0, words({2, 24, 121}))});
}

inline Bytes audio_tables() {
    Bytes co64_fields = words({4});
    for (const auto offset : audio_chunk_offsets) {
        put64(co64_fields, offset);
    }
    return join({full_box("stts", 0, 0, words({1, audio_samples, 50})),
                 full_box("stsc", 0, 0, words({1, 1, 4, 1})),
                 full_box("stsz", 0, 0, words({4, audio_samples})),
                 full_box("co64", 0, 0, co64_fields)});
}

inline Bytes video_edit() {
    // One edit: 8 seconds of movie time starting at media time 0.
    return box("edts", full_box("elst", 0, 0, words({1, 8 * movie_timescale, 0, 0x00010000})));
}

inline TrackSpec video_spec() {
    TrackSpec spec;
    spec.entry = avc1_entry();
    spec.stbl_extra = video_tables();
    spec.edts = video_edit();
    return spec;
}
inline TrackSpec audio_spec(const Bytes& entry = audio_entry("mp4a", esds(0x40))) {
    TrackSpec spec;
    spec.handler = "soun";
    spec.entry = entry;
    spec.timescale = audio_timescale;
    spec.stbl_extra = audio_tables();
    return spec;
}

inline Bytes mp4_file(const std::vector<TrackSpec>& tracks, bool fragmented = false) {
    // 16 bytes: major brand and minor version, no compatible brands.
    Bytes file = box("ftyp", join({Bytes{'i', 's', 'o', 'm'}, words({0})}));
    Bytes payload;
    for (auto offset = mdat_payload_begin; offset < mdat_payload_end; ++offset) {
        payload.push_back(pattern(offset));
    }
    file = join({file, box("mdat", payload)});
    Bytes mvhd_fields = words({0, 0, movie_timescale, 0, 0x00010000});
    mvhd_fields.resize(mvhd_fields.size() + 76, 0);
    Bytes moov_payload = full_box("mvhd", 0, 0, mvhd_fields);
    for (std::size_t index = 0; index < tracks.size(); ++index) {
        moov_payload =
            join({moov_payload, track(tracks[index], static_cast<std::uint32_t>(index + 1))});
    }
    file = join({file, box("moov", moov_payload)});
    if (fragmented) {
        file = join({file, box("moof", full_box("mfhd", 0, 0, words({1})))});
    }
    return file;
}
} // namespace send_airplay2::test::mp4_fixture
#endif
