// SPDX-License-Identifier: Apache-2.0
// Reads ISO base media file format sample tables (ISO/IEC 14496-12) for the
// HLS remux (D60). Written from the specification; no third-party code.
#include "mp4_demux.h"
#include "text_tracks.h"
#include <algorithm>
#include <array>
#include <limits>
#include <optional>
#include <utility>

namespace send_airplay2::detail {
namespace {
constexpr std::uint64_t max_moov_bytes = 64ULL << 20;
constexpr std::size_t max_samples_per_track = 4'000'000;
constexpr std::size_t box_header_bytes = 8;        // 32-bit size + type.
constexpr std::size_t large_box_header_bytes = 16; // size 1 + type + 64-bit size.
constexpr std::size_t full_box_fields = 4;         // version (8 bits) + flags (24 bits).
constexpr std::uint8_t aac_object_type = 0x40;     // MPEG-4 Audio (ISO/IEC 14496-1 Table 5).

[[noreturn]] void malformed(const std::string& what) {
    throw RemuxException(RemuxFailure::malformed, "MP4: " + what);
}
[[noreturn]] void unsupported(const std::string& what) {
    throw RemuxException(RemuxFailure::unsupported, "MP4: " + what);
}

constexpr std::uint32_t fourcc(const char (&name)[5]) {
    return (static_cast<std::uint32_t>(static_cast<unsigned char>(name[0])) << 24) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(name[1])) << 16) |
           (static_cast<std::uint32_t>(static_cast<unsigned char>(name[2])) << 8) |
           static_cast<std::uint32_t>(static_cast<unsigned char>(name[3]));
}

std::string fourcc_text(std::uint32_t code) {
    std::string text(4, '?');
    for (int index = 0; index < 4; ++index) {
        const auto c = static_cast<char>((code >> (24 - 8 * index)) & 0xff);
        text[static_cast<std::size_t>(index)] = (c >= 0x20 && c < 0x7f) ? c : '?';
    }
    return text;
}

std::uint32_t load_u32(const std::uint8_t* data) {
    return (static_cast<std::uint32_t>(data[0]) << 24) |
           (static_cast<std::uint32_t>(data[1]) << 16) |
           (static_cast<std::uint32_t>(data[2]) << 8) | static_cast<std::uint32_t>(data[3]);
}
std::uint64_t load_u64(const std::uint8_t* data) {
    return (static_cast<std::uint64_t>(load_u32(data)) << 32) | load_u32(data + 4);
}

/// Bounds-checked big-endian reader over one box payload. Every read that
/// would pass the end of the payload is a malformed box.
class Cursor {
public:
    Cursor(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}
    std::uint8_t u8() {
        need(1);
        return data_[position_++];
    }
    std::uint16_t u16() {
        need(2);
        const auto value =
            static_cast<std::uint16_t>((data_[position_] << 8) | data_[position_ + 1]);
        position_ += 2;
        return value;
    }
    std::uint32_t u32() {
        need(4);
        const auto value = load_u32(data_ + position_);
        position_ += 4;
        return value;
    }
    std::uint64_t u64() {
        need(8);
        const auto value = load_u64(data_ + position_);
        position_ += 8;
        return value;
    }
    void skip(std::size_t count) {
        need(count);
        position_ += count;
    }
    /// Version byte of a full box; the 24 flag bits are skipped.
    std::uint8_t full_box_version() {
        const auto version = u8();
        skip(full_box_fields - 1);
        return version;
    }
    [[nodiscard]] std::size_t remaining() const {
        return size_ - position_;
    }
    /// An entry count whose entries (of `entry_bytes` each) must fit the rest.
    std::uint32_t entry_count(std::size_t entry_bytes) {
        const auto count = u32();
        if (count > remaining() / entry_bytes) {
            malformed("table entry count exceeds its box");
        }
        return count;
    }

private:
    void need(std::size_t count) const {
        if (count > size_ - position_) {
            malformed("box shorter than its fields");
        }
    }
    const std::uint8_t* data_;
    std::size_t size_;
    std::size_t position_ = 0;
};

/// One box inside an in-memory buffer: [begin, begin + size) with the payload
/// after `header` bytes.
struct Box {
    std::uint32_t type = 0;
    const std::uint8_t* begin = nullptr;
    std::size_t size = 0;
    std::size_t header = 0;
    [[nodiscard]] Cursor payload() const {
        return {begin + header, size - header};
    }
    [[nodiscard]] Bytes copy() const {
        return {begin, begin + size};
    }
};

/// The boxes laid end to end in [data, data + size). A size of zero extends a
/// box to the end of its parent.
std::vector<Box> children(const std::uint8_t* data, std::size_t size) {
    std::vector<Box> boxes;
    std::size_t position = 0;
    while (position < size) {
        if (size - position < box_header_bytes) {
            malformed("truncated box header");
        }
        Box box;
        box.begin = data + position;
        box.type = load_u32(data + position + 4);
        std::uint64_t box_size = load_u32(data + position);
        box.header = box_header_bytes;
        if (box_size == 1) {
            if (size - position < large_box_header_bytes) {
                malformed("truncated box header");
            }
            box_size = load_u64(data + position + box_header_bytes);
            box.header = large_box_header_bytes;
        } else if (box_size == 0) {
            box_size = size - position;
        }
        if (box_size < box.header || box_size > size - position) {
            malformed("box size outside its parent");
        }
        box.size = static_cast<std::size_t>(box_size);
        boxes.push_back(box);
        position += box.size;
    }
    return boxes;
}
std::vector<Box> children(const Box& parent) {
    return children(parent.begin + parent.header, parent.size - parent.header);
}
const Box* find(const std::vector<Box>& boxes, std::uint32_t type) {
    const auto found = std::find_if(boxes.begin(), boxes.end(),
                                    [type](const Box& box) { return box.type == type; });
    return found == boxes.end() ? nullptr : &*found;
}
const Box& require(const std::vector<Box>& boxes, std::uint32_t type) {
    const auto* box = find(boxes, type);
    if (!box) {
        malformed("missing " + fourcc_text(type) + " box");
    }
    return *box;
}

/// The location of the top-level moov, found by reading box headers only.
struct TopLevel {
    std::uint64_t moov_offset = 0;
    std::uint64_t moov_size = 0;
};

TopLevel scan_top_level(const RandomReader& read, std::uint64_t file_size) {
    std::optional<TopLevel> moov;
    std::uint64_t position = 0;
    while (position < file_size) {
        std::array<std::uint8_t, large_box_header_bytes> header{};
        if (file_size - position < box_header_bytes) {
            malformed("truncated top-level box");
        }
        read(position, header.data(), box_header_bytes);
        const auto type = load_u32(header.data() + 4);
        std::uint64_t size = load_u32(header.data());
        std::uint64_t header_bytes = box_header_bytes;
        if (size == 1) {
            if (file_size - position < large_box_header_bytes) {
                malformed("truncated top-level box");
            }
            read(position + box_header_bytes, header.data() + box_header_bytes, 8);
            size = load_u64(header.data() + box_header_bytes);
            header_bytes = large_box_header_bytes;
        } else if (size == 0) {
            size = file_size - position;
        }
        if (size < header_bytes || size > file_size - position) {
            malformed("top-level box extends past the end of the file");
        }
        if (type == fourcc("moof")) {
            unsupported("fragmented input is not remuxed");
        }
        if (type == fourcc("moov")) {
            if (moov) {
                malformed("more than one moov box");
            }
            moov = TopLevel{position + header_bytes, size - header_bytes};
        }
        position += size;
    }
    if (!moov) {
        malformed("no moov box");
    }
    if (moov->moov_size > max_moov_bytes) {
        throw RemuxException(RemuxFailure::too_large, "MP4: moov box larger than 64 MiB");
    }
    return *moov;
}

/// Timescale from mvhd or mdhd, whose layouts agree up to that field.
std::uint32_t header_timescale(Cursor cursor, std::uint16_t* language = nullptr) {
    const auto version = cursor.full_box_version();
    cursor.skip(version == 1 ? 16 : 8); // Creation and modification times.
    const auto timescale = cursor.u32();
    cursor.skip(version == 1 ? 8 : 4); // Duration.
    if (language) {
        *language = cursor.u16();
    }
    if (timescale == 0) {
        malformed("zero timescale");
    }
    return timescale;
}

std::vector<Mp4Edit> edit_list(const std::vector<Box>& trak) {
    std::vector<Mp4Edit> edits;
    const auto* edts = find(trak, fourcc("edts"));
    if (!edts) {
        return edits;
    }
    // find() points into this vector, so it must outlive every use of elst.
    const auto edts_children = children(*edts);
    const auto* elst = find(edts_children, fourcc("elst"));
    if (!elst) {
        return edits;
    }
    auto cursor = elst->payload();
    const auto version = cursor.full_box_version();
    const auto count = cursor.entry_count(version == 1 ? 20 : 12);
    for (std::uint32_t index = 0; index < count; ++index) {
        Mp4Edit edit;
        if (version == 1) {
            edit.segment_duration = cursor.u64();
            edit.media_time = static_cast<std::int64_t>(cursor.u64());
        } else {
            edit.segment_duration = cursor.u32();
            edit.media_time = static_cast<std::int32_t>(cursor.u32());
        }
        edit.rate_integer = static_cast<std::int16_t>(cursor.u16());
        edit.rate_fraction = static_cast<std::int16_t>(cursor.u16());
        edits.push_back(edit);
    }
    return edits;
}

/// The length field of an MPEG-4 descriptor (ISO/IEC 14496-1 8.3.3): up to
/// four bytes of seven bits each, high bit set while more follow.
std::uint32_t descriptor_length(Cursor& cursor) {
    std::uint32_t length = 0;
    for (int index = 0; index < 4; ++index) {
        const auto byte = cursor.u8();
        length = (length << 7) | (byte & 0x7f);
        if (!(byte & 0x80)) {
            return length;
        }
    }
    malformed("descriptor length longer than four bytes");
}

/// objectTypeIndication of the esds DecoderConfigDescriptor (ISO/IEC 14496-1
/// 7.2.6.5-7.2.6.6).
std::uint8_t esds_object_type(const Box& esds) {
    constexpr std::uint8_t es_descriptor_tag = 0x03;
    constexpr std::uint8_t decoder_config_tag = 0x04;
    constexpr std::uint8_t stream_dependence_flag = 0x80;
    constexpr std::uint8_t url_flag = 0x40;
    constexpr std::uint8_t ocr_stream_flag = 0x20;
    auto cursor = esds.payload();
    cursor.full_box_version();
    if (cursor.u8() != es_descriptor_tag) {
        malformed("esds without an ES descriptor");
    }
    (void)descriptor_length(cursor);
    cursor.skip(2); // ES_ID.
    const auto flags = cursor.u8();
    if (flags & stream_dependence_flag) {
        cursor.skip(2);
    }
    if (flags & url_flag) {
        cursor.skip(cursor.u8());
    }
    if (flags & ocr_stream_flag) {
        cursor.skip(2);
    }
    if (cursor.u8() != decoder_config_tag) {
        malformed("esds without a decoder configuration");
    }
    (void)descriptor_length(cursor);
    return cursor.u8();
}

/// Child boxes of an audio sample entry, after its fixed fields. QuickTime
/// sound descriptions (versions 1 and 2) add 16 or 36 bytes of fields.
std::vector<Box> audio_entry_children(const Box& entry) {
    constexpr std::size_t base_fields = 28; // SampleEntry 8 + AudioSampleEntry 20.
    constexpr std::size_t version_offset = 8;
    auto cursor = entry.payload();
    cursor.skip(version_offset);
    const auto version = cursor.u16();
    const std::size_t fields = base_fields + (version == 1 ? 16 : version == 2 ? 36 : 0);
    if (entry.size - entry.header < fields) {
        malformed("audio sample entry shorter than its fields");
    }
    return children(entry.begin + entry.header + fields, entry.size - entry.header - fields);
}

bool aac_entry(const Box& entry) {
    auto boxes = audio_entry_children(entry);
    const auto* esds = find(boxes, fourcc("esds"));
    if (!esds) {
        if (const auto* wave = find(boxes, fourcc("wave"))) {
            boxes = children(*wave);
            esds = find(boxes, fourcc("esds"));
        }
    }
    return esds && esds_object_type(*esds) == aac_object_type;
}

/// The only stsd entry. More than one description would need per-sample
/// description switching, which the init segment cannot express here.
Box sample_entry(const Box& stsd) {
    auto cursor = stsd.payload();
    cursor.full_box_version();
    if (cursor.u32() != 1) {
        unsupported("more than one sample description in a track");
    }
    const auto entries = children(stsd.begin + stsd.header + 8, stsd.size - stsd.header - 8);
    if (entries.size() != 1) {
        malformed("sample description count disagrees with its entries");
    }
    return entries.front();
}

/// Builds the per-sample table from stsz, stts, ctts, stss, stsc and
/// stco/co64, checking that the tables agree and stay inside the file.
std::vector<Mp4Sample> sample_table(const std::vector<Box>& stbl, std::uint64_t file_size) {
    if (find(stbl, fourcc("stz2"))) {
        unsupported("compact sample sizes (stz2)");
    }
    auto sizes = require(stbl, fourcc("stsz")).payload();
    sizes.full_box_version();
    const auto fixed_size = sizes.u32();
    const auto count = sizes.u32();
    if (count > max_samples_per_track) {
        throw RemuxException(RemuxFailure::too_large, "MP4: more than 4000000 samples");
    }
    if (fixed_size == 0 && count > sizes.remaining() / 4) {
        malformed("sample size table shorter than its count");
    }
    std::vector<Mp4Sample> samples(count);
    for (auto& sample : samples) {
        sample.size = fixed_size ? fixed_size : sizes.u32();
        sample.sync = true;
    }

    auto times = require(stbl, fourcc("stts")).payload();
    times.full_box_version();
    std::size_t index = 0;
    std::uint64_t decode_time = 0;
    for (auto entries = times.entry_count(8); entries > 0; --entries) {
        const auto run = times.u32();
        const auto delta = times.u32();
        if (run > count - index) {
            malformed("time-to-sample table longer than the sample count");
        }
        for (std::uint32_t step = 0; step < run; ++step, ++index) {
            samples[index].decode_time = decode_time;
            samples[index].duration = delta;
            decode_time += delta;
        }
    }
    if (index != count) {
        malformed("time-to-sample table shorter than the sample count");
    }

    if (const auto* ctts = find(stbl, fourcc("ctts"))) {
        auto offsets = ctts->payload();
        offsets.full_box_version();
        index = 0;
        for (auto entries = offsets.entry_count(8); entries > 0; --entries) {
            const auto run = offsets.u32();
            // Version 0 is formally unsigned; writers store signed values in it.
            const auto offset = static_cast<std::int32_t>(offsets.u32());
            if (run > count - index) {
                malformed("composition offset table longer than the sample count");
            }
            for (std::uint32_t step = 0; step < run; ++step) {
                samples[index++].composition_offset = offset;
            }
        }
        if (index != count) {
            malformed("composition offset table shorter than the sample count");
        }
    }

    if (const auto* stss = find(stbl, fourcc("stss"))) {
        for (auto& sample : samples) {
            sample.sync = false;
        }
        auto sync = stss->payload();
        sync.full_box_version();
        for (auto entries = sync.entry_count(4); entries > 0; --entries) {
            const auto number = sync.u32(); // One-based.
            if (number == 0 || number > count) {
                malformed("sync sample number outside the track");
            }
            samples[number - 1].sync = true;
        }
    }

    std::vector<std::uint64_t> chunk_offsets;
    if (const auto* stco = find(stbl, fourcc("stco"))) {
        auto chunks = stco->payload();
        chunks.full_box_version();
        for (auto entries = chunks.entry_count(4); entries > 0; --entries) {
            chunk_offsets.push_back(chunks.u32());
        }
    } else {
        auto chunks = require(stbl, fourcc("co64")).payload();
        chunks.full_box_version();
        for (auto entries = chunks.entry_count(8); entries > 0; --entries) {
            chunk_offsets.push_back(chunks.u64());
        }
    }

    struct ChunkRun {
        std::uint32_t first_chunk; // One-based.
        std::uint32_t samples_per_chunk;
    };
    std::vector<ChunkRun> runs;
    auto stsc = require(stbl, fourcc("stsc")).payload();
    stsc.full_box_version();
    for (auto entries = stsc.entry_count(12); entries > 0; --entries) {
        const auto first_chunk = stsc.u32();
        const auto per_chunk = stsc.u32();
        stsc.skip(4); // sample_description_index: one description only.
        if (first_chunk == 0 || (!runs.empty() && first_chunk <= runs.back().first_chunk) ||
            (runs.empty() && first_chunk != 1)) {
            malformed("sample-to-chunk runs out of order");
        }
        runs.push_back({first_chunk, per_chunk});
    }
    index = 0;
    for (std::size_t run = 0; run < runs.size() && index < count; ++run) {
        const std::uint64_t end_chunk =
            run + 1 < runs.size() ? runs[run + 1].first_chunk : chunk_offsets.size() + 1ULL;
        for (std::uint64_t chunk = runs[run].first_chunk; chunk < end_chunk && index < count;
             ++chunk) {
            if (chunk > chunk_offsets.size()) {
                malformed("sample-to-chunk table names a missing chunk");
            }
            auto offset = chunk_offsets[static_cast<std::size_t>(chunk - 1)];
            for (std::uint32_t step = 0; step < runs[run].samples_per_chunk && index < count;
                 ++step, ++index) {
                auto& sample = samples[index];
                if (offset > file_size || sample.size > file_size - offset) {
                    malformed("sample outside the file");
                }
                sample.offset = offset;
                offset += sample.size;
            }
        }
    }
    if (index != count) {
        malformed("chunks hold fewer samples than the sample count");
    }
    return samples;
}

/// What one trak offers before its sample table is built.
struct TrackCandidate {
    TrackKind kind;
    bool enabled;
    Box sample_entry;
    std::string codec;
    bool remuxable;
};

std::optional<TrackCandidate> describe_track(const std::vector<Box>& trak) {
    constexpr std::uint32_t track_enabled_flag = 0x1;
    const auto& tkhd = require(trak, fourcc("tkhd"));
    auto header = tkhd.payload();
    header.skip(1);
    const auto flags = (static_cast<std::uint32_t>(header.u8()) << 16) | header.u16();
    const auto mdia = children(require(trak, fourcc("mdia")));
    auto handler = require(mdia, fourcc("hdlr")).payload();
    handler.full_box_version();
    handler.skip(4); // pre_defined (QuickTime component type).
    const auto handler_type = handler.u32();
    if (handler_type != fourcc("vide") && handler_type != fourcc("soun")) {
        return std::nullopt;
    }
    const auto minf = children(require(mdia, fourcc("minf")));
    const auto stbl = children(require(minf, fourcc("stbl")));
    const auto entry = sample_entry(require(stbl, fourcc("stsd")));
    TrackCandidate candidate{handler_type == fourcc("vide") ? TrackKind::video : TrackKind::audio,
                             (flags & track_enabled_flag) != 0, entry, fourcc_text(entry.type),
                             false};
    if (candidate.kind == TrackKind::video) {
        candidate.remuxable = remuxable_video_codec(candidate.codec);
    } else {
        candidate.remuxable = remuxable_audio_codec(candidate.codec) &&
                              (candidate.codec != "mp4a" || aac_entry(entry));
    }
    return candidate;
}

Mp4Track read_track(const std::vector<Box>& trak, const TrackCandidate& candidate,
                    std::uint64_t file_size) {
    Mp4Track track;
    track.kind = candidate.kind;
    track.codec = candidate.codec;
    track.sample_entry = candidate.sample_entry.copy();
    track.track_header = require(trak, fourcc("tkhd")).copy();
    track.edits = edit_list(trak);
    const auto mdia = children(require(trak, fourcc("mdia")));
    track.timescale = header_timescale(require(mdia, fourcc("mdhd")).payload(), &track.language);
    const auto minf = children(require(mdia, fourcc("minf")));
    track.samples = sample_table(children(require(minf, fourcc("stbl"))), file_size);
    if (track.samples.empty()) {
        malformed("selected track has no samples");
    }
    return track;
}
} // namespace

const char* remux_failure_name(RemuxFailure reason) noexcept {
    switch (reason) {
    case RemuxFailure::malformed:
        return "malformed input";
    case RemuxFailure::unsupported:
        return "not remuxable";
    case RemuxFailure::too_large:
        return "too large";
    }
    return "not remuxable";
}

namespace {
bool text_handler(const std::vector<Box>& trak) {
    const auto mdia = children(require(trak, fourcc("mdia")));
    auto handler = require(mdia, fourcc("hdlr")).payload();
    handler.full_box_version();
    handler.skip(4); // pre_defined.
    const auto type = handler.u32();
    return type == fourcc("sbtl") || type == fourcc("text") || type == fourcc("subt");
}
} // namespace

bool remuxable_video_codec(const std::string& codec) {
    return codec == "avc1" || codec == "avc3" || codec == "hvc1" || codec == "hev1";
}
bool remuxable_audio_codec(const std::string& codec) {
    return codec == "mp4a" || codec == "ac-3" || codec == "ec-3";
}

namespace {
// --- Text subtitle tracks (D60) ---------------------------------------------

constexpr std::size_t max_text_sample_bytes = 64 * 1024;
constexpr std::uint64_t max_text_bytes_per_track = 16ULL << 20;
// 3GPP TS 26.245 5.16: displayFlags bit for "all samples are forced".
constexpr std::uint32_t tx3g_all_samples_forced = 0x40000000;
constexpr std::size_t sample_entry_header_fields = 8; // Reserved 6 and data_reference_index.

/// "eng" from mdhd's packed ISO-639-2/T language (three 5-bit letters).
std::string unpacked_language(std::uint16_t packed) {
    std::string code(3, ' ');
    for (int index = 0; index < 3; ++index) {
        code[static_cast<std::size_t>(index)] =
            static_cast<char>(((packed >> (10 - 5 * index)) & 0x1F) + 0x60);
    }
    return code;
}

/// Bold, italic and underline runs of a tx3g sample's 'styl' modifier box
/// (TS 26.245 5.17.1.1): StyleRecords of startChar, endChar, font-ID,
/// face-style-flags (1 bold, 2 italic, 4 underline), font-size and colour.
/// Fonts, sizes and colours have no WebVTT cue-text form and are dropped.
std::vector<TextStyleRun> tx3g_styles(const Bytes& sample, std::size_t modifiers) {
    constexpr std::size_t style_record_bytes = 12;
    std::vector<TextStyleRun> runs;
    if (modifiers >= sample.size()) {
        return runs;
    }
    for (const auto& box : children(sample.data() + modifiers, sample.size() - modifiers)) {
        if (box.type != fourcc("styl")) {
            continue;
        }
        auto fields = box.payload();
        const auto count = fields.u16();
        for (std::uint16_t index = 0; index < count && fields.remaining() >= style_record_bytes;
             ++index) {
            TextStyleRun run;
            run.start = fields.u16();
            run.end = fields.u16();
            fields.skip(2); // font-ID.
            const auto face = fields.u8();
            fields.skip(1 + 4); // font-size, text-color-rgba.
            run.bold = (face & 0x01) != 0;
            run.italic = (face & 0x02) != 0;
            run.underline = (face & 0x04) != 0;
            if (run.bold || run.italic || run.underline) {
                runs.push_back(run);
            }
        }
    }
    return runs;
}

/// Cue text of one tx3g sample (TS 26.245 5.17): a 16-bit length, then UTF-8
/// or UTF-16 (byte-order mark FE FF) text, then modifier boxes, of which the
/// 'styl' bold/italic/underline runs are kept.
std::string tx3g_text(const Bytes& sample) {
    if (sample.size() < 2) {
        return {};
    }
    const std::size_t length = (static_cast<std::size_t>(sample[0]) << 8) | sample[1];
    const auto text_bytes = std::min(length, sample.size() - 2);
    const std::string_view text(reinterpret_cast<const char*>(sample.data()) + 2, text_bytes);
    std::vector<TextStyleRun> runs;
    try {
        runs = tx3g_styles(sample, 2 + text_bytes);
    } catch (const RemuxException&) {
        // A malformed modifier box loses the styling, not the text.
    }
    if (text.size() >= 2 && static_cast<unsigned char>(text[0]) == 0xFE &&
        static_cast<unsigned char>(text[1]) == 0xFF) {
        return plain_text_to_webvtt(utf16be_to_utf8(text.substr(2)), runs);
    }
    return plain_text_to_webvtt(text, runs);
}

/// Cue texts of one wvtt sample (ISO/IEC 14496-30 7): each 'vttc' box's
/// 'payl' payload, already WebVTT cue text; 'vtte' (no cue) gives none.
std::vector<std::string> wvtt_texts(const Bytes& sample) {
    std::vector<std::string> texts;
    for (const auto& cue : children(sample.data(), sample.size())) {
        if (cue.type != fourcc("vttc")) {
            continue;
        }
        // Named: find returns a pointer into this vector.
        const auto cue_boxes = children(cue);
        if (const auto* payload = find(cue_boxes, fourcc("payl"))) {
            const std::string_view text(
                reinterpret_cast<const char*>(payload->begin + payload->header),
                payload->size - payload->header);
            auto converted = webvtt_cue_text(text);
            if (!converted.empty()) {
                texts.push_back(std::move(converted));
            }
        }
    }
    return texts;
}

/// The text track of a trak with a tx3g or wvtt entry, or nothing for other
/// text formats or a track over the size limits. Samples wholly before the
/// first media edit are not presented and give no cue; one crossing it starts
/// at the edit. Throws RemuxException for a layout it cannot read.
std::optional<TextTrack> read_text_track(const std::vector<Box>& trak, const RandomReader& read,
                                         std::uint64_t file_size, std::uint32_t movie_timescale) {
    constexpr std::uint32_t track_enabled_flag = 0x1;
    auto header = require(trak, fourcc("tkhd")).payload();
    header.skip(1);
    const auto flags = (static_cast<std::uint32_t>(header.u8()) << 16) | header.u16();
    const auto mdia = children(require(trak, fourcc("mdia")));
    std::uint16_t packed_language = 0;
    const auto timescale =
        header_timescale(require(mdia, fourcc("mdhd")).payload(), &packed_language);
    const auto minf = children(require(mdia, fourcc("minf")));
    const auto stbl = children(require(minf, fourcc("stbl")));
    const auto entry = sample_entry(require(stbl, fourcc("stsd")));
    const bool tx3g = entry.type == fourcc("tx3g");
    if (!tx3g && entry.type != fourcc("wvtt")) {
        return std::nullopt;
    }
    TextTrack track;
    track.language = language_tag(unpacked_language(packed_language));
    track.default_track = (flags & track_enabled_flag) != 0;
    if (tx3g) {
        auto fields = entry.payload();
        fields.skip(sample_entry_header_fields);
        track.forced = (fields.u32() & tx3g_all_samples_forced) != 0;
    }
    // Presentation = media time - the media edit's start + any empty edit.
    std::int64_t media_start = 0;
    std::uint64_t empty_us = 0;
    for (const auto& edit : edit_list(trak)) {
        if (edit.media_time == -1) {
            empty_us += edit.segment_duration * 1'000'000 / movie_timescale;
        } else {
            media_start = edit.media_time;
            break;
        }
    }
    const auto to_us = [timescale, media_start, empty_us](std::int64_t ticks) -> std::uint64_t {
        const auto relative = ticks - media_start;
        return relative <= 0
                   ? empty_us
                   : empty_us + static_cast<std::uint64_t>(relative) / timescale * 1'000'000 +
                         static_cast<std::uint64_t>(relative) % timescale * 1'000'000 / timescale;
    };
    std::uint64_t total = 0;
    for (const auto& sample : sample_table(stbl, file_size)) {
        if (sample.size > max_text_sample_bytes) {
            return std::nullopt; // A partial rendition would silently miss cues.
        }
        if (sample.size <= 2) {
            continue; // Two bytes is an empty tx3g sample: a gap between cues.
        }
        const auto start =
            static_cast<std::int64_t>(sample.decode_time) + sample.composition_offset;
        if (start + static_cast<std::int64_t>(sample.duration) <= media_start) {
            continue; // Wholly before the media edit, so not presented.
        }
        total += sample.size;
        if (total > max_text_bytes_per_track) {
            return std::nullopt;
        }
        Bytes payload(sample.size);
        read(sample.offset, payload.data(), payload.size());
        const auto start_us = to_us(start);
        const auto end_us = std::max(to_us(start + sample.duration), start_us + 1000);
        const auto texts =
            tx3g ? std::vector<std::string>{tx3g_text(payload)} : wvtt_texts(payload);
        for (const auto& text : texts) {
            if (!text.empty()) {
                track.cues.push_back({start_us, end_us, text});
            }
        }
    }
    if (track.cues.empty()) {
        return std::nullopt;
    }
    std::stable_sort(track.cues.begin(), track.cues.end(),
                     [](const TextCue& a, const TextCue& b) { return a.start_us < b.start_us; });
    return track;
}
} // namespace

Mp4Movie read_mp4(const RandomReader& read, std::uint64_t file_size) {
    const auto location = scan_top_level(read, file_size);
    Bytes moov(static_cast<std::size_t>(location.moov_size));
    read(location.moov_offset, moov.data(), moov.size());
    const auto boxes = children(moov.data(), moov.size());
    if (find(boxes, fourcc("mvex"))) {
        unsupported("fragmented input is not remuxed");
    }
    Mp4Movie movie;
    movie.timescale = header_timescale(require(boxes, fourcc("mvhd")).payload());

    std::optional<std::pair<std::vector<Box>, TrackCandidate>> video;
    std::optional<std::pair<std::vector<Box>, TrackCandidate>> audio;
    bool saw_video = false;
    bool saw_audio = false;
    for (const auto& box : boxes) {
        if (box.type != fourcc("trak")) {
            continue;
        }
        auto trak = children(box);
        const auto candidate = describe_track(trak);
        if (!candidate || !candidate->enabled) {
            continue;
        }
        auto& slot = candidate->kind == TrackKind::video ? video : audio;
        (candidate->kind == TrackKind::video ? saw_video : saw_audio) = true;
        if (!slot && candidate->remuxable) {
            slot.emplace(std::move(trak), *candidate);
        }
    }
    if (!video) {
        unsupported(saw_video ? "video codec is not remuxable (H.264 or HEVC only)"
                              : "no video track");
    }
    if (saw_audio && !audio) {
        unsupported("audio codec is not remuxable (AAC, AC-3 or E-AC-3 only)");
    }
    movie.tracks.push_back(read_track(video->first, video->second, file_size));
    if (audio) {
        movie.tracks.push_back(read_track(audio->first, audio->second, file_size));
    }
    // Text subtitles are extras: any track that cannot be served is left out,
    // including a malformed or unsupported layout (several stsd entries, stz2).
    // Read failures and cancellation are not RemuxException and still propagate.
    for (const auto& box : boxes) {
        if (box.type != fourcc("trak")) {
            continue;
        }
        try {
            const auto trak = children(box);
            if (text_handler(trak)) {
                if (auto text = read_text_track(trak, read, file_size, movie.timescale)) {
                    movie.text_tracks.push_back(std::move(*text));
                }
            }
        } catch (const RemuxException&) {
            // Left out; the video and audio remux is unaffected.
        }
    }
    return movie;
}
} // namespace send_airplay2::detail
