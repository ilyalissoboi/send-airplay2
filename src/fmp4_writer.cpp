// SPDX-License-Identifier: Apache-2.0
// Fragmented MP4 (ISO/IEC 14496-12 8.8) for the HLS remux (D60). Written from
// the specification; no third-party code.
#include "fmp4_writer.h"
#include "box_writer.h"
#include <limits>
#include <stdexcept>
#include <utility>

namespace send_airplay2::detail {
namespace {
constexpr std::size_t box_header_bytes = 8;
constexpr std::size_t large_box_header_bytes = 16;
constexpr std::uint32_t tfhd_default_base_is_moof = 0x020000;
constexpr std::uint32_t tfhd_default_sample_flags = 0x000020;
constexpr std::uint32_t trun_data_offset = 0x000001;
constexpr std::uint32_t trun_sample_duration = 0x000100;
constexpr std::uint32_t trun_sample_size = 0x000200;
constexpr std::uint32_t trun_sample_flags = 0x000400;
constexpr std::uint32_t trun_composition_offset = 0x000800;
// sample_flags (14496-12 8.8.3.1): sample_depends_on in bits 25-24 and
// sample_is_non_sync_sample in bit 16.
constexpr std::uint32_t sync_sample_flags = 0x02000000;     // Depends on no other sample.
constexpr std::uint32_t non_sync_sample_flags = 0x01010000; // Depends on others; not sync.
constexpr std::uint32_t data_reference_self_contained = 0x000001;
constexpr std::uint32_t video_media_header_flags = 0x000001; // vmhd flags are always 1.

// Fixed box sizes, used to size a moof before writing it.
constexpr std::uint64_t mfhd_size = box_header_bytes + 4 + 4;          // Full box + sequence.
constexpr std::uint64_t tfdt_size = box_header_bytes + 4 + 8;          // Version 1, 64-bit time.
constexpr std::uint64_t tfhd_base_size = box_header_bytes + 4 + 4;     // Full box + track ID.
constexpr std::uint64_t trun_base_size = box_header_bytes + 4 + 4 + 4; // + count + data offset.

void movie_header(BoxWriter& out, const Mp4Movie& movie) {
    const auto mvhd = out.open_full("mvhd", 0, 0);
    out.u32(0); // Creation time.
    out.u32(0); // Modification time.
    out.u32(movie.timescale);
    out.u32(0);               // Duration: the segments carry the media.
    out.u32(fixed_point_one); // Rate 1.0.
    out.u16(full_volume);
    out.zeros(2 + 8); // Reserved.
    out.identity_matrix();
    out.zeros(6 * 4);                                             // pre_defined.
    out.u32(static_cast<std::uint32_t>(movie.tracks.size() + 1)); // next_track_ID.
    out.close(mvhd);
}

/// The source tkhd with this file's track ID and a zero duration; every other
/// field (flags, layer, volume, matrix, width, height) is kept.
void track_header(BoxWriter& out, const Mp4Track& track, std::uint32_t track_id) {
    const auto& source = track.track_header;
    const std::size_t header = source[0] == 0 && source[1] == 0 && source[2] == 0 && source[3] == 1
                                   ? large_box_header_bytes
                                   : box_header_bytes;
    Bytes payload(source.begin() + static_cast<std::ptrdiff_t>(header), source.end());
    const bool version_1 = payload.at(0) == 1;
    const std::size_t times = version_1 ? 16 : 8;          // Creation and modification.
    const std::size_t id_offset = 4 + times;               // After version/flags.
    const std::size_t duration_offset = id_offset + 4 + 4; // After track ID and reserved.
    const std::size_t duration_bytes = version_1 ? 8 : 4;
    if (payload.size() < duration_offset + duration_bytes) {
        throw std::length_error("tkhd shorter than its fields");
    }
    for (int index = 0; index < 4; ++index) {
        payload[id_offset + static_cast<std::size_t>(index)] =
            static_cast<std::uint8_t>(track_id >> (24 - 8 * index));
    }
    for (std::size_t index = 0; index < duration_bytes; ++index) {
        payload[duration_offset + index] = 0;
    }
    const auto tkhd = out.open("tkhd");
    out.bytes(payload);
    out.close(tkhd);
}

void edit_list(BoxWriter& out, const Mp4Track& track) {
    if (track.edits.empty()) {
        return;
    }
    bool wide = false;
    for (const auto& edit : track.edits) {
        wide = wide || edit.segment_duration > std::numeric_limits<std::uint32_t>::max() ||
               edit.media_time > std::numeric_limits<std::int32_t>::max() ||
               edit.media_time < std::numeric_limits<std::int32_t>::min();
    }
    const auto edts = out.open("edts");
    const auto elst = out.open_full("elst", wide ? 1 : 0, 0);
    out.u32(static_cast<std::uint32_t>(track.edits.size()));
    for (std::size_t index = 0; index < track.edits.size(); ++index) {
        const auto& edit = track.edits[index];
        // In a fragmented file the last media edit lasts to the end of the
        // segments that follow; zero says so without knowing their total.
        const bool last_media_edit = index + 1 == track.edits.size() && edit.media_time != -1;
        const auto duration = last_media_edit ? 0 : edit.segment_duration;
        if (wide) {
            out.u64(duration);
            out.u64(static_cast<std::uint64_t>(edit.media_time));
        } else {
            out.u32(static_cast<std::uint32_t>(duration));
            out.u32(static_cast<std::uint32_t>(static_cast<std::int32_t>(edit.media_time)));
        }
        out.u16(static_cast<std::uint16_t>(edit.rate_integer));
        out.u16(static_cast<std::uint16_t>(edit.rate_fraction));
    }
    out.close(elst);
    out.close(edts);
}

void empty_sample_tables(BoxWriter& out, const Mp4Track& track) {
    const auto stbl = out.open("stbl");
    const auto stsd = out.open_full("stsd", 0, 0);
    out.u32(1);
    out.bytes(track.sample_entry);
    out.close(stsd);
    for (const auto* type : {"stts", "stsc", "stco"}) {
        const auto box = out.open_full(type, 0, 0);
        out.u32(0); // entry_count.
        out.close(box);
    }
    const auto stsz = out.open_full("stsz", 0, 0);
    out.u32(0); // sample_size.
    out.u32(0); // sample_count.
    out.close(stsz);
    out.close(stbl);
}

void media(BoxWriter& out, const Mp4Track& track) {
    const bool video = track.kind == TrackKind::video;
    const auto mdia = out.open("mdia");
    const auto mdhd = out.open_full("mdhd", 0, 0);
    out.u32(0); // Creation time.
    out.u32(0); // Modification time.
    out.u32(track.timescale);
    out.u32(0); // Duration.
    out.u16(track.language);
    out.u16(0); // pre_defined.
    out.close(mdhd);
    const auto hdlr = out.open_full("hdlr", 0, 0);
    out.u32(0); // pre_defined.
    out.bytes(reinterpret_cast<const std::uint8_t*>(video ? "vide" : "soun"), 4);
    out.zeros(3 * 4); // Reserved.
    out.text(video ? "VideoHandler" : "SoundHandler");
    out.close(hdlr);
    const auto minf = out.open("minf");
    if (video) {
        const auto vmhd = out.open_full("vmhd", 0, video_media_header_flags);
        out.zeros(2 + 3 * 2); // graphicsmode and opcolor.
        out.close(vmhd);
    } else {
        const auto smhd = out.open_full("smhd", 0, 0);
        out.zeros(2 + 2); // balance and reserved.
        out.close(smhd);
    }
    const auto dinf = out.open("dinf");
    const auto dref = out.open_full("dref", 0, 0);
    out.u32(1);
    const auto url = out.open_full("url ", 0, data_reference_self_contained);
    out.close(url);
    out.close(dref);
    out.close(dinf);
    empty_sample_tables(out, track);
    out.close(minf);
    out.close(mdia);
}

bool has_negative_offset(const Mp4Track& track, const SampleRange& range) {
    for (auto index = range.first; index < range.end; ++index) {
        if (track.samples[index].composition_offset < 0) {
            return true;
        }
    }
    return false;
}

/// Bytes per trun sample: duration and size, plus flags and composition
/// offset for video.
std::uint64_t trun_sample_bytes(const Mp4Track& track) {
    return track.kind == TrackKind::video ? 16 : 8;
}

std::uint64_t traf_size(const Mp4Track& track, const SampleRange& range) {
    const std::uint64_t tfhd = tfhd_base_size + (track.kind == TrackKind::audio ? 4 : 0);
    const std::uint64_t trun = trun_base_size + range.count() * trun_sample_bytes(track);
    return box_header_bytes + tfhd + tfdt_size + trun;
}

void check_layout(const Mp4Movie& movie, const SegmentLayout& segment) {
    if (segment.ranges.size() != movie.tracks.size()) {
        throw std::invalid_argument("segment layout does not match the movie's tracks");
    }
    for (std::size_t index = 0; index < segment.ranges.size(); ++index) {
        const auto& range = segment.ranges[index];
        if (range.first > range.end || range.end > movie.tracks[index].samples.size()) {
            throw std::invalid_argument("segment range outside its track");
        }
    }
}
} // namespace

Bytes write_init_segment(const Mp4Movie& movie) {
    BoxWriter out;
    const auto ftyp = out.open("ftyp");
    out.bytes(reinterpret_cast<const std::uint8_t*>("iso5"), 4); // default-base-is-moof.
    out.u32(0);                                                  // Minor version.
    out.bytes(reinterpret_cast<const std::uint8_t*>("iso5iso6mp41"), 12);
    out.close(ftyp);
    const auto moov = out.open("moov");
    movie_header(out, movie);
    for (std::size_t index = 0; index < movie.tracks.size(); ++index) {
        const auto& track = movie.tracks[index];
        const auto trak = out.open("trak");
        track_header(out, track, static_cast<std::uint32_t>(index + 1));
        edit_list(out, track);
        media(out, track);
        out.close(trak);
    }
    const auto mvex = out.open("mvex");
    for (std::size_t index = 0; index < movie.tracks.size(); ++index) {
        const auto trex = out.open_full("trex", 0, 0);
        out.u32(static_cast<std::uint32_t>(index + 1)); // track_ID.
        out.u32(1);                                     // default_sample_description_index.
        out.u32(0);                                     // default_sample_duration.
        out.u32(0);                                     // default_sample_size.
        out.u32(0);                                     // default_sample_flags.
        out.close(trex);
    }
    out.close(mvex);
    out.close(moov);
    return out.take();
}

std::uint64_t moof_size(const Mp4Movie& movie, const SegmentLayout& segment) {
    check_layout(movie, segment);
    std::uint64_t size = box_header_bytes + mfhd_size;
    for (std::size_t index = 0; index < segment.ranges.size(); ++index) {
        if (segment.ranges[index].count()) {
            size += traf_size(movie.tracks[index], segment.ranges[index]);
        }
    }
    return size;
}

std::uint64_t mdat_payload_size(const Mp4Movie& movie, const SegmentLayout& segment) {
    check_layout(movie, segment);
    std::uint64_t size = 0;
    for (std::size_t index = 0; index < segment.ranges.size(); ++index) {
        const auto& samples = movie.tracks[index].samples;
        for (auto sample = segment.ranges[index].first; sample < segment.ranges[index].end;
             ++sample) {
            size += samples[sample].size;
        }
    }
    return size;
}

std::uint64_t mdat_header_size(const Mp4Movie& movie, const SegmentLayout& segment) {
    return mdat_payload_size(movie, segment) + box_header_bytes >
                   std::numeric_limits<std::uint32_t>::max()
               ? large_box_header_bytes
               : box_header_bytes;
}

Bytes write_segment_header(const Mp4Movie& movie, const SegmentLayout& segment) {
    const auto moof_bytes = moof_size(movie, segment);
    const auto mdat_header = mdat_header_size(movie, segment);
    BoxWriter out;
    const auto moof = out.open("moof");
    const auto mfhd = out.open_full("mfhd", 0, 0);
    out.u32(segment.sequence);
    out.close(mfhd);
    // Offset of each track's first sample from the start of the moof.
    std::uint64_t data_offset = moof_bytes + mdat_header;
    for (std::size_t index = 0; index < segment.ranges.size(); ++index) {
        const auto& range = segment.ranges[index];
        const auto& track = movie.tracks[index];
        if (!range.count()) {
            continue;
        }
        if (data_offset > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
            throw std::length_error("fMP4 segment larger than its 32-bit data offsets");
        }
        const bool video = track.kind == TrackKind::video;
        const auto traf = out.open("traf");
        const auto tfhd = out.open_full(
            "tfhd", 0, tfhd_default_base_is_moof | (video ? 0 : tfhd_default_sample_flags));
        out.u32(static_cast<std::uint32_t>(index + 1));
        if (!video) {
            out.u32(sync_sample_flags);
        }
        out.close(tfhd);
        const auto tfdt = out.open_full("tfdt", 1, 0);
        out.u64(track.samples[range.first].decode_time);
        out.close(tfdt);
        const auto flags = trun_data_offset | trun_sample_duration | trun_sample_size |
                           (video ? trun_sample_flags | trun_composition_offset : 0);
        const auto trun =
            out.open_full("trun", video && has_negative_offset(track, range) ? 1 : 0, flags);
        out.u32(static_cast<std::uint32_t>(range.count()));
        out.u32(static_cast<std::uint32_t>(data_offset));
        for (auto sample = range.first; sample < range.end; ++sample) {
            const auto& entry = track.samples[sample];
            out.u32(entry.duration);
            out.u32(entry.size);
            if (video) {
                out.u32(entry.sync ? sync_sample_flags : non_sync_sample_flags);
                out.u32(static_cast<std::uint32_t>(entry.composition_offset));
            }
            data_offset += entry.size;
        }
        out.close(trun);
        out.close(traf);
    }
    out.close(moof);
    const auto payload = mdat_payload_size(movie, segment);
    if (mdat_header == large_box_header_bytes) {
        out.u32(1);
        out.bytes(reinterpret_cast<const std::uint8_t*>("mdat"), 4);
        out.u64(payload + large_box_header_bytes);
    } else {
        out.u32(static_cast<std::uint32_t>(payload + box_header_bytes));
        out.bytes(reinterpret_cast<const std::uint8_t*>("mdat"), 4);
    }
    return out.take();
}
} // namespace send_airplay2::detail
