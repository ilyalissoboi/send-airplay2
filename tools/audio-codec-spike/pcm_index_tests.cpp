// SPDX-License-Identifier: Apache-2.0
#include "hls_test_support.h"
#include "virtual_pcm_source.h"
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
constexpr std::uint32_t audible_frames = 1440144;
constexpr std::uint32_t movie_frames = 1464000;
constexpr double pi = 3.14159265358979323846;
void compare(const std::vector<float>& reference, const DecodedAudioWindow& actual,
             const std::string& scenario) {
    const auto offset = actual.first_sample * 2;
    expect(actual.channels == 2 && actual.source_track_id == 42 &&
               offset + actual.samples.size() <= reference.size() && !actual.samples.empty(),
           scenario + ": selected identity/PCM bounds");
    double error = 0;
    for (std::size_t index = 0; index < actual.samples.size(); ++index) {
        expect(std::isfinite(actual.samples[index]), scenario + ": nonfinite PCM");
        const auto difference = actual.samples[index] - reference[offset + index];
        error += difference * difference;
    }
    expect(error / actual.samples.size() < 1e-10,
           scenario +
               ": phase/sample mismatch, MSE=" + std::to_string(error / actual.samples.size()));
}
std::vector<float> fine_reference(const std::filesystem::path& path,
                                  std::uint32_t expected_frames = audible_frames) {
    Context context;
    Source source(path);
    auto decoder = AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
    std::vector<float> samples;
    for (std::uint32_t first = 0; first < expected_frames; first += 480000) {
        const auto window = decoder->read_window(first, 480000, context.value);
        samples.insert(samples.end(), window.samples.begin(), window.samples.end());
        if (window.eof) {
            break;
        }
    }
    expect(samples.size() == expected_frames * 2,
           path.filename().string() + ": literal audible samples expected=" +
               std::to_string(expected_frames) + " actual=" + std::to_string(samples.size() / 2));
    return samples;
}
void long_windows(const std::filesystem::path& directory) {
    const auto reference = fine_reference(directory / "variable-44100-fine.mkv");
    Context context;
    Source source(directory / "variable-44100.mkv");
    auto decoder = AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
    expect(!decoder->requires_start_rebuild(), "long PCM selects packet index");
    for (const auto start : {1400000U, 480013U, 960003U, 900001U, 904097U, 480013U}) {
        const auto window = decoder->read_window(start, 4096, context.value);
        expect(window.samples.size() == 8192 && window.timestamp_precision_samples == 48,
               "long window preserves length and original absolute clock precision");
        compare(reference, window, "long cold/reused start=" + std::to_string(start));
    }
    const auto tail = decoder->read_window(audible_frames - 144, 4096, context.value);
    expect(tail.eof && tail.samples.size() == 288, "long reused literal EOF tail");
    compare(reference, tail, "long reused EOF");
    auto cold = AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
    const auto cold_tail = cold->read_window(audible_frames - 144, 4096, context.value);
    expect(cold_tail.eof && cold_tail.samples.size() == 288, "long cold literal EOF tail");
    compare(reference, cold_tail, "long cold EOF");
}
std::vector<Bytes> audio(const std::filesystem::path& path) {
    Context context;
    Source source(path);
    Store store;
    auto hls = StereoAacHls::open(source.callbacks(), store.callbacks(), 4 * 1024 * 1024,
                                  context.value, 42);
    expect(hls->audio_segment_count() == 6, "long six-segment AAC presentation");
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
    const auto pcm = MovieDecoder(combined).decode();
    expect(pcm.size() == movie_frames * 2, "long independent MOV/AAC final trim");
    expect(store.seals == 6 && hls->failure() == StereoAacHlsFailure::none,
           "long all segments sealed without terminal failure");
    expect(read(resource(*hls, "a5.m4s"), context.value) == fragments[6], "long late cache reuse");
    hls->stop();
    expect(store.clears == 1 && store.sealed.empty(), "long store clears once");
    return fragments;
}
void sparse_hour(const std::filesystem::path& fixture) {
    VirtualPcmSource source(fixture);
    Context context;
    auto decoder = AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
    expect(source.size_calls == 1 && source.requested < 64ULL * 1024 * 1024 &&
               source.payload_read < 4ULL * 1024 * 1024,
           "hour metadata index/probe stays bounded without whole-audio reads");
    std::cout << "hour open: requested_bytes=" << source.requested
              << ", actual_pcm_bytes=" << source.payload_read << '\n';
    for (const auto start : {172704003ULL, 96003ULL, 86400003ULL, 172704003ULL}) {
        source.reset_reads();
        const auto window = decoder->read_window(start, 4096, context.value);
        expect(window.samples.size() == 8192 && window.channels == 2 &&
                   window.source_track_id == 42,
               "hour cold/backward/forward window length and selected identity");
        expect(source.payload_read <= 3ULL * 192000 && source.requested < 32ULL * 1024 * 1024 &&
                   source.largest_offset > 0x1'0000'0000,
               "hour seek reads nearby PCM above 4 GiB, start=" + std::to_string(start));
        std::cout << "hour window: start=" << start << ", requested_bytes=" << source.requested
                  << ", actual_pcm_bytes=" << source.payload_read << '\n';
        for (const auto index : {0U, 91U, 1001U, 4095U}) {
            for (std::size_t channel = 0; channel < 2; ++channel) {
                const auto frequency = channel == 0 ? 173U : 227U;
                const auto frame = (start + index) % 48000;
                const auto expected =
                    std::round(4096 * std::sin(2 * pi * frequency * frame / 48000)) / 32768;
                expect(std::abs(window.samples[index * 2 + channel] - expected) < 1.0 / 32768,
                       "hour independent tone sample, start=" + std::to_string(start) + ", index=" +
                           std::to_string(index) + ", channel=" + std::to_string(channel));
            }
        }
    }
    const auto tail = decoder->read_window(172799856, 4096, context.value);
    expect(tail.eof && tail.samples.size() == 288, "hour literal 144-sample tail");
    source.fail_payload = true;
    try {
        (void)decoder->read_window(172704003, 4096, context.value);
        throw std::runtime_error("hour source failure accepted");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::source_error,
               "indexed packet source failure category");
    }
    source.fail_payload = false;
    try {
        (void)decoder->read_window(0, 1, context.value);
        throw std::runtime_error("failed indexed decoder reused");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::unsupported,
               "indexed failure poisons decoder");
    }
}
void primed_windows(const std::filesystem::path& directory) {
    // ceil((1323132 - 132 - 132) * 48000 / 44100): a partial
    // final output interval still produces one sample after resampler drain.
    constexpr std::uint32_t expected_end = 1439857;
    const auto reference = fine_reference(directory / "trimmed-44100-fine.mkv", expected_end);
    Context context;
    Source source(directory / "trimmed-44100.mkv");
    auto decoder = AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
    for (const auto start : {expected_end - 144, 480013U, 0U, expected_end - 144}) {
        const auto window = decoder->read_window(start, 4096, context.value);
        if (start == expected_end - 144) {
            expect(window.eof && window.samples.size() == 288, "primed PCM literal trimmed tail");
        }
        compare(reference, window, "primed PCM start=" + std::to_string(start));
    }
    expect(std::all_of(reference.begin(), reference.begin() + 64,
                       [](float sample) { return std::abs(sample) < 1e-8; }),
           "extra leading trim preserves initial silence after CodecDelay exactly once");
    Source raw_fine(directory / "trimmed-44100-raw-fine.mkv");
    auto exact = AudioDecoderSource::open(raw_fine.callbacks(), context.value, 42, true);
    compare(reference, exact->read_window(0, 4096, context.value),
            "sample-clock indexed PCM preserves raw first-block trim with delay");
}
void clock_admission(const std::filesystem::path& directory) {
    Context context;
    Source boundary(directory / "clock-one-second.mkv");
    auto decoder = AudioDecoderSource::open(boundary.callbacks(), context.value, 42, true);
    const auto window = decoder->read_window(0, 128, context.value);
    expect(window.samples.size() == 256 && window.timestamp_precision_samples == 48000,
           "literal one-second clock preserves 48000-sample absolute precision");
    for (const auto* name : {"clock-over-one-second.mkv", "clock-over-uint32.mkv"}) {
        Source source(directory / name);
        const bool exceeds_backend_scale = std::string(name) == "clock-over-uint32.mkv";
        const auto expected_reason =
            exceeds_backend_scale ? AudioDecodeFailure::malformed : AudioDecodeFailure::unsupported;
        const auto* expected_message = exceeds_backend_scale
                                           ? "audio container open failed"
                                           : "indexed PCM timestamp clock is unsupported";
        // The pinned backend already refuses scales above UINT_MAX nanoseconds
        // during format open, before indexed precision is available.
        expect(probe_media_tracks(source.callbacks(), context.value).tracks.size() == 2,
               std::string(name) + ": native inventory accepts clock control");
        try {
            (void)AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
            throw std::runtime_error(std::string(name) + ": unsupported clock accepted");
        } catch (const AudioDecodeException& error) {
            expect(error.reason() == expected_reason &&
                       std::string(error.what()) == expected_message,
                   std::string(name) +
                       ": reject original precision before narrowing/indexing: " + error.what());
        }
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        expect(argc == 2, "PCM index fixture directory");
        const std::filesystem::path directory(argv[1]);
        long_windows(directory);
        primed_windows(directory);
        clock_admission(directory);
        expect(audio(directory / "variable-44100.mkv") ==
                   audio(directory / "variable-44100-fine.mkv"),
               "long byte-identical AAC joins against fine-clock controls");
        expect(audio(directory / "trimmed-44100.mkv") ==
                   audio(directory / "trimmed-44100-fine.mkv"),
               "primed/leading/final-trim PCM generates identical AAC joins");
        sparse_hour(directory.parent_path() / "expanded-decoder" / "pcm-s16le.mkv");
        std::cout << "long PCM index/window/AAC and sparse-hour checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "PCM index failure: " << error.what() << '\n';
        return 1;
    }
}
