// SPDX-License-Identifier: Apache-2.0
#include "hls_test_support.h"
#include "virtual_hour_source.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <future>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace sap2_test;

template <class Function>
void expect_error(Function action, AudioDecodeFailure category, const std::string& scenario) {
    try {
        action();
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == category, scenario + ": wrong failure category");
        return;
    }
    throw std::runtime_error(scenario + ": unexpectedly succeeded");
}
void boundary_planning() {
    const std::vector<std::uint64_t> normal{0, 287744, 575488, 792000};
    expect(plan_stereo_aac_boundaries({0, 288000, 576000, 792000}) == normal,
           "normal cuts preserve literal AAC grid");
    expect(plan_stereo_aac_boundaries({0, 792000}) == normal,
           "single 16.5 second video GOP gets three audio segments");
    expect(plan_stereo_aac_boundaries({0, 311232, 622752, 792000}) ==
               std::vector<std::uint64_t>({0, 310272, 622592, 792000}),
           "rounding fixture preserves its video-aligned audio cuts");
    expect(plan_stereo_aac_boundaries({0, 1, 100, 1024, 1025, 4097}) ==
               std::vector<std::uint64_t>({0, 1024, 4097}),
           "subframe video cuts do not produce empty audio segments");
    const auto maximum = plan_stereo_aac_boundaries({0, 29030400000ULL});
    expect(maximum.front() == 0 && maximum.back() == 29030400000ULL && maximum.size() == 100801,
           "literal seven-day duration and bounded segment count");
    for (std::size_t index = 1; index < maximum.size(); ++index) {
        expect(maximum[index] > maximum[index - 1] &&
                   maximum[index] - maximum[index - 1] <= 336000 &&
                   (index + 1 == maximum.size() || maximum[index] % 1024 == 0),
               "maximum boundary invariant at " + std::to_string(index));
    }
    for (const auto& invalid :
         std::vector<std::vector<std::uint64_t>>{{},
                                                 {0},
                                                 {1, 2},
                                                 {0, 29030400001ULL},
                                                 {0, std::numeric_limits<std::uint64_t>::max()}}) {
        expect_error([&] { (void)plan_stereo_aac_boundaries(invalid); },
                     AudioDecodeFailure::unsupported, "boundary admission");
    }
    expect_error([] { (void)plan_stereo_aac_boundaries({0, 2048, 2048, 4096}); },
                 AudioDecodeFailure::malformed, "repeated video boundary");
    expect_error([] { (void)plan_stereo_aac_boundaries({0, 4096, 2048}); },
                 AudioDecodeFailure::malformed, "backward video boundary");
    expect_error(
        [] {
            (void)plan_stereo_aac_boundaries(
                {0, std::numeric_limits<std::uint64_t>::max(), 792000});
        },
        AudioDecodeFailure::malformed, "oversized intermediate refused before splitting");
    expect_error([] { (void)plan_stereo_aac_boundaries(std::vector<std::uint64_t>(100803)); },
                 AudioDecodeFailure::unsupported, "boundary count admission");
}
void long_gop(const std::filesystem::path& path) {
    Context context;
    Source source(path);
    Store store;
    auto hls = StereoAacHls::open(source.callbacks(), store.callbacks(), 2 * 1024 * 1024,
                                  context.value, 42);
    expect(hls->presentation().segment_count == 1 && hls->audio_segment_count() == 3,
           "real long GOP copied once with independent audio cuts");
    const RandomReader reader = [&](std::uint64_t offset, std::uint8_t* output, std::size_t count) {
        expect(offset <= source.bytes->size() && count <= source.bytes->size() - offset,
               "long GOP original range");
        std::memcpy(output, source.bytes->data() + offset, count);
    };
    const auto video = read_mp4_video_source(reader, source.bytes->size());
    expect(video.tracks.front().samples.size() == 33 &&
               std::count_if(video.tracks.front().samples.begin(),
                             video.tracks.front().samples.end(),
                             [](const auto& sample) { return sample.sync; }) == 1,
           "encoded 33-frame GOP has one random access frame");
    const auto header = write_segment_header(video, SegmentLayout{1, {{0, 33}}});
    auto expected_video = header;
    for (const auto& packet : video.tracks.front().samples) {
        expected_video.insert(expected_video.end(),
                              source.bytes->begin() + static_cast<std::ptrdiff_t>(packet.offset),
                              source.bytes->begin() +
                                  static_cast<std::ptrdiff_t>(packet.offset + packet.size));
    }
    expect(read(resource(*hls, "s0.m4s"), context.value) == expected_video,
           "long GOP compressed video payload unchanged");
    const auto audio_playlist = text(resource(*hls, "audio.m3u8"), context.value);
    const auto video_playlist = text(resource(*hls, "index.m3u8"), context.value);
    expect(audio_playlist.find("#EXTINF:5.994667,") != std::string::npos &&
               audio_playlist.find("#EXTINF:4.510667,") != std::string::npos &&
               audio_playlist.find("#EXT-X-TARGETDURATION:17\n") != std::string::npos &&
               video_playlist.find("#EXTINF:16.500000,") != std::string::npos &&
               video_playlist.find("#EXT-X-TARGETDURATION:17\n") != std::string::npos,
           "literal independent cuts retain shared target and exact end");
    auto movie = read(resource(*hls, "audio-init.mp4"), context.value);
    for (const auto* name : {"a0.m4s", "a1.m4s", "a2.m4s"}) {
        const auto bytes = read(resource(*hls, name), context.value);
        movie.insert(movie.end(), bytes.begin(), bytes.end());
    }
    expect(MovieDecoder(movie).decode().size() == 1584000,
           "long GOP split audio independent MOV decode and exact 16.5 second end");
}
void terminal_cache_failure(const std::filesystem::path& path) {
    for (int mode = 0; mode < 3; ++mode) {
        Context context;
        Source source(path);
        Store store;
        auto callbacks = store.callbacks();
        const auto ordinary_read = callbacks.read_at;
        std::atomic_bool broken{false};
        callbacks.read_at = [&](std::size_t index, std::uint64_t offset, std::uint8_t* output,
                                std::size_t capacity, const AacCacheContext& request) {
            if (!broken) {
                return ordinary_read(index, offset, output, capacity, request);
            }
            if (mode == 0) {
                throw std::runtime_error("private host path must not escape");
            }
            return mode == 1 ? std::size_t{0} : capacity + 1;
        };
        auto hls =
            StereoAacHls::open(source.callbacks(), callbacks, 2 * 1024 * 1024, context.value, 42);
        const auto sealed = resource(*hls, "a0.m4s");
        const auto pending = resource(*hls, "a1.m4s");
        const auto unused = resource(*hls, "a2.m4s");
        expect(!read(sealed, context.value).empty(), "cache failure first serves valid bytes");
        source.block = true;
        auto waiter = std::async(std::launch::async, [&] {
            expect_error([&] { (void)media_resource_size(pending, context.value); },
                         AudioDecodeFailure::source_error, "fatal read cancels shared generation");
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!source.entered && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        if (!source.entered) {
            hls->stop(); // Drain the future before reporting a failed setup.
        }
        expect(source.entered, "fatal cache test enters cooperative generation");
        broken = true;
        std::uint8_t byte = 0;
        try {
            (void)sealed.source.read_at(0, &byte, 1, context.value);
            throw std::logic_error("fatal read succeeded");
        } catch (const AudioDecodeException& error) {
            expect(error.reason() == AudioDecodeFailure::source_error &&
                       std::string(error.what()) == "AAC cache failure",
                   "cache failure fixed diagnostic mode " + std::to_string(mode));
        }
        waiter.get();
        for (const auto& retained : {sealed, pending, unused}) {
            expect_error([&] { (void)media_resource_size(retained, context.value); },
                         AudioDecodeFailure::source_error, "all retained audio refuses fatal cast");
        }
        expect(hls->failure() == StereoAacHlsFailure::cache_error && store.seals == 1,
               "first failure cancels worker and preserves category");
        hls->stop();
        hls->stop();
        expect(hls->failure() == StereoAacHlsFailure::cache_error && store.clears == 1 &&
                   store.sealed.empty(),
               "fatal Stop clears once and retains category");
    }
}
void cancelled_cache_read(const std::filesystem::path& path) {
    Source source(path);
    Store store;
    Context context;
    Context cancelled;
    auto callbacks = store.callbacks();
    const auto ordinary_read = callbacks.read_at;
    bool cancel_read = false;
    callbacks.read_at = [&](std::size_t index, std::uint64_t offset, std::uint8_t* output,
                            std::size_t capacity, const AacCacheContext& request) {
        if (cancel_read) {
            cancelled.cancelled = true;
            throw std::runtime_error("request stopped by host");
        }
        return ordinary_read(index, offset, output, capacity, request);
    };
    auto hls =
        StereoAacHls::open(source.callbacks(), callbacks, 2 * 1024 * 1024, context.value, 42);
    const auto audio = resource(*hls, "a0.m4s");
    expect(!read(audio, context.value).empty(), "cancelled cache test seals first");
    cancel_read = true;
    expect_error([&] { (void)read(audio, cancelled.value); }, AudioDecodeFailure::stopped,
                 "cache callback request cancellation");
    cancel_read = false;
    expect(hls->failure() == StereoAacHlsFailure::none && !read(audio, context.value).empty(),
           "cancelled cache read leaves shared cast healthy");
}
void terminal_generation_failure(const std::filesystem::path& path) {
    for (const bool sealing : {false, true}) {
        Source source(path);
        Store store;
        Context context;
        std::atomic_bool broken{false};
        auto callbacks = source.callbacks();
        const auto ordinary_read = callbacks.read_at;
        callbacks.read_at = [&](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
                                const MediaReadContext& request) {
            if (broken) {
                throw std::runtime_error("private source detail must not escape");
            }
            return ordinary_read(offset, output, capacity, request);
        };
        auto hls =
            StereoAacHls::open(callbacks, store.callbacks(), 2 * 1024 * 1024, context.value, 42);
        const auto earlier = resource(*hls, "a0.m4s");
        expect(!read(earlier, context.value).empty(), "generation failure retains earlier cache");
        if (sealing) {
            store.fail_seal = true;
        } else {
            broken = true;
        }
        expect_error([&] { (void)read(resource(*hls, "a2.m4s"), context.value); },
                     AudioDecodeFailure::source_error, "generation failure reported");
        expect_error([&] { (void)read(earlier, context.value); }, AudioDecodeFailure::source_error,
                     "generation failure refuses sealed audio");
        expect(hls->failure() ==
                   (sealing ? StereoAacHlsFailure::cache_error : StereoAacHlsFailure::source_error),
               "generation first fixed failure category");
        hls->stop();
        expect(store.clears == 1 && store.seals == 1, "generation failure owns cleanup once");
    }
}
void hour_cold_seeks(const std::filesystem::path& path) {
    Context context;
    VirtualHourSource source(path);
    Store store;
    const auto start = std::chrono::steady_clock::now();
    auto hls = StereoAacHls::open(source.callbacks(), store.callbacks(), 256 * 1024 * 1024,
                                  context.value, 42);
    const auto startup = std::chrono::steady_clock::now() - start;
    expect(hls->presentation().segment_count == 600 && hls->audio_segment_count() == 600 &&
               store.seals == 0 && source.size_calls == 1,
           "one-hour metadata startup does not eagerly convert audio");
    expect(source.metadata_bytes <= 32 * 1024 * 1024 && source.payload_bytes <= 1024 * 1024,
           "one-hour startup bounded I/O metadata=" + std::to_string(source.metadata_bytes) +
               " payload=" + std::to_string(source.payload_bytes));
    auto init = read(resource(*hls, "audio-init.mp4"), context.value);
    // Literal source-clock expectations, independently of the planner output.
    const std::uint64_t begin_samples[] = {172511232, 86400000};
    const std::uint64_t lengths[] = {288768, 287744};
    std::uint64_t maximum_read = 0;
    std::int64_t maximum_generation_ms = 0;
    for (std::size_t index = 0; index < 2; ++index) {
        const auto before = source.payload_bytes.load();
        const auto generation_start = std::chrono::steady_clock::now();
        const auto fragment =
            read(resource(*hls, index == 0 ? "a599.m4s" : "a300.m4s"), context.value);
        const auto consumed = source.payload_bytes.load() - before;
        maximum_generation_ms = std::max<std::int64_t>(
            maximum_generation_ms, std::chrono::duration_cast<std::chrono::milliseconds>(
                                       std::chrono::steady_clock::now() - generation_start)
                                       .count());
        maximum_read = std::max(maximum_read, consumed);
        expect(consumed > 0 && consumed <= 4 * 1024 * 1024,
               "one-hour cold seek bounded source bytes=" + std::to_string(consumed));
        auto movie = init;
        movie.insert(movie.end(), fragment.begin(), fragment.end());
        const auto decoded =
            MovieDecoder(movie, static_cast<std::int64_t>(begin_samples[index])).decode();
        expect(decoded.size() == lengths[index] * 2,
               "one-hour cold MOV decode global PTS and exact end");
        double energy = 0;
        for (const auto sample : decoded) {
            expect(std::isfinite(sample) && std::abs(sample) < 1.0F,
                   "one-hour finite AAC headroom");
            energy += sample * sample;
        }
        expect(energy / decoded.size() > 0.00001, "one-hour cold audio is audible");
    }
    expect(store.seals == 2 && source.largest_offset > std::numeric_limits<std::uint32_t>::max(),
           "cold seeks beyond 4 GiB seal only requested fragments");
    std::cout << "synthetic hour startup_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(startup).count()
              << " metadata_bytes=" << source.metadata_bytes
              << " maximum_cold_read_bytes=" << maximum_read
              << " maximum_generation_ms=" << maximum_generation_ms
              << " largest_offset=" << source.largest_offset << '\n';
}
void queue_admission(const std::filesystem::path& path) {
    Context context;
    VirtualHourSource source(path);
    Store store;
    auto hls = StereoAacHls::open(source.callbacks(), store.callbacks(), 256 * 1024 * 1024,
                                  context.value, 42);
    source.block = true;
    Context cancelled;
    auto first = std::async(std::launch::async, [&] {
        expect_error([&] { (void)media_resource_size(resource(*hls, "a0.m4s"), cancelled.value); },
                     AudioDecodeFailure::stopped, "generating job waiter cancelled");
    });
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!source.entered && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    cancelled.cancelled = true;
    first.get();
    if (!source.entered) {
        hls->stop();
    }
    expect(source.entered, "queue fixture blocked cooperative worker");
    for (int index = 1; index < 5; ++index) {
        Context expired;
        expired.value.deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(30);
        expect_error(
            [&] {
                (void)media_resource_size(resource(*hls, "a" + std::to_string(index) + ".m4s"),
                                          expired.value);
            },
            AudioDecodeFailure::stopped, "four pending jobs admit before request expiry");
    }
    expect_error([&] { (void)media_resource_size(resource(*hls, "a5.m4s"), context.value); },
                 AudioDecodeFailure::too_large, "fifth pending job refused");
    expect(hls->failure() == StereoAacHlsFailure::none, "queue admission is request local");
    hls->stop();
    expect(store.clears == 1 && store.seals == 0 && hls->failure() == StereoAacHlsFailure::none,
           "queue Stop cancels all work without fatal failure");
}
} // namespace
int main(int argc, char** argv) {
    try {
        expect(argc == 2, "readiness fixture directory");
        const std::filesystem::path fixtures(argv[1]);
        boundary_planning();
        long_gop(fixtures / "aac-long-gop.mp4");
        terminal_cache_failure(fixtures / "dts.mp4");
        cancelled_cache_read(fixtures / "dts.mp4");
        terminal_generation_failure(fixtures / "dts.mp4");
        hour_cold_seeks(fixtures / "dts.mp4");
        queue_admission(fixtures / "dts.mp4");
        std::cout << "AAC readiness boundaries, terminal failures and synthetic scaling passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
