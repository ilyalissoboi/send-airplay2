// SPDX-License-Identifier: Apache-2.0
#include "file_media_source.h"
#include "mp3_audio.h"
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cerr << "Usage: airplay2-mp3-probe FILE (private framing inventory, no receiver)\n";
        return 2;
    }
    try {
        std::atomic_bool stopped{false}, cancelled{false};
        const send_airplay2::MediaReadContext context{
            &stopped, &cancelled, std::chrono::steady_clock::now() + std::chrono::seconds(30)};
        const auto source = send_airplay2::detail::open_file_media_source(
            std::filesystem::u8path(argv[1]),
            std::make_shared<send_airplay2::detail::FileReadStats>());
        const auto info = send_airplay2::detail::probe_mp3_audio(source, context);
        std::cout << "MP3: rate=" << info.sample_rate << " channels=" << info.channels
                  << " frames=" << info.frame_count << " coded_samples=" << info.coded_samples
                  << " xing=" << (info.xing ? "yes" : "no")
                  << " variable=" << (info.variable_bitrate ? "yes" : "no")
                  << " anchors=" << info.seek_points.size() << " audible_samples=";
        if (info.audible_samples) {
            std::cout << *info.audible_samples;
        } else {
            std::cout << "unknown";
        }
        std::cout << '\n';
        return 0;
    } catch (const std::exception&) {
        // Do not echo paths, labels or host exception strings.
        std::cerr << "MP3 probe refused input or source read\n";
        return 1;
    }
}
