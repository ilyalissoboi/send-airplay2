// SPDX-License-Identifier: Apache-2.0
#include "hls_test_support.h"
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
using namespace send_airplay2;
using namespace send_airplay2::detail;
using sap2_test::expect;
constexpr std::uint32_t full_window = 336000;

void compare(const DecodedAudioWindow& reference, const DecodedAudioWindow& actual,
             const std::string& scenario) {
    expect(actual.channels == reference.channels && actual.source_track_id == 42,
           scenario + ": selected layout/identity");
    const auto offset = actual.first_sample * actual.channels;
    expect(offset + actual.samples.size() <= reference.samples.size(), scenario + ": PCM bounds");
    double squared_error = 0;
    for (std::size_t index = 0; index < actual.samples.size(); ++index) {
        expect(std::isfinite(actual.samples[index]), scenario + ": nonfinite output");
        const auto difference = actual.samples[index] - reference.samples[offset + index];
        squared_error += difference * difference;
    }
    expect(!actual.samples.empty() && squared_error / actual.samples.size() < 1e-10,
           scenario + ": phase/sample mismatch, MSE=" +
               std::to_string(squared_error / actual.samples.size()));
}

void windows(const std::filesystem::path& directory, const std::string& name,
             const std::filesystem::path& fine_path) {
    sap2_test::Context context;
    sap2_test::Source fine(fine_path);
    auto fine_decoder = AudioDecoderSource::open(fine.callbacks(), context.value, 42, true);
    const auto reference = fine_decoder->read_window(0, full_window, context.value);
    expect(reference.eof && reference.samples.size() == 288144 * reference.channels,
           name + ": independent original audible length");
    sap2_test::Source coarse(directory / name);
    auto decoder = AudioDecoderSource::open(coarse.callbacks(), context.value, 42, true);
    expect(decoder->requires_start_rebuild(), name + ": bounded reconstruction selected");
    // First request is cold and late. The next pair meets at a deliberately
    // non-frame-aligned boundary, then requests reverse order and the EOF tail.
    for (const auto start : {270000U, 90001U, 94097U, 160003U, 48001U, 270000U}) {
        const auto actual = decoder->read_window(start, 4096, context.value);
        expect(actual.samples.size() == 4096 * actual.channels &&
                   actual.timestamp_precision_samples == 48,
               name + ": bounded window length/absolute clock precision");
        compare(reference, actual, name + ": start=" + std::to_string(start));
    }
    const auto tail = decoder->read_window(288000, 4096, context.value);
    expect(tail.eof && tail.samples.size() == 144 * tail.channels, name + ": literal tail trim");
    compare(reference, tail, name + ": reused EOF tail");
    sap2_test::Source cold(directory / name);
    auto cold_decoder = AudioDecoderSource::open(cold.callbacks(), context.value, 42, true);
    const auto cold_tail = cold_decoder->read_window(288000, 4096, context.value);
    expect(cold_tail.eof && cold_tail.samples.size() == 144 * cold_tail.channels,
           name + ": cold EOF trim");
    compare(reference, cold_tail, name + ": cold EOF tail");
    compare(reference, decoder->read_window(0, full_window, context.value),
            name + ": full rebuild");
    if (name == "opus.mkv") {
        const auto beginning = decoder->read_window(0, 840, context.value);
        double energy = 0;
        for (const auto sample : beginning.samples) {
            energy += sample * sample;
        }
        expect(energy / beginning.samples.size() > 0.001,
               "Opus negative-time priming packet preserves first audible samples");
    }
}

std::vector<Bytes> audio_resources(const std::filesystem::path& path) {
    sap2_test::Context context;
    sap2_test::Source source(path);
    sap2_test::Store store;
    auto hls = StereoAacHls::open(source.callbacks(), store.callbacks(), 2 * 1024 * 1024,
                                  context.value, 42);
    expect(hls->audio_segment_count() == 2, path.filename().string() + ": segment count");
    // Cold final segment followed by backward generation of the first.
    const auto late = sap2_test::read(sap2_test::resource(*hls, "a1.m4s"), context.value);
    const auto first = sap2_test::read(sap2_test::resource(*hls, "a0.m4s"), context.value);
    auto init = sap2_test::read(sap2_test::resource(*hls, "audio-init.mp4"), context.value);
    auto combined = init;
    combined.insert(combined.end(), first.begin(), first.end());
    combined.insert(combined.end(), late.begin(), late.end());
    const auto decoded = sap2_test::MovieDecoder(combined).decode();
    expect(decoded.size() == 312000 * 2, path.filename().string() + ": literal AAC final trim");
    expect(sap2_test::read(sap2_test::resource(*hls, "a1.m4s"), context.value) == late &&
               store.seals == 2 && hls->failure() == StereoAacHlsFailure::none,
           path.filename().string() + ": sealed cache/status");
    hls->stop();
    expect(store.clears == 1 && store.sealed.empty(), "coarse HLS storage cleanup");
    return {init, first, late};
}

void decoder_horizon(const std::filesystem::path& directory) {
    sap2_test::Context context;
    sap2_test::Source source(directory / "pcm-s16le.mkv");
    auto decoder = AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
    const auto boundary = decoder->read_window(479999, 1, context.value);
    expect(boundary.eof && boundary.channels == 2 && boundary.samples.empty(),
           "literal ten-second boundary permits a post-EOF window");
    const auto calls = source.calls.load();
    try {
        (void)decoder->read_window(480000, 1, context.value);
        throw std::runtime_error("reconstruction horizon accepted");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::unsupported && source.calls == calls,
               "literal ten-second horizon refuses before I/O");
    }
    try {
        (void)decoder->read_window(0, 1, context.value);
        throw std::runtime_error("failed reconstruction decoder reused");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::unsupported && source.calls == calls,
               "failed decoder remains unusable");
    }
}
void presentation_horizon(const std::filesystem::path& directory) {
    sap2_test::Context context;
    sap2_test::Source long_video(directory / "over-limit-video.mkv");
    sap2_test::Store store;
    try {
        (void)StereoAacHls::open(long_video.callbacks(), store.callbacks(), 2 * 1024 * 1024,
                                 context.value, 42);
        throw std::runtime_error("long coarse HLS registered");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::unsupported &&
                   std::string(error.what()) ==
                       "coarse Matroska HLS exceeds bounded start reconstruction" &&
                   store.seals == 0 && store.clears == 1,
               "long coarse presentation fails before sealing/registration and cleans store");
    }
    // The bound belongs only to the coarse reconstruction path. The same video
    // and selected audio under a sample clock still prepare successfully.
    sap2_test::Source long_fine(directory / "long-video-fine.mkv");
    sap2_test::Store fine_store;
    auto fine_hls = StereoAacHls::open(long_fine.callbacks(), fine_store.callbacks(),
                                       2 * 1024 * 1024, context.value, 42);
    expect(fine_hls->audio_segment_count() == 2, "sample-clock long control remains accepted");
    fine_hls->stop();
}
void discontinuity(const std::filesystem::path& directory) {
    sap2_test::Context context;
    sap2_test::Source discontinuous(directory / "discontinuous.mkv");
    auto gapped = AudioDecoderSource::open(discontinuous.callbacks(), context.value, 42, true);
    try {
        (void)gapped->read_window(270000, 4096, context.value);
        throw std::runtime_error("discontinuous reconstruction accepted");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::unsupported &&
                   std::string(error.what()) ==
                       "Matroska timestamp disagrees with rebuilt sample clock",
               "coarse reconstruction checks observed timestamps instead of concealing gaps");
    }
}
void rebuild_cancellation(const std::filesystem::path& directory) {
    sap2_test::Context context;
    sap2_test::Source source(directory / "pcm-s16le.mkv");
    std::size_t window_reads = 0;
    bool cancel_reads = false;
    auto callbacks = source.callbacks();
    const auto read_at = callbacks.read_at;
    callbacks.read_at = [&](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
                            const MediaReadContext& request) {
        if (cancel_reads && ++window_reads == 20) {
            context.cancelled = true;
        }
        return read_at(offset, output, capacity, request);
    };
    auto cancelled = AudioDecoderSource::open(callbacks, context.value, 42, true);
    cancel_reads = true;
    try {
        (void)cancelled->read_window(270000, 4096, context.value);
        throw std::runtime_error("cancelled start reconstruction completed");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::stopped && window_reads == 20,
               "mid-rebuild cancellation observes callback boundary");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        expect(argc == 2, "coarse-clock fixture directory");
        const std::filesystem::path directory(argv[1]);
        for (const auto* name :
             {"flac.mkv", "opus.mkv", "vorbis.mkv", "truehd.mkv", "pcm-s16le.mkv", "pcm-s24be.mkv",
              "pcm-f64le.mkv", "opus-leading-discard.mkv", "pcm-leading-discard.mkv", "flac-51.mkv",
              "opus-51.mkv", "vorbis-51.mkv", "truehd-51.mkv", "pcm-variable-44100.mkv"}) {
            const auto fine = std::string(name) == "pcm-variable-44100.mkv"
                                  ? directory / "pcm-variable-44100-fine.mkv"
                                  : directory.parent_path() / "expanded-decoder" / name;
            windows(directory, name, fine);
            expect(audio_resources(directory / name) == audio_resources(fine),
                   std::string(name) + ": byte-identical AAC joins against sample-clock control");
            std::cout << name << ": cold/reused windows, phase and AAC joins passed\n";
        }
        decoder_horizon(directory);
        presentation_horizon(directory);
        discontinuity(directory);
        rebuild_cancellation(directory);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "coarse-clock failure: " << error.what() << '\n';
        return 1;
    }
}
