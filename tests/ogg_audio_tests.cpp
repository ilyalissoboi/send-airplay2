// SPDX-License-Identifier: Apache-2.0
#include "ogg_audio.h"
#include "standalone_audio.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace send_airplay2;
using namespace send_airplay2::detail;
void expect(bool condition, const std::string& scenario) {
    if (!condition) {
        throw std::runtime_error(scenario);
    }
}
void set(Bytes& bytes, std::size_t offset, std::uint64_t value, std::size_t width) {
    for (std::size_t index = 0; index < width; ++index) {
        bytes.at(offset + index) = static_cast<std::uint8_t>(value >> (8 * index));
    }
}
// Independent bit-at-a-time CRC oracle, rather than the production lookup table.
void checksum(Bytes& page) {
    set(page, 22, 0, 4);
    std::uint32_t value = 0;
    for (auto byte : page) {
        value ^= std::uint32_t{byte} << 24;
        for (int bit = 0; bit < 8; ++bit) {
            const auto carry = value >> 31;
            value <<= 1;
            if (carry) {
                value ^= 0x04c11db7;
            }
        }
    }
    set(page, 22, value, 4);
}
Bytes page(const Bytes& body, std::uint32_t sequence, std::uint8_t flags, std::uint64_t granule,
           bool unfinished = false) {
    Bytes result(27);
    std::memcpy(result.data(), "OggS", 4);
    result[5] = flags;
    set(result, 6, granule, 8);
    set(result, 14, 0x12345678, 4);
    set(result, 18, sequence, 4);
    const auto whole_laces = body.size() / 255;
    expect(whole_laces + !unfinished <= 255, "fixture page lacing bound");
    result[26] = static_cast<std::uint8_t>(whole_laces + !unfinished);
    result.insert(result.end(), whole_laces, 255);
    if (!unfinished) {
        result.push_back(static_cast<std::uint8_t>(body.size() % 255));
    }
    result.insert(result.end(), body.begin(), body.end());
    checksum(result);
    return result;
}
Bytes opus_id() {
    Bytes id(19);
    std::memcpy(id.data(), "OpusHead", 8);
    id[8] = 1;
    id[9] = 2;
    set(id, 10, 312, 2);
    set(id, 12, 44100, 4); // Informational input rate must not change Opus's 48-kHz clock.
    return id;
}
Bytes opus_tags() {
    Bytes tags(16);
    std::memcpy(tags.data(), "OpusTags", 8);
    return tags;
}
using Pages = std::vector<Bytes>;
Pages opus() {
    // 20-ms TOC, then a final packet trimmed by 100 samples: 1820 - pre-skip 312 = 1508.
    return {page(opus_id(), 0, 2, 0), page(opus_tags(), 1, 0, 0), page(Bytes{0xf8, 0}, 2, 0, 960),
            page(Bytes{0xf8, 0}, 3, 4, 1820)};
}
Bytes join(const Pages& pages) {
    Bytes bytes;
    for (const auto& value : pages) {
        bytes.insert(bytes.end(), value.begin(), value.end());
    }
    return bytes;
}
struct Input {
    Bytes bytes;
    std::uint64_t size;
    std::size_t calls = 0, cancel_after = 0;
    std::atomic_bool stopped{false}, cancelled{false};
    MediaReadContext context{&stopped, &cancelled,
                             std::chrono::steady_clock::now() + std::chrono::seconds(30)};
    explicit Input(Bytes data) : bytes(std::move(data)), size(bytes.size()) {}
    MediaSource source() {
        return {[this] { return size; },
                [this](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
                       const MediaReadContext&) {
                    ++calls;
                    expect(capacity <= 65536 && offset <= bytes.size(), "Ogg read bounds");
                    const auto count = static_cast<std::size_t>(
                        std::min<std::uint64_t>({capacity, 7, bytes.size() - offset}));
                    std::memcpy(output, bytes.data() + offset, count);
                    if (cancel_after && calls >= cancel_after) {
                        cancelled = true;
                    }
                    return count;
                }};
    }
    MediaTrackInventory probe() {
        return probe_audio_only_tracks(source(), context);
    }
};
void refusal(const Pages& pages, const std::string& scenario, RemuxFailure reason) {
    Input input(join(pages));
    try {
        (void)input.probe();
        throw std::runtime_error(scenario + ": unexpectedly admitted");
    } catch (const RemuxException& error) {
        expect(error.reason() == reason, scenario + ": refusal category, actual=" +
                                             std::to_string(static_cast<int>(error.reason())));
    }
}
void page_integrity_and_stream_rules() {
    // Literal first page authored by the development Ogg muxer, serial rewritten
    // in the owned fixture generator. CRC 0x3eb0ec23 is not computed here.
    const Bytes known_page{0x4f, 0x67, 0x67, 0x53, 0,    2,    0,    0,    0,    0,    0,    0,
                           0,    0,    0x78, 0x56, 0x34, 0x12, 0,    0,    0,    0,    0x23, 0xec,
                           0xb0, 0x3e, 1,    0x13, 0x4f, 0x70, 0x75, 0x73, 0x48, 0x65, 0x61, 0x64,
                           1,    2,    0x38, 1,    0x80, 0xbb, 0,    0,    0,    0,    0};
    auto known_stream = opus();
    known_stream[0] = known_page;
    Input known_input(join(known_stream));
    expect(known_input.probe().tracks[0].standalone_sample_frames == 1508,
           "independent literal Ogg CRC known-answer page");
    Input input(join(opus()));
    const auto info = input.probe();
    expect(info.container == SourceContainer::ogg && info.tracks.size() == 1 &&
               info.tracks[0].id == 1 && info.tracks[0].timescale == 48000 &&
               info.tracks[0].standalone_sample_frames == 1508 &&
               info.tracks[0].audio_channel_mask == 3,
           "Opus literal pre-skip/end-trim duration, identity and clock");
    expect(select_source_audio(info, 99).status == AudioSelectionStatus::invalid_selection,
           "Ogg invalid selection never falls back");
    try {
        (void)probe_media_tracks(input.source(), input.context);
        throw std::logic_error("public inventory unexpectedly admits Ogg");
    } catch (const RemuxException&) {
    }
    struct Mutation {
        std::size_t page, offset;
        std::uint64_t value;
        std::size_t width;
        const char* scenario;
        RemuxFailure reason;
    };
    for (const auto& change :
         {Mutation{0, 4, 1, 1, "version", RemuxFailure::malformed},
          {0, 5, 0, 1, "missing BOS", RemuxFailure::unsupported},
          {2, 5, 2, 1, "second BOS", RemuxFailure::unsupported},
          {2, 5, 1, 1, "unexpected continuation", RemuxFailure::malformed},
          {2, 14, 42, 4, "second serial", RemuxFailure::unsupported},
          {2, 18, 3, 4, "sequence gap", RemuxFailure::malformed},
          {3, 18, 2, 4, "duplicate sequence", RemuxFailure::malformed},
          {2, 6, 961, 8, "nonzero Opus origin", RemuxFailure::unsupported},
          {3, 6, 959, 8, "granule regression", RemuxFailure::malformed},
          {3, 6, 960, 8, "trim beyond last packet", RemuxFailure::unsupported},
          {3, 6, 1921, 8, "end beyond available packets", RemuxFailure::unsupported},
          {3, 6, 0xffffffffffffffffULL, 8, "unknown final granule", RemuxFailure::malformed}}) {
        auto pages = opus();
        set(pages[change.page], change.offset, change.value, change.width);
        checksum(pages[change.page]);
        refusal(pages, change.scenario, change.reason);
    }
    auto missing_eos = opus();
    missing_eos.back() = page(Bytes{0xf8, 0}, 3, 0, 1920);
    refusal(missing_eos, "missing EOS", RemuxFailure::malformed);
    auto bad_crc = opus();
    bad_crc[2].back() ^= 1;
    refusal(bad_crc, "CRC corruption", RemuxFailure::malformed);
    auto chain = opus();
    const auto second = opus();
    chain.insert(chain.end(), second.begin(), second.end());
    refusal(chain, "chained stream", RemuxFailure::unsupported);
    auto truncated = opus();
    truncated.back().pop_back();
    refusal(truncated, "truncated physical page", RemuxFailure::malformed);
}
void codec_headers_and_continuation() {
    auto id = opus_id();
    id[18] = 255;
    auto invalid = opus();
    invalid[0] = page(id, 0, 2, 0);
    refusal(invalid, "unknown Opus mapping", RemuxFailure::unsupported);
    auto tags = opus_tags();
    set(tags, 8, 90000, 4);
    tags.insert(tags.begin() + 12, 90000, 'a');
    constexpr std::size_t first_page_payload = 255 * 255;
    const Bytes first(tags.begin(), tags.begin() + first_page_payload);
    const Bytes last(tags.begin() + first_page_payload, tags.end());
    Pages continued{page(opus_id(), 0, 2, 0),
                    page(first, 1, 0, std::numeric_limits<std::uint64_t>::max(), true),
                    page(last, 2, 1, 0), page(Bytes{0xf8, 0}, 3, 4, 960)};
    Input valid(join(continued));
    expect(valid.probe().tracks[0].standalone_sample_frames == 648,
           "continued comment reassembly and short-read input");
    continued[2][5] = 0;
    checksum(continued[2]);
    refusal(continued, "missing continuation", RemuxFailure::malformed);

    Bytes vorbis_id(30);
    vorbis_id[0] = 1;
    std::memcpy(vorbis_id.data() + 1, "vorbis", 6);
    vorbis_id[11] = 2;
    set(vorbis_id, 12, 44100, 4);
    vorbis_id[28] = 0xb8;
    vorbis_id[29] = 1;
    Bytes comment(16);
    comment[0] = 3;
    std::memcpy(comment.data() + 1, "vorbis", 6);
    comment.back() = 1;
    Bytes setup{5,   'v', 'o', 'r',
                'b', 'i', 's', 0}; // Metadata test only; decoder checks codebooks.
    Pages vorbis{page(vorbis_id, 0, 2, 0), page(comment, 1, 0, 0), page(setup, 2, 0, 0),
                 page(Bytes{0}, 3, 4, 132432)};
    Input valid_vorbis(join(vorbis));
    expect(valid_vorbis.probe().tracks[0].standalone_sample_frames == 132432 &&
               valid_vorbis.probe().tracks[0].timescale == 44100,
           "Vorbis final granule is source-rate sample count");
    set(vorbis_id, 12, 192000, 4);
    vorbis[0] = page(vorbis_id, 0, 2, 0);
    vorbis[3] = page(Bytes{0}, 3, 4, 7ULL * 24 * 3600 * 192000 - 1);
    refusal(vorbis, "duration rounds up to seven-day boundary", RemuxFailure::unsupported);
    vorbis_id[11] = 7;
    vorbis[0] = page(vorbis_id, 0, 2, 0);
    refusal(vorbis, "unsupported back-center layout", RemuxFailure::unsupported);
}
void read_limits_and_cancellation() {
    Input oversized(join(opus()));
    oversized.size = (64ULL << 20) + 1;
    try {
        (void)oversized.probe();
        throw std::logic_error("Ogg 64-MiB limit ignored");
    } catch (const RemuxException& error) {
        expect(error.reason() == RemuxFailure::too_large, "Ogg literal source bound category");
    }
    for (auto after : {std::size_t{0}, std::size_t{8}}) {
        Input input(join(opus()));
        input.cancelled = after == 0;
        input.cancel_after = after;
        try {
            (void)input.probe();
            throw std::logic_error("Ogg cancellation ignored");
        } catch (const std::runtime_error& error) {
            expect(std::string(error.what()).find("stopped") != std::string::npos,
                   "Ogg cancellation category at callback " + std::to_string(after));
        }
        expect(input.calls == after, "Ogg no callbacks after cancellation");
    }

    Pages oversized_packet{page(opus_id(), 0, 2, 0)};
    const Bytes fragment(255 * 255, 'a');
    for (std::uint32_t sequence = 1; sequence <= 17; ++sequence) {
        oversized_packet.push_back(page(fragment, sequence, sequence == 1 ? 0 : 1,
                                        std::numeric_limits<std::uint64_t>::max(), true));
    }
    refusal(oversized_packet, "literal 1-MiB packet bound", RemuxFailure::too_large);

    Pages excessive_pages{page(opus_id(), 0, 2, 0), page(opus_tags(), 1, 0, 0)};
    for (std::uint32_t sequence = 2; sequence <= 65536; ++sequence) {
        excessive_pages.push_back(page(Bytes{0xf8, 0}, sequence, 0, (sequence - 1) * 960ULL));
    }
    refusal(excessive_pages, "literal 65536-page bound", RemuxFailure::too_large);
}
} // namespace
int main() {
    try {
        page_integrity_and_stream_rules();
        codec_headers_and_continuation();
        read_limits_and_cancellation();
        std::cout << "Ogg page, codec inventory, granule, continuation and bounds checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Ogg audio failure: " << error.what() << '\n';
        return 1;
    }
}
