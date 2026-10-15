// SPDX-License-Identifier: Apache-2.0
#include "hls_test_support.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>

namespace {
using namespace send_airplay2;
using namespace send_airplay2::detail;
using namespace sap2_test;

void decoder_windows(const std::filesystem::path& fixture, const std::filesystem::path& baseline) {
    const auto scenario = fixture.filename().string();
    Context context;
    Source original(baseline), source(fixture);
    auto reference = AudioDecoderSource::open(original.callbacks(), context.value, 42, true);
    auto decoder = AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
    (void)reference->read_window(0, 1024, context.value);
    (void)decoder->read_window(0, 1024, context.value);
    for (const auto first : {790000U, 48001U, 287000U, 575000U, 0U, 790000U}) {
        const auto expected = reference->read_window(first, 4096, context.value);
        const auto actual = decoder->read_window(first, 4096, context.value);
        expect(actual.source_track_id == 42 && actual.channels == 6 &&
                   actual.timestamp_precision_samples == 48 &&
                   actual.samples.size() == expected.samples.size() && actual.eof == expected.eof,
               scenario + ": identity/precision/window at " + std::to_string(first));
        expect(actual.samples.size() == (first == 790000 ? 2064 : 4096) * 6,
               scenario + ": literal tail and decoder padding count");
        double squared = 0;
        for (std::size_t sample = 0; sample < actual.samples.size(); ++sample) {
            expect(std::isfinite(actual.samples[sample]), scenario + ": finite PCM");
            const auto difference = actual.samples[sample] - expected.samples[sample];
            squared += difference * difference;
        }
        const auto mse = squared / actual.samples.size();
        expect(mse < 1e-10, scenario + ": fixed-offset PCM MSE=" + std::to_string(mse));
    }
}

void identical_hls(const std::filesystem::path& fixture, const std::filesystem::path& baseline) {
    const auto scenario = fixture.filename().string();
    Context context;
    Source original(baseline), source(fixture);
    Store original_store, store;
    auto reference = StereoAacHls::open(original.callbacks(), original_store.callbacks(),
                                        2 * 1024 * 1024, context.value, 42);
    auto hls = StereoAacHls::open(source.callbacks(), store.callbacks(), 2 * 1024 * 1024,
                                  context.value, 42);
    expect(hls->audio_segment_count() == 3 && store.seals == 0,
           scenario + ": three lazy fragments");
    auto presentation = read(resource(*hls, "audio-init.mp4"), context.value);
    for (const auto index : {2, 0, 1, 2}) {
        const auto name = "a" + std::to_string(index) + ".m4s";
        expect(read(resource(*hls, name), context.value) ==
                   read(resource(*reference, name), context.value),
               scenario + ": identical canonical AAC fragment " + std::to_string(index));
    }
    for (const auto index : {0, 1, 2}) {
        const auto fragment =
            read(resource(*hls, "a" + std::to_string(index) + ".m4s"), context.value);
        presentation.insert(presentation.end(), fragment.begin(), fragment.end());
    }
    expect(MovieDecoder(presentation).decode().size() == 792000 * 2,
           scenario + ": exact video-span AAC EOF trim");
    hls->stop();
    hls->stop();
    expect(store.seals == 3 && store.clears == 1 && store.sealed.empty(),
           scenario + ": bounded cache and one-time cleanup");
}

void discontinuity_refused(const std::filesystem::path& fixture) {
    Context context;
    Source source(fixture);
    auto decoder = AudioDecoderSource::open(source.callbacks(), context.value, 42, true);
    (void)decoder->read_window(0, 1024, context.value);
    bool refused = false;
    try {
        (void)decoder->read_window(48000, 24000, context.value);
    } catch (const AudioDecodeException& error) {
        refused = error.reason() == AudioDecodeFailure::unsupported;
    }
    expect(refused,
           fixture.filename().string() + ": gap between DTS lace blocks remains unsupported");
    Store store;
    refused = false;
    try {
        (void)StereoAacHls::open(source.callbacks(), store.callbacks(), 2 * 1024 * 1024,
                                 context.value, 42);
    } catch (const AudioDecodeException& error) {
        refused = error.reason() == AudioDecodeFailure::unsupported;
    }
    expect(refused && store.seals == 0 && store.clears == 1,
           "DTS lace discontinuity preparation refuses with complete cache cleanup");
}
} // namespace
int main(int argc, char** argv) {
    try {
        expect(argc == 2, "DTS lacing fixture directory");
        const std::filesystem::path fixtures(argv[1]);
        for (const auto* name :
             {"dts-fixed.mkv", "dts-xiph.mkv", "dts-ebml.mkv", "dts-fixed-256.mkv"}) {
            std::cout << "DTS lacing scenario " << name << '\n';
            decoder_windows(fixtures / name, fixtures.parent_path() / "hls/dts.mkv");
            identical_hls(fixtures / name, fixtures.parent_path() / "origin/dts-early-indexed.mkv");
        }
        discontinuity_refused(fixtures / "dts-gap.mkv");
        discontinuity_refused(fixtures / "dts-gap-frame.mkv");
        std::cout << "DTS lace sample clocks, seek phase, AAC bytes and gap refusal passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
