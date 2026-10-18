// SPDX-License-Identifier: Apache-2.0
#include "standalone_audio.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace send_airplay2;
using namespace send_airplay2::detail;
void expect(bool passed, const std::string& scenario) {
    if (!passed) {
        throw std::runtime_error(scenario);
    }
}
void append(Bytes& bytes, std::uint64_t value, std::size_t width, bool big_endian = false) {
    for (std::size_t index = 0; index < width; ++index) {
        const auto shift = 8 * (big_endian ? width - index - 1 : index);
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}
void set(Bytes& bytes, std::size_t offset, std::uint64_t value, std::size_t width) {
    for (std::size_t index = 0; index < width; ++index) {
        bytes.at(offset + index) = static_cast<std::uint8_t>(value >> (8 * index));
    }
}
void tag(Bytes& bytes, const char* text) {
    bytes.insert(bytes.end(), text, text + 4);
}
constexpr std::size_t riff_header_bytes = 12;
constexpr std::size_t chunk_header_bytes = 8;
constexpr std::size_t format_body_offset = riff_header_bytes + chunk_header_bytes;
constexpr std::size_t format_body_bytes = 16;
constexpr std::size_t data_size_offset = format_body_offset + format_body_bytes + 4;
Bytes pcm_format() {
    Bytes bytes;
    append(bytes, 1, 2);
    append(bytes, 2, 2);
    append(bytes, 48000, 4);
    append(bytes, 192000, 4);
    append(bytes, 4, 2);
    append(bytes, 16, 2);
    return bytes;
}
Bytes wave() {
    Bytes bytes;
    tag(bytes, "RIFF");
    append(bytes, 0, 4);
    tag(bytes, "WAVE");
    tag(bytes, "fmt ");
    append(bytes, format_body_bytes, 4);
    const auto format = pcm_format();
    bytes.insert(bytes.end(), format.begin(), format.end());
    tag(bytes, "data");
    append(bytes, 32, 4);
    bytes.resize(bytes.size() + 32);
    set(bytes, 4, bytes.size() - 8, 4);
    return bytes;
}
Bytes flac(bool final_streaminfo = true, std::uint64_t frames = 132432) {
    Bytes bytes;
    tag(bytes, "fLaC");
    append(bytes, final_streaminfo ? 0x80 : 0, 1);
    append(bytes, 34, 3, true);
    append(bytes, 4608, 2, true);
    append(bytes, 4608, 2, true);
    append(bytes, 0, 6); // Unknown compressed frame byte sizes.
    append(bytes, (44100ULL << 44) | (1ULL << 41) | (15ULL << 36) | frames, 8, true);
    bytes.resize(bytes.size() + 16); // No STREAMINFO MD5 check is performed by this header test.
    if (final_streaminfo) {
        bytes.push_back(0xff);
        bytes.push_back(0xf8);
    }
    return bytes;
}
struct Input {
    Bytes bytes;
    std::uint64_t declared_size;
    std::size_t calls = 0;
    std::atomic_bool stopped{false}, cancelled{false};
    MediaReadContext context{&stopped, &cancelled,
                             std::chrono::steady_clock::now() + std::chrono::seconds(15)};
    std::size_t cancel_after = 0;
    explicit Input(Bytes data) : bytes(std::move(data)), declared_size(bytes.size()) {}
    MediaSource source() {
        return {[this] { return declared_size; },
                [this](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
                       const MediaReadContext&) {
                    ++calls;
                    expect(capacity <= 65536, "standalone probe callback capacity");
                    expect(offset <= bytes.size(),
                           "sparse native probe must not read audio payload");
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
void refusal(Bytes bytes, const std::string& scenario, RemuxFailure reason) {
    Input input(std::move(bytes));
    try {
        (void)input.probe();
        throw std::runtime_error(scenario + ": unexpectedly admitted");
    } catch (const RemuxException& error) {
        expect(error.reason() == reason, scenario + ": wrong refusal category");
    }
}
void wave_headers() {
    Input valid(wave());
    const auto info = valid.probe();
    expect(info.container == SourceContainer::wave && info.tracks.size() == 1 &&
               info.tracks[0].id == 1 && info.tracks[0].standalone_sample_frames == 8 &&
               info.tracks[0].timescale == 48000 && info.tracks[0].audio_channel_mask == 3,
           "RIFF exact packed duration and source identity");
    expect(select_source_audio(info).status == AudioSelectionStatus::selected &&
               select_source_audio(info, 99).status == AudioSelectionStatus::invalid_selection,
           "standalone source identity selection never falls back");
    // Authored WAVEFORMATEXTENSIBLE: base fields, cbSize, valid bits, mask and GUID.
    Bytes extended;
    tag(extended, "RIFF");
    append(extended, 0, 4);
    tag(extended, "WAVE");
    tag(extended, "fmt ");
    append(extended, 40, 4);
    auto format = pcm_format();
    set(format, 0, 0xfffe, 2);
    extended.insert(extended.end(), format.begin(), format.end());
    append(extended, 22, 2);
    append(extended, 16, 2);
    append(extended, 3, 4);
    append(extended, 1, 4);
    append(extended, 0x00100000, 4);
    for (auto byte : {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}) {
        extended.push_back(static_cast<std::uint8_t>(byte));
    }
    tag(extended, "data");
    append(extended, 32, 4);
    extended.resize(extended.size() + 32);
    set(extended, 4, extended.size() - 8, 4);
    Input extensible(extended);
    expect(extensible.probe().tracks.front().audio_channel_mask == 3,
           "WAVE extensible native speaker mask");
    auto bad_extension = extended;
    set(bad_extension, format_body_offset + 18, 8, 2);
    refusal(std::move(bad_extension), "WAVE reduced valid-bit packing", RemuxFailure::unsupported);
    bad_extension = extended;
    bad_extension.at(format_body_offset + 39) ^= 1;
    refusal(std::move(bad_extension), "WAVE unknown subtype GUID", RemuxFailure::unsupported);
    bad_extension = extended;
    set(bad_extension, format_body_offset + 20, 0x100001, 4);
    refusal(std::move(bad_extension), "WAVE unsupported speaker with matching channel count",
            RemuxFailure::unsupported);
    auto changed = wave();
    set(changed, data_size_offset, 31, 4);
    refusal(std::move(changed), "RIFF partial frame/pad", RemuxFailure::malformed);
    changed = wave();
    set(changed, format_body_offset + 8, 1, 4);
    refusal(std::move(changed), "RIFF byte rate mismatch", RemuxFailure::malformed);
    changed = wave();
    set(changed, format_body_offset + 14, 8, 2);
    refusal(std::move(changed), "RIFF unsigned 8-bit outside subset", RemuxFailure::unsupported);
    changed = wave();
    set(changed, format_body_offset, 0x55, 2);
    refusal(std::move(changed), "RIFF compressed codec outside subset", RemuxFailure::unsupported);
    changed = wave();
    changed.pop_back();
    refusal(std::move(changed), "RIFF declared size exceeds file", RemuxFailure::malformed);
    Bytes many;
    tag(many, "RIFF");
    append(many, 0, 4);
    tag(many, "WAVE");
    for (std::size_t index = 0; index < 4097; ++index) {
        tag(many, "JUNK");
        append(many, 0, 4);
    }
    set(many, 4, many.size() - 8, 4);
    refusal(std::move(many), "RIFF chunk admission bound", RemuxFailure::too_large);
    try {
        (void)probe_media_tracks(valid.source(), valid.context);
        throw std::runtime_error("public legacy probe admitted WAVE");
    } catch (const RemuxException&) { /* Existing MP4/MKV inventory remains unchanged. */
    }
}
void sparse_rf64() {
    constexpr std::uint64_t audio_bytes = 5ULL << 30;
    constexpr std::uint64_t frame_count = 1342177280; // Literal five GiB / stereo PCM16 frame.
    constexpr std::uint64_t header_bytes = 92;        // Includes a sentinel fact chunk.
    Bytes bytes;
    tag(bytes, "RF64");
    append(bytes, 0xffffffffU, 4);
    tag(bytes, "WAVE");
    tag(bytes, "ds64");
    append(bytes, 28, 4);
    append(bytes, header_bytes + audio_bytes - 8, 8);
    append(bytes, audio_bytes, 8);
    append(bytes, frame_count, 8);
    append(bytes, 0, 4);
    tag(bytes, "fmt ");
    append(bytes, 16, 4);
    const auto format = pcm_format();
    bytes.insert(bytes.end(), format.begin(), format.end());
    tag(bytes, "fact");
    append(bytes, 4, 4);
    append(bytes, 0xffffffffU, 4); // ds64 owns the exact 64-bit count.
    tag(bytes, "data");
    append(bytes, 0xffffffffU, 4);
    expect(bytes.size() == header_bytes, "RF64 authored header layout");
    Input input(bytes);
    input.declared_size = header_bytes + audio_bytes;
    const auto info = input.probe();
    expect(info.tracks.front().standalone_sample_frames == frame_count && input.calls < 30,
           "RF64 five-GiB duration uses only bounded header reads");
    set(bytes, 20, header_bytes + audio_bytes - 7, 8);
    Input malformed(bytes);
    malformed.declared_size = input.declared_size;
    try {
        (void)malformed.probe();
        throw std::runtime_error("RF64 inconsistent root size admitted");
    } catch (const RemuxException& error) {
        expect(error.reason() == RemuxFailure::malformed, "RF64 size refusal category");
    }
}
void flac_headers() {
    Input valid(flac());
    const auto info = valid.probe();
    expect(info.container == SourceContainer::flac &&
               info.tracks.front().standalone_sample_frames == 132432 &&
               info.tracks.front().timescale == 44100 &&
               info.tracks.front().audio_channel_mask == 3,
           "FLAC native STREAMINFO fields");
    refusal(flac(true, 0), "FLAC unknown total samples", RemuxFailure::unsupported);
    auto changed = flac();
    changed[7] = 33;
    refusal(std::move(changed), "FLAC short STREAMINFO", RemuxFailure::malformed);
    changed = flac(false);
    changed.insert(changed.end(), {0x86, 0, 0, 0, 0xff, 0xf8});
    refusal(std::move(changed), "FLAC picture metadata outside subset", RemuxFailure::unsupported);
    changed = flac(false);
    changed.insert(changed.end(), {0x83, 0, 0, 18});
    append(changed, 0, 8, true);
    append(changed, 10000, 8, true);
    append(changed, 4608, 2, true);
    changed.insert(changed.end(), {0xff, 0xf8});
    refusal(std::move(changed), "FLAC seek offset outside frame data", RemuxFailure::malformed);
    changed = flac(false);
    changed.insert(changed.end(), {0x83, 0, 0, 1, 0, 0xff, 0xf8});
    refusal(std::move(changed), "FLAC partial seek point", RemuxFailure::unsupported);
}
void contexts_and_source_bounds() {
    Input cancelled(wave());
    cancelled.cancelled = true;
    try {
        (void)cancelled.probe();
        throw std::logic_error("cancelled native probe admitted");
    } catch (const std::runtime_error& error) {
        expect(std::string(error.what()) == "standalone audio probe stopped",
               "cancelled context category");
    }
    expect(cancelled.calls == 0, "cancelled context has no reads");
    Input mid(wave());
    mid.cancel_after = 3;
    try {
        (void)mid.probe();
        throw std::logic_error("mid-read cancellation ignored");
    } catch (const std::runtime_error& error) {
        expect(std::string(error.what()) == "standalone audio probe stopped",
               "mid-read cancellation category");
    }
    expect(mid.calls == 3, "mid-read cancellation stops callbacks promptly");
    Input large(wave());
    large.declared_size = (1ULL << 40) + 1;
    try {
        (void)large.probe();
        throw std::logic_error("native source limit ignored");
    } catch (const RemuxException& error) {
        expect(error.reason() == RemuxFailure::too_large, "native source size category");
    }
}
} // namespace
int main() {
    try {
        wave_headers();
        sparse_rf64();
        flac_headers();
        contexts_and_source_bounds();
        std::cout << "Standalone audio metadata, bounds, sparse RF64 and cancellation passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Standalone audio failure: " << error.what() << '\n';
        return 1;
    }
}
