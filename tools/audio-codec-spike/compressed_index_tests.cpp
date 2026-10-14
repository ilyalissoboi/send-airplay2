// SPDX-License-Identifier: Apache-2.0
#include "hls_test_support.h"
#include "virtual_audio_source.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace sap2_test;
constexpr std::uint32_t long_audible_frames = 1440144;
constexpr std::uint32_t long_movie_frames = 1464000;

std::vector<float> reference(const std::filesystem::path& path, std::uint32_t expected_frames) {
    Context context;
    Source source(path);
    // Independent ordinary sample-clock demuxing, without the new native index.
    auto decoder = AudioDecoderSource::open(source.callbacks(), context.value, 42, false);
    std::vector<float> result;
    for (std::uint32_t start = 0; start < expected_frames; start += 480000) {
        const auto window = decoder->read_window(start, 480000, context.value);
        result.insert(result.end(), window.samples.begin(), window.samples.end());
        if (window.eof) {
            break;
        }
    }
    expect(result.size() == expected_frames * 2,
           path.filename().string() +
               ": literal reference end, actual=" + std::to_string(result.size() / 2));
    return result;
}
void compare(const std::vector<float>& expected, const DecodedAudioWindow& window,
             const std::string& scenario) {
    const auto offset = window.first_sample * 2;
    expect(window.channels == 2 && window.source_track_id == 42 && !window.samples.empty() &&
               offset + window.samples.size() <= expected.size(),
           scenario + ": PCM bounds/layout/identity");
    double error = 0;
    for (std::size_t index = 0; index < window.samples.size(); ++index) {
        expect(std::isfinite(window.samples[index]), scenario + ": nonfinite PCM");
        const auto difference = window.samples[index] - expected[offset + index];
        error += difference * difference;
    }
    expect(error / window.samples.size() < 1e-10,
           scenario + ": phase mismatch, MSE=" + std::to_string(error / window.samples.size()));
}
void long_windows(const std::filesystem::path& directory, const std::string& name) {
    const auto expected = reference(directory / (name + "-fine.mkv"), long_audible_frames);
    Context context;
    Source source(directory / (name + ".mkv"));
    auto decoder = AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
    expect(!decoder->requires_start_rebuild(),
           name + ": native index replaces start reconstruction");
    for (const auto start : {1400000U, 480013U, 960003U, 900001U, 904097U, 480013U}) {
        const auto window = decoder->read_window(start, 4096, context.value);
        expect(window.samples.size() == 8192 && window.timestamp_precision_samples == 48,
               name + ": exact window length and original clock precision");
        compare(expected, window, name + ": cold/reused start=" + std::to_string(start));
    }
    for (const bool cold : {false, true}) {
        if (cold) {
            decoder = AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
        }
        const auto tail = decoder->read_window(long_audible_frames - 144, 4096, context.value);
        expect(tail.eof && tail.samples.size() == 288,
               name + ": literal cold/reused 144-sample tail");
        compare(expected, tail, name + ": EOF");
    }
}
std::vector<Bytes> audio(const std::filesystem::path& path) {
    Context context;
    Source source(path);
    Store store;
    auto hls = StereoAacHls::open(source.callbacks(), store.callbacks(), 4 * 1024 * 1024,
                                  context.value, 42);
    expect(hls->audio_segment_count() == 6, path.filename().string() + ": six AAC segments");
    std::vector<Bytes> fragments(7);
    fragments[0] = read(resource(*hls, "audio-init.mp4"), context.value);
    for (const auto index : {5U, 0U, 3U, 1U, 4U, 2U}) {
        fragments[index + 1] =
            read(resource(*hls, "a" + std::to_string(index) + ".m4s"), context.value);
    }
    auto combined = fragments[0];
    for (std::size_t index = 1; index < fragments.size(); ++index) {
        combined.insert(combined.end(), fragments[index].begin(), fragments[index].end());
    }
    expect(MovieDecoder(combined).decode().size() == long_movie_frames * 2,
           path.filename().string() + ": independent MOV/AAC final presentation trim");
    expect(store.seals == 6 && hls->failure() == StereoAacHlsFailure::none,
           "compressed AAC segments sealed without terminal failure");
    expect(read(resource(*hls, "a5.m4s"), context.value) == fragments[6],
           "compressed late cache reuse");
    hls->stop();
    expect(store.clears == 1 && store.sealed.empty(), "compressed store cleared once");
    return fragments;
}
void independent_trim(const std::filesystem::path& directory, const std::string& name,
                      std::uint32_t expected_end) {
    const auto expected = reference(directory / (name + "-audible.mkv"), expected_end);
    Context context;
    Source source(directory / (name + "-trim.mkv"));
    auto decoder = AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
    for (const auto start : {expected_end - 144, 0U, 48013U, expected_end - 144}) {
        const auto window = decoder->read_window(start, 4096, context.value);
        if (start == expected_end - 144) {
            expect(window.eof && window.samples.size() == 288,
                   name + ": literal independently trimmed EOF");
        }
        compare(expected, window,
                name + ": independently prepared trim, start=" + std::to_string(start));
    }
}
void bad_headers(const std::filesystem::path& directory, const std::string& name) {
    Source source(directory / (name + ".mkv"));
    const RandomReader read = [&](std::uint64_t offset, std::uint8_t* output, std::size_t length) {
        expect(offset + length <= source.bytes->size(), "header mutation source bounds");
        std::memcpy(output, source.bytes->data() + offset, length);
    };
    const auto packets = read_mkv_audio_packets(read, source.bytes->size(), 42);
    expect(packets.size() > 2, name + ": mutation packet inventory");
    const auto index = name == "truehd" ? 0U : 2U;
    const auto offset = packets[index].offset;
    if (name == "opus") {
        (*source.bytes)[offset] = 0xff;
        (*source.bytes)[offset + 1] = 0;
    } else if (name == "vorbis") {
        (*source.bytes)[offset] |= 1;
    } else if (name == "truehd") {
        (*source.bytes)[offset + 1] ^= 1;
    } else {
        (*source.bytes)[offset] = 0;
    }
    Context context;
    try {
        (void)AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
        throw std::runtime_error(name + ": malformed duration/restart header accepted");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::unsupported ||
                   error.reason() == AudioDecodeFailure::malformed,
               name + ": malformed header refusal category: " + error.what());
    }
}
void sparse_flac(const std::filesystem::path& fixture) {
    VirtualAudioSource source(fixture);
    Context context;
    auto decoder = AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
    expect(source.size_calls == 1 && source.requested < 64ULL * 1024 * 1024 &&
               source.payload_read < 4ULL * 1024 * 1024,
           "hour FLAC header index/probe remains bounded");
    std::cout << "hour FLAC open: requested=" << source.requested
              << ", actual_payload=" << source.payload_read << '\n';
    constexpr double pi = 3.14159265358979323846;
    for (const auto start : {172704003ULL, 96003ULL, 86400003ULL, 172704003ULL}) {
        source.reset_reads();
        const auto window = decoder->read_window(start, 4096, context.value);
        expect(window.samples.size() == 8192 && source.requested < 16ULL * 1024 * 1024 &&
                   source.payload_read < 256ULL * 1024 && source.largest_offset > 0x1'0000'0000,
               "hour FLAC nearby bounded packets above 4 GiB, start=" + std::to_string(start));
        std::cout << "hour FLAC window: start=" << start << ", requested=" << source.requested
                  << ", actual_payload=" << source.payload_read << '\n';
        for (const auto index : {0U, 91U, 1001U, 4095U}) {
            for (std::size_t channel = 0; channel < 2; ++channel) {
                const auto frequency = channel == 0 ? 173U : 227U;
                const auto frame = (start + index) % 48000;
                const auto expected =
                    std::round(4096 * std::sin(2 * pi * frequency * frame / 48000)) / 32768;
                expect(std::abs(window.samples[index * 2 + channel] - expected) < 1.0 / 32768,
                       "hour FLAC independent tone, start=" + std::to_string(start) + ", index=" +
                           std::to_string(index) + ", channel=" + std::to_string(channel));
            }
        }
    }
    const auto tail = decoder->read_window(172799856, 4096, context.value);
    expect(tail.eof && tail.samples.size() == 288, "hour FLAC literal 144-sample EOF tail");
    source.fail_payload = true;
    try {
        (void)decoder->read_window(172704003, 4096, context.value);
        throw std::runtime_error("hour FLAC source failure accepted");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::source_error,
               "FLAC indexed source failure category");
    }
    source.fail_payload = false;
    try {
        (void)decoder->read_window(0, 1, context.value);
        throw std::runtime_error("failed FLAC indexed decoder reused");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::unsupported,
               "FLAC failure poisons indexed owner");
    }
}
void vorbis_overlap_refusal(const std::filesystem::path& directory) {
    Source source(directory / "vorbis-trim.mkv");
    const Bytes header{0x75, 0xa2, 0x88};
    const Bytes original_padding{0xff, 0xff, 0xff, 0xff, 0xfe, 0xba, 0x7a, 0xab};
    const auto field =
        std::search(source.bytes->begin(), source.bytes->end(), header.begin(), header.end());
    expect(field != source.bytes->end() && source.bytes->end() - field >= 11,
           "Vorbis first signed padding element bounds");
    // -1024 source samples at 48 kHz round to -21333333 ns. Add one
    // source sample of leading skip, which the intrinsic overlap discard would
    // otherwise silently overwrite. Literal signed big-endian nanoseconds.
    expect(std::equal(original_padding.begin(), original_padding.end(), field + 3),
           "Vorbis original first padding literal");
    const Bytes extra_padding{0xff, 0xff, 0xff, 0xff, 0xfe, 0xba, 0x29, 0x49};
    std::copy(extra_padding.begin(), extra_padding.end(), field + 3);
    Context context;
    try {
        (void)AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
        throw std::runtime_error("extra Vorbis first skip accepted");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::unsupported &&
                   std::string(error.what()) ==
                       "indexed Vorbis first trim must match decoder overlap priming",
               std::string("Vorbis extra first trim refusal: ") + error.what());
    }
}
void exact_continuity_refusal(const std::filesystem::path& directory) {
    Context context;
    Source source(directory / "flac-middle-discard.mkv");
    auto decoder = AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
    try {
        (void)decoder->read_window(0, 48000, context.value);
        throw std::runtime_error(
            "16-sample internal discard gap concealed by original 48-sample precision");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::unsupported &&
                   std::string(error.what()) == "audio presentation timeline is discontinuous",
               std::string("indexed exact continuity refusal: ") + error.what());
    }
    const auto calls = source.calls.load();
    try {
        (void)decoder->read_window(0, 1, context.value);
        throw std::runtime_error("failed discontinuous indexed decoder reused");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::unsupported && source.calls == calls,
               "discontinuous indexed owner remains unusable without callbacks");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        expect(argc == 2, "compressed index fixture directory");
        const std::filesystem::path directory(argv[1]);
        for (const auto* name : {"flac", "opus", "vorbis", "truehd"}) {
            long_windows(directory, name);
            expect(audio(directory / (std::string(name) + ".mkv")) ==
                       audio(directory / (std::string(name) + "-fine.mkv")),
                   std::string(name) + ": byte-identical AAC joins from cold/reordered windows");
            bad_headers(directory, name);
            std::cout << name << ": long windows, AAC joins and header refusal passed\n";
        }
        independent_trim(directory, "flac", 95736);
        independent_trim(directory, "opus", 95960);
        independent_trim(directory, "vorbis", 95984);
        independent_trim(directory, "truehd", 95980);
        sparse_flac(directory / "flac-cycle.mkv");
        vorbis_overlap_refusal(directory);
        exact_continuity_refusal(directory);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "compressed index failure: " << error.what() << '\n';
        return 1;
    }
}
