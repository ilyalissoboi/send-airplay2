// SPDX-License-Identifier: Apache-2.0
#include "audio_decoder_source.h"
#include "media_track_inventory.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <memory>
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
struct Source {
    std::vector<std::uint8_t> bytes;
    std::atomic<bool> stop{false};
    std::size_t short_read = 1021;
    std::size_t size_calls = 0;
    std::size_t reads = 0;
    std::size_t cancel_at_read = 0;
    bool zero_read = false;
    bool oversized_read = false;
    bool throw_read = false;
    const MediaReadContext* expected_context = nullptr;

    explicit Source(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        expect(input.good(), "missing decoder fixture: " + path.filename().string());
        bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    MediaSource callbacks() {
        return {[this] {
                    ++size_calls;
                    return static_cast<std::uint64_t>(bytes.size());
                },
                [this](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
                       const MediaReadContext& context) {
                    expect(&context == expected_context,
                           "callback did not receive current operation context");
                    expect(capacity <= 65536 && offset < bytes.size(), "callback read bounds");
                    ++reads;
                    if (cancel_at_read && reads >= cancel_at_read) {
                        stop.store(true);
                    }
                    if (throw_read) {
                        throw std::logic_error("owned callback failure");
                    }
                    if (oversized_read) {
                        return capacity + 1;
                    }
                    if (zero_read) {
                        return std::size_t{0};
                    }
                    const auto count = std::min(
                        {capacity, short_read, bytes.size() - static_cast<std::size_t>(offset)});
                    std::memcpy(output, bytes.data() + offset, count);
                    return count;
                }};
    }
    MediaReadContext context() {
        return {&stop, &stop, std::chrono::steady_clock::now() + std::chrono::seconds(30)};
    }
};
std::unique_ptr<AudioDecoderSource> open(Source& source, const MediaReadContext& context,
                                         std::optional<std::uint64_t> id = 42) {
    source.expected_context = &context;
    return AudioDecoderSource::open(source.callbacks(), context, id);
}
DecodedAudioWindow window(AudioDecoderSource& decoder, Source& source, std::uint64_t start,
                          std::uint32_t count) {
    const auto context = source.context();
    source.expected_context = &context;
    return decoder.read_window(start, count, context);
}
template <typename Call>
void refused(Call call, AudioDecodeFailure expected, const std::string& scenario) {
    try {
        call();
        throw std::runtime_error(scenario + ": unexpectedly accepted");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == expected, scenario + ": wrong failure category");
    }
}
double energy(const DecodedAudioWindow& value) {
    double sum = 0;
    for (const auto sample : value.samples) {
        expect(std::isfinite(sample), "nonfinite decoded sample");
        sum += static_cast<double>(sample) * sample;
    }
    return value.samples.empty() ? 0 : sum / value.samples.size();
}

/// Compare seek output to independently continued decoding of the same compressed
/// source. Permit only the reported container-clock quantum, not an arbitrary lag.
void compare_window(const DecodedAudioWindow& reference, const DecodedAudioWindow& actual,
                    const std::string& scenario) {
    expect(actual.source_track_id == 42 && actual.channels == 6,
           scenario + ": selected identity/channel count");
    expect(!actual.samples.empty() && energy(actual) > 0.0001, scenario + ": missing signal");
    const auto frames = actual.samples.size() / actual.channels;
    const auto tolerance = static_cast<int>(actual.timestamp_precision_samples) + 1;
    double best_error = std::numeric_limits<double>::infinity();
    int best_lag = 0;
    for (int lag = -tolerance; lag <= tolerance; ++lag) {
        const auto start = static_cast<std::int64_t>(actual.first_sample) + lag;
        if (start < 0 || static_cast<std::uint64_t>(start) + frames >
                             reference.samples.size() / reference.channels) {
            continue;
        }
        double difference = 0;
        for (std::size_t index = 0; index < actual.samples.size(); ++index) {
            const auto error =
                static_cast<double>(actual.samples[index]) -
                reference.samples[static_cast<std::size_t>(start) * actual.channels + index];
            difference += error * error;
        }
        difference /= actual.samples.size();
        if (difference < best_error) {
            best_error = difference;
            best_lag = lag;
        }
    }
    expect(best_error < 0.000001,
           scenario + ": seek PCM mismatch, MSE=" + std::to_string(best_error) + ", lag=" +
               std::to_string(best_lag) + ", clock samples=" + std::to_string(tolerance));
}
void selected_streams_and_seeking(const std::filesystem::path& directory) {
    for (const auto* name :
         {"aac-commentary-first.mp4", "aac-main-first.mp4", "aac-44100.mp4",
          "dts-commentary-first.mp4", "ac3-commentary-first.mp4", "eac3-commentary-first.mp4",
          "dts-commentary-first.mkv", "dts-main-first.mkv", "dts-delay.mkv", "dts-spoofed-tags.mkv",
          "ac3-commentary-first.mkv", "eac3-commentary-first.mkv"}) {
        Source source(directory / name);
        const auto context = source.context();
        auto decoder = open(source, context);
        const auto reference = window(*decoder, source, 0, 160000);
        expect(reference.eof && reference.source_track_id == 42 && reference.channels == 6,
               std::string(name) + ": full decode identity/EOF");
        expect(reference.samples.size() / 6 >= 140000 && reference.samples.size() / 6 <= 150000,
               std::string(name) + ": full decoded presentation duration");
        expect(reference.timestamp_precision_samples ==
                   (std::string(name).find(".mkv") != std::string::npos ? 48U
                    : std::string(name) == "aac-44100.mp4"              ? 2U
                                                                        : 1U),
               std::string(name) + ": declared timestamp precision");
        // Reuse after EOF, seek late first, backward, then forward again.
        for (const auto start : {110400U, 19200U, 60000U}) {
            const auto actual = window(*decoder, source, start, 4800);
            expect(actual.samples.size() == 4800 * 6 && !actual.eof,
                   std::string(name) + ": bounded window length, start=" + std::to_string(start));
            compare_window(reference, actual,
                           std::string(name) + ": start=" + std::to_string(start));
        }
        const auto past_end = window(*decoder, source, 192000, 4800);
        expect(past_end.eof && past_end.samples.empty(),
               std::string(name) + ": no fabricated EOF tail");
        expect(source.size_calls == 1, std::string(name) + ": host size called more than once");
        std::cout << name << ": selected stream, forward/backward seek, EOF passed\n";
    }
}
void selection_failures(const std::filesystem::path& directory) {
    for (const auto id : {std::uint64_t{0}, std::uint64_t{11}, std::uint64_t{999}}) {
        Source source(directory / "aac-commentary-first.mp4");
        const auto context = source.context();
        refused([&] { (void)open(source, context, id); }, AudioDecodeFailure::invalid_selection,
                "missing/non-audio identity=" + std::to_string(id));
    }
    {
        Source source(directory / "aac-commentary-first.mp4");
        const auto context = source.context();
        refused([&] { (void)open(source, context, {}); }, AudioDecodeFailure::selection_required,
                "ambiguous MP4 must not pick commentary");
    }
    for (const auto* name : {"aac-disabled.mp4", "aac-unknown.mp4", "aac-protected.mp4"}) {
        Source source(directory / name);
        const auto context = source.context();
        refused([&] { (void)open(source, context); },
                std::string(name) == "aac-disabled.mp4" ? AudioDecodeFailure::invalid_selection
                                                        : AudioDecodeFailure::unsupported,
                std::string(name) + ": no fallback to AAC commentary");
    }
    {
        Source source(directory / "dts-commentary-first.mkv");
        const auto context = source.context();
        auto decoder = open(source, context, {});
        expect(window(*decoder, source, 24000, 4800).source_track_id == 42,
               "Matroska unique default selects DTS before codec filtering");
    }
    {
        Source source(directory / "aac-commentary-first.mp4");
        const auto context = source.context();
        auto decoder = open(source, context, 7);
        const auto commentary = window(*decoder, source, 24000, 4800);
        expect(commentary.channels == 2 && commentary.source_track_id == 7 &&
                   energy(commentary) > 0.0001,
               "explicit AAC commentary remains distinct from main audio");
    }
}
void edited_presentation(const std::filesystem::path& directory) {
    Source source(directory / "aac-edits.mp4");
    const auto context = source.context();
    auto decoder = open(source, context);
    const auto reference = window(*decoder, source, 0, 160000);
    expect(reference.eof && reference.samples.size() / 6 >= 131000 &&
               reference.samples.size() / 6 <= 133000,
           "edited presentation duration includes initial silence: frames=" +
               std::to_string(reference.samples.size() / 6) +
               ", eof=" + std::to_string(reference.eof));
    expect(std::all_of(reference.samples.begin(), reference.samples.begin() + 12000 * 6,
                       [](float sample) { return sample == 0; }),
           "initial 250 ms edit silence");
    expect(energy(window(*decoder, source, 0, 4800)) == 0, "seek inside initial empty edit");
    for (const auto start : {72000U, 14400U}) {
        compare_window(reference, window(*decoder, source, start, 4800),
                       "MP4 empty/media edits applied once, start=" + std::to_string(start));
    }
    Source original(directory / "aac-commentary-first.mp4");
    const auto original_context = original.context();
    auto original_decoder = open(original, original_context);
    const auto unedited = window(*original_decoder, original, 0, 160000);
    auto edited = window(*decoder, source, 24000, 4800);
    // 250 ms silence and 500 ms media offset: presentation 500 ms is source 750 ms.
    edited.first_sample = 36000;
    compare_window(unedited, edited, "MP4 edit offset against unedited compressed source");
}
void identity_and_delay_regressions(const std::filesystem::path& directory) {
    {
        Source source(directory / "dts-large-id.mkv");
        const auto context = source.context();
        constexpr std::uint64_t large_id = 0x10000002A;
        auto decoder = open(source, context, large_id);
        const auto actual = window(*decoder, source, 24000, 4800);
        expect(actual.source_track_id == large_id && actual.channels == 6 &&
                   energy(actual) > 0.0001,
               "Matroska TrackNumber above uint32 is retained without ordinal substitution");
    }
    Source original(directory / "dts-commentary-first.mkv");
    const auto original_context = original.context();
    auto original_decoder = open(original, original_context);
    const auto reference = window(*original_decoder, original, 0, 160000);
    Source delayed(directory / "dts-delay.mkv");
    const auto delayed_context = delayed.context();
    auto delayed_decoder = open(delayed, delayed_context);
    compare_window(reference, window(*delayed_decoder, delayed, 24000, 4800),
                   "Matroska stored +20 ms and CodecDelay -20 ms cancel exactly once");
}
void format_changes_and_request_bounds(const std::filesystem::path& directory) {
    for (const auto start : {0U, 110400U}) {
        Source source(directory / "ac3-format-change.mkv");
        const auto context = source.context();
        auto decoder = open(source, context);
        refused([&] { (void)window(*decoder, source, start, start ? 4800 : 160000); },
                AudioDecodeFailure::unsupported,
                "AC-3 layout change must fail, start=" + std::to_string(start));
    }
    Source source(directory / "aac-commentary-first.mp4");
    const auto context = source.context();
    auto decoder = open(source, context);
    const auto reads = source.reads;
    refused(
        [&] { (void)window(*decoder, source, std::numeric_limits<std::uint64_t>::max(), 4800); },
        AudioDecodeFailure::unsupported, "overflow-sized presentation target");
    refused([&] { (void)window(*decoder, source, 0, 480001); }, AudioDecodeFailure::unsupported,
            "window exceeds ten seconds");
    expect(source.reads == reads, "invalid window request performed I/O");
    expect(!window(*decoder, source, 24000, 4800).samples.empty(),
           "invalid request does not poison a valid decoder");
}
void lifecycle_failures(const std::filesystem::path& directory) {
    {
        Source source(directory / "aac-commentary-first.mp4");
        const auto context = source.context();
        source.stop.store(true);
        refused([&] { (void)open(source, context); }, AudioDecodeFailure::stopped,
                "pre-open cancellation");
        expect(source.size_calls == 0 && source.reads == 0, "cancelled open invoked callbacks");
    }
    for (unsigned mode = 0; mode < 5; ++mode) {
        Source source(directory / "dts-commentary-first.mkv");
        auto context = source.context();
        auto decoder = open(source, context);
        (void)window(*decoder, source, 24000, 4800);
        context = source.context();
        source.expected_context = &context;
        if (mode == 0) {
            source.cancel_at_read = source.reads + 2;
        } else if (mode == 1) {
            source.zero_read = true;
        } else if (mode == 2) {
            source.oversized_read = true;
        } else if (mode == 3) {
            context.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        } else {
            source.throw_read = true;
        }
        if (mode == 4) {
            try {
                (void)decoder->read_window(110400, 4800, context);
                throw std::runtime_error("callback exception unexpectedly accepted");
            } catch (const std::logic_error& error) {
                expect(std::string(error.what()) == "owned callback failure",
                       "callback exception propagation");
            }
        } else {
            refused([&] { (void)decoder->read_window(110400, 4800, context); },
                    mode == 0 || mode == 3 ? AudioDecodeFailure::stopped
                                           : AudioDecodeFailure::source_error,
                    "window lifecycle failure mode=" + std::to_string(mode));
        }
        source.stop.store(false);
        const auto calls_after_failure = source.reads;
        refused([&] { (void)window(*decoder, source, 0, 4800); }, AudioDecodeFailure::unsupported,
                "failed decoder cannot be reused");
        expect(source.reads == calls_after_failure, "failed decoder performed callbacks");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        expect(argc == 2, "decoder fixture directory argument");
        const std::filesystem::path directory(argv[1]);
        selected_streams_and_seeking(directory);
        selection_failures(directory);
        edited_presentation(directory);
        identity_and_delay_regressions(directory);
        format_changes_and_request_bounds(directory);
        lifecycle_failures(directory);
        std::cout << "decoder source checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "decoder source failure: " << error.what() << '\n';
        return 1;
    }
}
