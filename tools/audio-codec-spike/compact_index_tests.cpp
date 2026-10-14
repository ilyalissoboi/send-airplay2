// SPDX-License-Identifier: Apache-2.0
#include "hls_test_support.h"
#include "virtual_audio_source.h"
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace sap2_test;
void high_packet_movie(const std::filesystem::path& fixture, bool truehd) {
    const std::uint32_t seconds = truehd ? 3600 : 7200;
    VirtualAudioSource source(fixture, seconds);
    Context context;
    context.value.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    const auto started = std::chrono::steady_clock::now();
    std::unique_ptr<AudioDecoderSource> decoder;
    try {
        decoder = AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
    } catch (...) {
        std::cerr << "sparse open requested=" << source.requested
                  << ", returned_cycle=" << source.payload_read << '\n';
        throw;
    }
    const auto stats = decoder->index_statistics();
    const std::string scenario = truehd ? "hour TrueHD" : "two-hour Opus";
    // Literal independent packet counts and retained-memory ceiling. Do not
    // derive the expectation from the production anchor interval or limits.
    expect(stats.packets == (truehd ? 4320000ULL : 360000ULL), scenario + ": packet count");
    expect(stats.anchors <= seconds && stats.retained_bytes < 2 * 1024 * 1024,
           scenario + ": compact retained coordinates");
    expect(source.requested < 256ULL * 1024 * 1024 && source.size_calls == 1,
           scenario + ": unchanged open byte bound");
    std::cout << scenario << " open: packets=" << stats.packets << ", anchors=" << stats.anchors
              << ", retained_bytes=" << stats.retained_bytes << ", requested=" << source.requested
              << ", ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(
                     std::chrono::steady_clock::now() - started)
                     .count()
              << '\n';
    const auto movie_frames = static_cast<std::uint64_t>(seconds) * 48000;
    constexpr double pi = 3.14159265358979323846;
    for (const auto start :
         {movie_frames - 96003, 96003ULL, movie_frames / 2 + 3, movie_frames - 96003}) {
        context.value.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        source.reset_reads();
        const auto window = decoder->read_window(start, 4096, context.value);
        expect(window.channels == 2 && window.samples.size() == 8192 &&
                   source.requested < 2ULL * 1024 * 1024 && source.largest_offset > 0x1'0000'0000,
               scenario + ": nearby cold/reused >4-GiB reads, start=" + std::to_string(start));
        for (const auto index : {0U, 91U, 1001U, 4095U}) {
            for (std::size_t channel = 0; channel < 2; ++channel) {
                const auto frequency = channel == 0 ? 173U : 227U;
                const auto frame = (start + index) % 48000;
                const auto expected =
                    truehd ? std::round(4096 * std::sin(2 * pi * frequency * frame / 48000)) / 32768
                           : 0.0;
                expect(std::abs(window.samples[index * 2 + channel] - expected) < 1.0 / 32768,
                       scenario + ": independent tone/silence sample, start=" +
                           std::to_string(start) + ", index=" + std::to_string(index));
            }
        }
        std::cout << scenario << " window: start=" << start << ", requested=" << source.requested
                  << '\n';
    }
    // Native Opus's initial 120-sample pre-skip applies only once in this movie.
    const auto end = movie_frames - (truehd ? 0 : 120);
    const auto tail = decoder->read_window(end - 144, 4096, context.value);
    expect(tail.eof && tail.samples.size() == 288, scenario + ": literal 144-frame EOF tail");
    context.cancelled = true;
    try {
        (void)decoder->read_window(0, 1, context.value);
        throw std::runtime_error(scenario + ": cancelled sparse epoch accepted");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::stopped, scenario + ": cancellation category");
    }
    context.cancelled = false;
    const auto requested = source.requested.load();
    try {
        (void)decoder->read_window(0, 1, context.value);
        throw std::runtime_error(scenario + ": cancelled owner reused");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::unsupported && source.requested == requested,
               scenario + ": failed owner has no new callbacks");
    }
}
void anchor_limit(const std::filesystem::path& fixture) {
    Context allowed_context;
    VirtualAudioSource allowed_source(fixture, 65536);
    allowed_context.value.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    const auto allowed =
        AudioDecoderSource::open(allowed_source.callbacks(), allowed_context.value, 42, true);
    const auto stats = allowed->index_statistics();
    expect(stats.anchors == 65536 && stats.retained_bytes <= 6 * 1024 * 1024,
           "literal maximum anchor count and bounded allocation");
    VirtualAudioSource source(fixture, 65537);
    Context context;
    context.value.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
    try {
        (void)AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
        throw std::runtime_error("65537 restart anchors admitted");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::too_large &&
                   std::string(error.what()) == "indexed audio restart anchor budget exceeded",
               std::string("literal first-refused anchor count: ") + error.what());
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        expect(argc == 3, "compressed and compact fixture directories");
        high_packet_movie(std::filesystem::path(argv[1]) / "truehd-fine.mkv", true);
        high_packet_movie(std::filesystem::path(argv[2]) / "opus-silence.mkv", false);
        anchor_limit(std::filesystem::path(argv[1]) / "flac-cycle.mkv");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "compact index failure: " << error.what() << '\n';
        return 1;
    }
}
