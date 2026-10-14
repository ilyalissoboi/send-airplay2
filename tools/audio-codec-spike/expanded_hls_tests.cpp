// SPDX-License-Identifier: Apache-2.0
#include "hls_test_support.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace send_airplay2;
using namespace send_airplay2::detail;
void converted_renditions(const std::filesystem::path& directory) {
    for (const auto* name :
         {"flac.mkv", "flac.mp4", "opus.mkv", "opus.mp4", "opus-leading-discard.mkv", "vorbis.mkv",
          "truehd.mkv", "pcm-s16le.mkv", "pcm-s24be.mkv", "pcm-s32le.mkv", "pcm-f32le.mkv",
          "pcm-f64le.mkv", "pcm-f64be.mov", "flac-51.mkv", "opus-51.mkv", "vorbis-51.mkv",
          "truehd-51.mkv"}) {
        const std::string scenario(name);
        sap2_test::Context context;
        sap2_test::Source source(directory / name);
        sap2_test::Store store;
        auto hls = StereoAacHls::open(source.callbacks(), store.callbacks(), 2 * 1024 * 1024,
                                      context.value, 42);
        sap2_test::expect(hls->audio_segment_count() == 2, scenario + ": two audio windows");
        const auto late = sap2_test::read(sap2_test::resource(*hls, "a1.m4s"), context.value);
        auto combined = sap2_test::read(sap2_test::resource(*hls, "audio-init.mp4"), context.value);
        const auto first = sap2_test::read(sap2_test::resource(*hls, "a0.m4s"), context.value);
        combined.insert(combined.end(), first.begin(), first.end());
        combined.insert(combined.end(), late.begin(), late.end());
        const auto decoded = sap2_test::MovieDecoder(combined).decode();
        // Thirteen owned 2-fps video frames; shorter audio leaves a silent tail.
        constexpr std::uint32_t video_presentation_frames = 312000;
        sap2_test::expect(decoded.size() == video_presentation_frames * 2,
                          scenario + ": literal generated AAC trim, actual=" +
                              std::to_string(decoded.size() / 2));
        double energy = 0;
        for (std::size_t sample = 96000 * 2; sample < 100096 * 2; ++sample) {
            energy += decoded[sample] * decoded[sample];
        }
        sap2_test::expect(energy / 8192 > 0.0001, scenario + ": missing AAC tone signal");
        sap2_test::expect(sap2_test::read(sap2_test::resource(*hls, "a1.m4s"), context.value) ==
                                  late &&
                              store.seals == 2 && hls->failure() == StereoAacHlsFailure::none,
                          scenario + ": retained late bytes and conversion status");
        hls->stop();
        sap2_test::expect(store.clears == 1 && store.sealed.empty(), scenario + ": cache cleanup");
        std::cout << scenario << ": cold/backward AAC rendition and final trim passed\n";
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        sap2_test::expect(argc == 2, "expanded HLS fixture directory");
        converted_renditions(argv[1]);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "expanded HLS failure: " << error.what() << '\n';
        return 1;
    }
}
