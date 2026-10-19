// SPDX-License-Identifier: Apache-2.0
#include "mp3_audio.h"
#include "mp4_demux.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {
using namespace send_airplay2;
using namespace send_airplay2::detail;
void expect(bool condition, const std::string& scenario) {
    if (!condition) {
        throw std::runtime_error(scenario);
    }
}
Bytes frame() {
    // Independent MPEG-1 layer-III 128 kbps / 44.1 kHz stereo frame: 417 bytes,
    // 1152 samples. Zero side info has no reservoir dependency; payload is not decoded.
    Bytes result(417);
    result[0] = 0xff;
    result[1] = 0xfb;
    result[2] = 0x90;
    return result;
}
Bytes stream(std::size_t frames = 3) {
    Bytes result;
    const auto value = frame();
    for (std::size_t index = 0; index < frames; ++index) {
        result.insert(result.end(), value.begin(), value.end());
    }
    return result;
}
struct Input {
    Bytes bytes;
    std::uint64_t declared_size;
    std::size_t calls = 0, cancel_after = 0;
    std::atomic_bool stopped{false}, cancelled{false};
    MediaReadContext context{&stopped, &cancelled,
                             std::chrono::steady_clock::now() + std::chrono::seconds(25)};
    explicit Input(Bytes data) : bytes(std::move(data)), declared_size(bytes.size()) {}
    explicit Input(const std::filesystem::path& path) : declared_size(0) {
        std::ifstream input(path, std::ios::binary);
        expect(input.good(), "missing owned MP3 fixture " + path.filename().string());
        bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        declared_size = bytes.size();
    }
    MediaSource source() {
        return {[this] { return declared_size; },
                [this](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
                       const MediaReadContext&) {
                    ++calls;
                    expect(capacity <= 65536 && offset <= bytes.size(), "MP3 read bounds");
                    const auto count = static_cast<std::size_t>(
                        std::min<std::uint64_t>({capacity, 7, bytes.size() - offset}));
                    std::memcpy(output, bytes.data() + offset, count);
                    if (cancel_after && calls >= cancel_after) {
                        cancelled = true;
                    }
                    return count;
                }};
    }
    Mp3AudioInventory probe() {
        return probe_mp3_audio(source(), context);
    }
};
void refusal(Bytes bytes, const std::string& scenario, RemuxFailure category) {
    Input input(std::move(bytes));
    try {
        (void)input.probe();
        throw std::runtime_error(scenario + ": unexpected admission");
    } catch (const RemuxException& error) {
        expect(error.reason() == category, scenario + ": incorrect refusal category");
    }
}
void literal_headers_and_refusals() {
    Input valid(stream());
    const auto info = valid.probe();
    expect(info.sample_rate == 44100 && info.channels == 2 && info.frame_count == 3 &&
               info.coded_samples == 3456 && info.audio_end == 1251 && !info.variable_bitrate &&
               !info.encoder_trim && !info.audible_samples &&
               info.seek_points.front().byte_offset == 0,
           "literal raw CBR frame count, extent and unknown trim");
    struct HeaderMutation {
        std::size_t offset;
        std::uint8_t value;
    };
    for (const auto& mutation :
         {HeaderMutation{1, 0xfa}, {1, 0xf9}, {1, 0xeb}, {2, 0}, {2, 0xf0}, {2, 0x9c}, {3, 2}}) {
        auto bytes = stream();
        bytes[mutation.offset] = mutation.value;
        refusal(std::move(bytes),
                "unsupported header field offset " + std::to_string(mutation.offset) + " value " +
                    std::to_string(mutation.value),
                RemuxFailure::unsupported);
    }
    auto truncated = stream();
    truncated.pop_back();
    refusal(std::move(truncated), "truncated final frame", RemuxFailure::malformed);
    auto changing = stream();
    changing[417 + 3] = 0xc0;
    refusal(std::move(changing), "channel count changes", RemuxFailure::unsupported);
    auto junk = stream();
    junk.insert(junk.begin() + 417, 0);
    refusal(std::move(junk), "midstream junk has no resync", RemuxFailure::malformed);
    auto reservoir = stream();
    reservoir[4] = 1;
    refusal(std::move(reservoir), "reservoir before first frame", RemuxFailure::malformed);
    auto vbri = stream();
    std::memcpy(vbri.data() + 36, "VBRI", 4);
    refusal(std::move(vbri), "unvalidated VBRI admission", RemuxFailure::unsupported);
}
void xing_counts_without_gapless() {
    auto bytes = stream();
    std::memcpy(bytes.data() + 36, "Info", 4);
    bytes[43] = 3; // Flags: frame count and MPEG byte count.
    bytes[47] = 2; // Two audio frames, plus the metadata frame.
    bytes[50] = 4;
    bytes[51] = 0xe3; // 1251 MPEG bytes, big endian.
    Input valid(bytes);
    const auto info = valid.probe();
    expect(info.xing && info.frame_count == 2 && info.coded_samples == 2304 &&
               !info.audible_samples && !info.encoder_trim,
           "Info header excluded from audio count, absent gapless stays unknown");
    bytes[47] = 3;
    refusal(bytes, "Xing frame count mismatch", RemuxFailure::malformed);
    bytes[47] = 2;
    bytes[51] = 0xe2;
    refusal(bytes, "Xing byte extent mismatch", RemuxFailure::malformed);
}
void metadata_bounds_and_contexts() {
    auto bytes = stream();
    const Bytes id3{'I', 'D', '3', 4, 0, 0, 0, 0, 0, 4, 't', 'e', 's', 't'};
    bytes.insert(bytes.begin(), id3.begin(), id3.end());
    Input valid(bytes);
    expect(valid.probe().audio_begin == 14 && valid.probe().coded_samples == 3456,
           "ID3v2.4 body skipped without changing clock");
    bytes[5] = 0x10;
    refusal(bytes, "ID3 footer flag outside initial subset", RemuxFailure::unsupported);
    bytes[5] = 0;
    bytes[6] = 0x80;
    refusal(bytes, "non-synchsafe ID3 size", RemuxFailure::malformed);
    bytes[6] = 1;
    bytes[7] = 0;
    refusal(bytes, "literal one-MiB ID3 bound", RemuxFailure::too_large);
    Input oversized(stream());
    oversized.declared_size = (1ULL << 40) + 1;
    try {
        (void)oversized.probe();
        throw std::logic_error("MP3 source bound ignored");
    } catch (const RemuxException& error) {
        expect(error.reason() == RemuxFailure::too_large, "MP3 literal TiB bound");
    }
    for (auto after : {std::size_t{0}, std::size_t{4}}) {
        Input input(stream());
        input.cancelled = after == 0;
        input.cancel_after = after;
        try {
            (void)input.probe();
            throw std::logic_error("MP3 cancellation ignored");
        } catch (const std::runtime_error& error) {
            expect(std::string(error.what()).find("stopped") != std::string::npos,
                   "MP3 cancellation category");
        }
        expect(input.calls == after, "MP3 no callbacks after cancellation");
    }
    Input expired(stream());
    expired.context.deadline = std::chrono::steady_clock::now();
    try {
        (void)expired.probe();
        throw std::logic_error("MP3 deadline ignored");
    } catch (const std::runtime_error& error) {
        expect(std::string(error.what()).find("stopped") != std::string::npos,
               "MP3 deadline category");
    }
    expect(expired.calls == 0, "expired MP3 deadline makes no reads");
}
void owned_fixtures(const std::filesystem::path& directory) {
    struct Case {
        const char* name;
        std::uint32_t rate, channels;
        std::uint64_t audible;
        bool vbr;
    };
    for (const auto& item : {Case{"cbr48.mp3", 48000, 2, 3072000, false},
                             {"vbr441.mp3", 44100, 2, 2822400, true},
                             {"mono24.mp3", 24000, 1, 312000, false},
                             {"mono12.mp3", 12000, 1, 36000, false}}) {
        Input input(directory / item.name);
        const auto info = input.probe();
        expect(info.sample_rate == item.rate && info.channels == item.channels && info.xing &&
                   info.variable_bitrate == item.vbr && info.audible_samples == item.audible &&
                   info.encoder_trim,
               std::string(item.name) + ": native Xing/gapless clock vs original authored samples");
        for (const auto target : {std::uint64_t{0}, std::uint64_t{8} * item.rate, item.audible / 2,
                                  std::numeric_limits<std::uint64_t>::max()}) {
            const auto point = locate_mp3_sample(info, target);
            expect(point.byte_offset >= info.audio_begin && point.byte_offset < info.audio_end &&
                       point.coded_sample < info.coded_samples,
                   std::string(item.name) + ": bounded locator at target " +
                       std::to_string(target));
        }
        for (std::size_t index = 1; index < info.seek_points.size(); ++index) {
            const auto gap =
                info.seek_points[index].coded_sample - info.seek_points[index - 1].coded_sample;
            expect(gap >= item.rate && gap < item.rate + info.samples_per_frame &&
                       info.seek_points[index].byte_offset >
                           info.seek_points[index - 1].byte_offset,
                   std::string(item.name) + ": ordered sparse coordinates at index " +
                       std::to_string(index));
        }
        auto damaged = input.bytes;
        constexpr std::string_view encoder_marker = "Lavc";
        const auto found = std::search(damaged.begin(), damaged.end(), encoder_marker.begin(),
                                       encoder_marker.end());
        expect(found != damaged.end(), std::string(item.name) + ": owned encoder marker exists");
        damaged.at(static_cast<std::size_t>(found - damaged.begin()) + 21) ^= 1;
        refusal(std::move(damaged), std::string(item.name) + ": encoder trim CRC",
                RemuxFailure::malformed);
        std::cout << item.name << ": native samples=" << *info.audible_samples
                  << " frames=" << info.frame_count << '\n';
    }
    Input raw(directory / "bare48.mp3"), tagged(directory / "id3-bare48.mp3");
    const auto plain = raw.probe(), metadata = tagged.probe();
    expect(!plain.audible_samples && plain.coded_samples == metadata.coded_samples &&
               metadata.audio_begin == 14 && metadata.audio_end + 128 == tagged.bytes.size(),
           "missing trim remains unknown across leading/trailing metadata");
    Input invalid_mono(directory / "invalid-mono-crc.mp3");
    refusal(invalid_mono.bytes, "retained development encoder invalid mono CRC",
            RemuxFailure::malformed);
}
void sparse_scaling_and_resource_bounds() {
    std::atomic_bool stopped{false}, cancelled{false};
    MediaReadContext context{&stopped, &cancelled,
                             std::chrono::steady_clock::now() + std::chrono::seconds(20)};
    auto header = frame();
    // 320-kbps MPEG-1 at 32 kHz: literal 1440-byte frame, 1152 samples.
    header[2] = 0xe8;
    constexpr std::uint64_t frame_bytes = 1440, frames = 150000;
    std::uint64_t virtual_frames = frames, virtual_frame_bytes = frame_bytes, calls = 0,
                  requested = 0;
    std::size_t read_limit = 65536;
    MediaSource source{
        [&] { return virtual_frames * virtual_frame_bytes; },
        [&](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
            const MediaReadContext&) {
            ++calls;
            requested += capacity;
            expect(capacity <= 1440, "sparse MP3 probe never allocates media extent");
            const auto actual = std::min(capacity, read_limit);
            for (std::size_t index = 0; index < actual; ++index) {
                const auto within = (offset + index) % virtual_frame_bytes;
                output[index] = within < 4 ? header[static_cast<std::size_t>(within)] : 0;
            }
            return actual;
        }};
    const auto info = probe_mp3_audio(source, context);
    expect(info.frame_count == 150000 && info.coded_samples == 172800000 &&
               info.audio_end == 216000000 && info.seek_points.size() < 6000 &&
               requested < 6000000 && calls < 310000,
           "sparse 216-MB MP3 exact clock and bounded header-only reads");
    virtual_frames = 1000001;
    calls = 0;
    requested = 0;
    try {
        (void)probe_mp3_audio(source, context);
        throw std::logic_error("MP3 frame bound ignored");
    } catch (const RemuxException& error) {
        expect(error.reason() == RemuxFailure::too_large &&
                   std::string(error.what()) == "MP3 frame count bound",
               "literal million-frame bound");
    }
    calls = 0;
    requested = 0;
    read_limit = 1;
    try {
        (void)probe_mp3_audio(source, context);
        throw std::logic_error("MP3 callback bound ignored");
    } catch (const RemuxException& error) {
        expect(error.reason() == RemuxFailure::too_large &&
                   std::string(error.what()) == "MP3 callback budget" && calls == 2100000,
               "literal callback bound with one-byte progress");
    }
    // MPEG-2.5 mono at 8 kHz, 8 kbps: 72-byte frames, 576 samples.
    header[1] = 0xe3;
    header[2] = 0x18;
    header[3] = 0xc0;
    virtual_frame_bytes = 72;
    virtual_frames = 917505;
    calls = 0;
    requested = 0;
    read_limit = 65536;
    try {
        (void)probe_mp3_audio(source, context);
        throw std::logic_error("MP3 locator bound ignored");
    } catch (const RemuxException& error) {
        expect(error.reason() == RemuxFailure::too_large &&
                   std::string(error.what()) == "MP3 seek locator bound",
               "literal 65536 sparse-locator bound");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        expect(argc == 2, "owned MP3 fixture directory required");
        literal_headers_and_refusals();
        xing_counts_without_gapless();
        metadata_bounds_and_contexts();
        owned_fixtures(argv[1]);
        sparse_scaling_and_resource_bounds();
        std::cout << "MP3 framing, metadata, clock, locators, bounds and cancellation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "MP3 input failure: " << error.what() << '\n';
        return 1;
    }
}
