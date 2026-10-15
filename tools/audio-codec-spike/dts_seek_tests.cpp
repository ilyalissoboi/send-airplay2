// SPDX-License-Identifier: Apache-2.0
#include "virtual_dts_seek_source.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace sap2_test;
void compare_cue_paths(const std::filesystem::path& fixture) {
    VirtualDtsSeekSource reference_source(fixture, true), source(fixture, false);
    expect(source.callbacks().size() > 1073741824, "owned virtual movie exceeds literal 1 GiB");
    Context context;
    auto reference =
        AudioDecoderSource::open(reference_source.callbacks(), context.value, 42, true);
    auto decoder = AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
    (void)reference->read_window(0, 1024, context.value);
    (void)decoder->read_window(0, 1024, context.value);
    for (const auto first : {6240003ULL, 3120003ULL, 96003ULL, 6240003ULL, 6290000ULL}) {
        context.value.deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        reference_source.reset_reads();
        source.reset_reads();
        const auto fetch = [&](AudioDecoderSource& owner, const char* label) {
            try {
                return owner.read_window(first, 4096, context.value);
            } catch (const std::exception& error) {
                throw std::runtime_error(std::string(label) + " at " + std::to_string(first) +
                                         ": " + error.what());
            }
        };
        const auto expected = fetch(*reference, "audio-cued control");
        const auto actual = fetch(*decoder, "video-only cues");
        expect(actual.source_track_id == 42 && actual.channels == 6 &&
                   actual.timestamp_precision_samples == 48 &&
                   actual.samples.size() == (first == 6290000 ? 1456U : 4096U) * 6 &&
                   actual.eof == (first == 6290000) && actual.eof == expected.eof &&
                   actual.samples == expected.samples,
               "DTS video-only cues fixed-offset PCM, start=" + std::to_string(first));
        expect(source.requested < 64ULL * 1024 * 1024,
               "DTS nearby seek read ceiling, start=" + std::to_string(first));
        std::cout << "window=" << first << " requested=" << source.requested
                  << " control_requested=" << reference_source.requested << '\n';
    }
}
void refused_coordinates(const std::filesystem::path& fixture) {
    for (const auto cue :
         {VirtualDtsSeekSource::VideoCues::absent, VirtualDtsSeekSource::VideoCues::late}) {
        VirtualDtsSeekSource source(fixture, false, cue);
        Context context;
        auto decoder = AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
        (void)decoder->read_window(0, 1024, context.value);
        source.reset_reads();
        bool refused = false;
        const bool absent = cue == VirtualDtsSeekSource::VideoCues::absent;
        try {
            (void)decoder->read_window(3120003, 4096, context.value);
        } catch (const AudioDecodeException& error) {
            refused = absent ? error.reason() == AudioDecodeFailure::too_large &&
                                   source.requested == 268435456
                             : error.reason() == AudioDecodeFailure::unsupported &&
                                   std::string(error.what()) ==
                                       "Matroska video coordinate does not cover audio preroll";
        }
        expect(refused, absent ? "missing cues retain literal 256-MiB bound"
                               : "late video coordinate cannot synthesize missing audio");
        const auto calls = source.calls;
        try {
            (void)decoder->read_window(0, 1024, context.value);
            throw std::runtime_error("failed coordinate owner reused");
        } catch (const AudioDecodeException& error) {
            expect(error.reason() == AudioDecodeFailure::unsupported && source.calls == calls,
                   "failed coordinate owner performs no callbacks");
        }
    }
}
void callback_failure(const std::filesystem::path& fixture) {
    VirtualDtsSeekSource source(fixture, false);
    Context context;
    auto decoder = AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
    (void)decoder->read_window(0, 1024, context.value);
    source.reset_reads();
    source.fail_reads = true;
    bool refused = false;
    try {
        (void)decoder->read_window(3120003, 4096, context.value);
    } catch (const AudioDecodeException& error) {
        refused = error.reason() == AudioDecodeFailure::source_error;
    }
    expect(refused && source.calls == 1,
           "video-coordinate source failure has no alternate seek retry");
}
} // namespace
int main(int argc, char** argv) {
    try {
        expect(argc == 2, "DTS seek owned source path");
        av_log_set_level(AV_LOG_QUIET);
        compare_cue_paths(argv[1]);
        refused_coordinates(argv[1]);
        callback_failure(argv[1]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "DTS bounded seek failure: " << error.what() << '\n';
        return 1;
    }
}
