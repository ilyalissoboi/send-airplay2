// SPDX-License-Identifier: Apache-2.0
#include "hls_test_support.h"
#include "mkv_demux.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace {
using namespace send_airplay2;
using namespace send_airplay2::detail;
using namespace sap2_test;

RandomReader reader(const Source& source) {
    return [&source](std::uint64_t offset, std::uint8_t* output, std::size_t count) {
        expect(offset <= source.bytes->size() && count <= source.bytes->size() - offset,
               "origin fixture read range");
        std::memcpy(output, source.bytes->data() + offset, count);
    };
}

void video_and_captions(Source& source, const StereoAacHls& hls, const MediaReadContext& context,
                        std::uint64_t origin_ms, bool indexed, const std::string& scenario) {
    auto index = MkvIndex::open_video_source(reader(source), source.bytes->size());
    expect(static_cast<bool>(index) == indexed, scenario + ": literal index path");
    auto movie = read_mkv_video_source(reader(source), source.bytes->size());
    auto& video = movie.tracks.front();
    expect(video.timescale == 1000 && video.samples.front().decode_time == origin_ms,
           scenario + ": literal native video origin");
    const auto segments = plan_segments(movie);
    auto zero_based = movie;
    for (auto& sample : zero_based.tracks.front().samples) {
        sample.decode_time -= origin_ms;
    }
    for (std::size_t segment = 0; segment < segments.size(); ++segment) {
        const auto output = read(resource(hls, "s" + std::to_string(segment) + ".m4s"), context);
        const auto header = write_segment_header(zero_based, segments[segment]);
        expect(output.size() >= header.size() &&
                   std::equal(header.begin(), header.end(), output.begin()),
               scenario + ": rebased tfdt/trun header " + std::to_string(segment));
        auto cursor = header.size();
        const auto& range = segments[segment].ranges.front();
        for (auto sample = range.first; sample < range.end; ++sample) {
            const auto& packet = video.samples[sample];
            expect(cursor + packet.size <= output.size() &&
                       std::equal(output.begin() + cursor, output.begin() + cursor + packet.size,
                                  source.bytes->begin() + packet.offset),
                   scenario + ": unchanged compressed video packet");
            cursor += packet.size;
        }
        expect(cursor == output.size(), scenario + ": exact video payload end");
    }
    const auto captions = text(resource(hls, "t0s0.vtt"), context);
    expect(captions.find("00:00:00.000 --> 00:00:00.040\nCrosses origin") != std::string::npos &&
               captions.find("00:00:01.000 --> 00:00:02.000\nOriginal caption") !=
                   std::string::npos &&
               captions.find("Before video") == std::string::npos,
           scenario + ": subtitle clipping and common-origin timing: " + captions);
    expect(text(resource(hls, "t0s1.vtt"), context)
                   .find("00:00:05.900 --> 00:00:06.200\nCrosses segment") != std::string::npos,
           scenario + ": subtitle retained across segment cut");
    expect(movie.text_tracks.front().cues.at(2).start_us == (origin_ms + 1000) * 1000,
           scenario + ": native subtitle clock remains unchanged");
}

void audible_reference(Source& source, const std::vector<float>& decoded,
                       std::uint64_t origin_samples, const MediaReadContext& context,
                       const std::string& scenario) {
    auto reference = AudioDecoderSource::open(source.callbacks(), context, 42, true);
    (void)reference->read_window(0, 1024, context);
    // Literal offsets from the independently authored 83/123-ms video origins.
    // Check the beginning, the AAC join and the final tail without correlation shifts.
    const auto total = decoded.size() / 2;
    for (const auto begin : {std::uint64_t{0}, std::uint64_t{263744}, total - 24000}) {
        const auto window = reference->read_window(origin_samples + begin, 24000, context);
        const auto weights = stereo_downmix_matrix(window.channel_mask, window.channels);
        double squared = 0;
        double reference_energy = 0;
        for (std::size_t sample = 0; sample < 24000; ++sample) {
            for (std::size_t output = 0; output < 2; ++output) {
                double expected = 0;
                for (std::size_t input = 0; input < window.channels; ++input) {
                    const auto at = sample * window.channels + input;
                    if (at < window.samples.size()) {
                        expected += window.samples[at] * weights[output * 8 + input];
                    }
                }
                const auto difference = decoded[(begin + sample) * 2 + output] - expected;
                squared += difference * difference;
                reference_energy += expected * expected;
            }
        }
        const auto rms = std::sqrt(squared / 48000);
        // AAC is lossy. Bound relative error as well as absolute RMS so the
        // undownmixed stereo control and quieter 5.1 mix have comparable gates.
        const auto relative = std::sqrt(squared / std::max(reference_energy, 1e-12));
        expect(rms < 0.006 && (relative < 0.06 || rms < 0.0005), scenario + ": reference at " +
                                                                     std::to_string(begin) +
                                                                     " RMS=" + std::to_string(rms));
    }
}

void origin_case(const std::filesystem::path& path, bool indexed, std::uint64_t origin_ms,
                 std::uint64_t expected_samples, bool late) {
    const auto scenario = path.filename().string();
    std::cout << "origin scenario " << scenario << '\n';
    Source source(path);
    Store store;
    Context context;
    auto hls = StereoAacHls::open(source.callbacks(), store.callbacks(), 2 * 1024 * 1024,
                                  context.value, 42);
    expect(hls->presentation().audio_source_origin_48khz == (origin_ms == 83 ? 3984 : 5904),
           scenario + ": literal 48-kHz origin");
    expect(store.seals == 0, scenario + ": lazy startup");
    video_and_captions(source, *hls, context.value, origin_ms, indexed, scenario);
    const auto count = hls->audio_segment_count();
    const auto tail_name = "a" + std::to_string(count - 1) + ".m4s";
    const auto tail = read(resource(*hls, tail_name), context.value);
    auto combined = read(resource(*hls, "audio-init.mp4"), context.value);
    for (std::size_t segment = 0; segment < count; ++segment) {
        const auto bytes =
            read(resource(*hls, "a" + std::to_string(segment) + ".m4s"), context.value);
        combined.insert(combined.end(), bytes.begin(), bytes.end());
    }
    expect(read(resource(*hls, tail_name), context.value) == tail,
           scenario + ": cold tail/backward/forward bytes stable");
    if (origin_ms == 123) {
        Source unshifted(path.parent_path().parent_path() / "expanded-decoder/flac.mkv");
        Store original_store;
        auto original = StereoAacHls::open(unshifted.callbacks(), original_store.callbacks(),
                                           2 * 1024 * 1024, context.value, 42);
        for (std::size_t segment = 0; segment < count; ++segment) {
            const auto name = "a" + std::to_string(segment) + ".m4s";
            expect(read(resource(*hls, name), context.value) ==
                       read(resource(*original, name), context.value),
                   scenario + ": common offset preserves exact zero-origin AAC bytes");
        }
    }
    const auto decoded = MovieDecoder(combined).decode();
    expect(decoded.size() == expected_samples * 2, scenario + ": exact rebased EOF sample count");
    audible_reference(source, decoded, origin_ms == 83 ? 3984 : 5904, context.value, scenario);
    if (late) {
        // Audible audio begins 120 ms after video; avoid AAC's boundary pre-echo.
        expect(std::all_of(decoded.begin(), decoded.begin() + 4000 * 2,
                           [](float value) { return std::abs(value) < 1e-5; }),
               scenario + ": literal initial audio silence");
    }
    hls->stop();
    expect(store.clears == 1 && store.sealed.empty(), scenario + ": one-time cleanup");
}

void partial_cadence_refusal(const std::filesystem::path& fixtures) {
    Source source(fixtures.parent_path() / "decoder/dts-delay.mkv");
    Store store;
    Context context;
    bool refused = false;
    try {
        (void)StereoAacHls::open(source.callbacks(), store.callbacks(), 2 * 1024 * 1024,
                                 context.value, 42);
    } catch (const AudioDecodeException& error) {
        refused = error.reason() == AudioDecodeFailure::unsupported;
    }
    expect(refused && store.seals == 0 && store.clears == 1,
           "partial initial DTS cadence remains unsupported with complete cleanup");
}
} // namespace
int main(int argc, char** argv) {
    try {
        expect(argc == 2, "origin fixture directory");
        const std::filesystem::path fixtures(argv[1]);
        origin_case(fixtures / "dts-early-indexed.mkv", true, 83, 792000, false);
        origin_case(fixtures / "dts-late-full.mkv", false, 83, 792000, true);
        origin_case(fixtures / "dts-reordered-indexed.mkv", true, 83, 792000, false);
        origin_case(fixtures / "flac-common-indexed.mkv", true, 123, 312000, false);
        partial_cadence_refusal(fixtures);
        std::cout << "Matroska common-origin video/audio/subtitle, seek and EOF checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
