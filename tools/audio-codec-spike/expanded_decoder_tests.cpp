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
#include <stdexcept>
#include <string>

namespace {
using namespace send_airplay2;
using namespace send_airplay2::detail;
constexpr std::uint32_t audible_frames = 288144;
constexpr std::uint32_t full_window_frames = 336000;
constexpr double pi = 3.14159265358979323846;
void expect(bool value, const std::string& scenario) {
    if (!value) {
        throw std::runtime_error(scenario);
    }
}
/// Owns immutable bytes until every borrowed callback/decoder has been released.
class OwnedInput {
public:
    std::atomic_bool stop{false};
    std::size_t size_calls = 0;
    Bytes bytes;
    std::string label;
    explicit OwnedInput(const std::filesystem::path& path) {
        label = path.filename().string();
        std::ifstream file(path, std::ios::binary);
        expect(file.good(), "missing expanded fixture: " + path.filename().string());
        bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }
    OwnedInput(const OwnedInput&) = delete;
    OwnedInput& operator=(const OwnedInput&) = delete;
    OwnedInput(OwnedInput&&) = delete;
    OwnedInput& operator=(OwnedInput&&) = delete;
    MediaReadContext context() {
        return {&stop, &stop, std::chrono::steady_clock::now() + std::chrono::seconds(30)};
    }
    MediaSource callbacks() {
        return {[this] {
                    ++size_calls;
                    return static_cast<std::uint64_t>(bytes.size());
                },
                [this](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
                       const MediaReadContext& context) {
                    if (context.should_stop() || offset >= bytes.size()) {
                        return std::size_t{0};
                    }
                    const auto count = std::min<std::size_t>(
                        {capacity, 1021, bytes.size() - static_cast<std::size_t>(offset)});
                    std::memcpy(output, bytes.data() + offset, count);
                    return count;
                }};
    }
};
DecodedAudioWindow read(AudioDecoderSource& decoder, OwnedInput& input, std::uint64_t start,
                        std::uint32_t frames) {
    const auto context = input.context();
    try {
        return decoder.read_window(start, frames, context);
    } catch (const AudioDecodeException& error) {
        throw std::runtime_error(input.label + ": window start=" + std::to_string(start) +
                                 ", count=" + std::to_string(frames) + ": " + error.what());
    }
}
void compare(const DecodedAudioWindow& reference, const DecodedAudioWindow& actual,
             const std::string& scenario) {
    expect(actual.channels == reference.channels && actual.source_track_id == 42,
           scenario + ": identity/layout");
    expect(actual.samples.size() <= reference.samples.size() &&
               actual.first_sample * actual.channels <=
                   reference.samples.size() - actual.samples.size(),
           scenario + ": reference bounds");
    double error = 0;
    for (std::size_t index = 0; index < actual.samples.size(); ++index) {
        const auto difference = actual.samples[index] -
                                reference.samples[actual.first_sample * actual.channels + index];
        expect(std::isfinite(actual.samples[index]), scenario + ": nonfinite PCM");
        error += difference * difference;
    }
    expect(!actual.samples.empty() && error / actual.samples.size() < 0.000001,
           scenario + ": PCM mismatch, MSE=" + std::to_string(error / actual.samples.size()));
}
void known_lossless_samples(const DecodedAudioWindow& value, const std::string& scenario,
                            std::uint32_t leading_discard = 0) {
    for (const auto frame : {0U, 91U, 4801U, 160003U, audible_frames - 1}) {
        if (frame < leading_discard) {
            continue;
        }
        for (std::size_t channel = 0; channel < value.channels; ++channel) {
            constexpr unsigned surround_frequencies[] = {310, 470, 630, 80, 790, 950};
            const auto frequency = value.channels == 6 ? surround_frequencies[channel]
                                   : channel == 0      ? 173U
                                                       : 227U;
            const auto integer = std::round(4096 * std::sin(2 * pi * frequency * frame / 48000));
            expect(std::abs(value.samples[frame * value.channels + channel] - integer / 32768) <
                       1.0 / 32768,
                   scenario + ": independent tone sample, frame=" + std::to_string(frame) +
                       ", channel=" + std::to_string(channel));
        }
    }
}
/// Independent tone frequencies identify channel ordering even for lossy inputs.
void known_channel_frequencies(const DecodedAudioWindow& value, const std::string& scenario) {
    constexpr unsigned surround_frequencies[] = {310, 470, 630, 80, 790, 950};
    constexpr unsigned stereo_frequencies[] = {173, 227};
    constexpr std::size_t tone_start = 48000;
    constexpr std::size_t tone_frames = 4096;
    const auto* frequencies = value.channels == 6 ? surround_frequencies : stereo_frequencies;
    for (std::size_t channel = 0; channel < value.channels; ++channel) {
        double expected_power = 0;
        double largest_other_power = 0;
        for (std::size_t tone = 0; tone < value.channels; ++tone) {
            double sine = 0, cosine = 0;
            for (std::size_t frame = 0; frame < tone_frames; ++frame) {
                const auto angle = 2 * pi * frequencies[tone] * frame / 48000;
                const auto sample = value.samples[(tone_start + frame) * value.channels + channel];
                sine += sample * std::sin(angle);
                cosine += sample * std::cos(angle);
            }
            const auto power = sine * sine + cosine * cosine;
            if (tone == channel) {
                expected_power = power;
            } else {
                largest_other_power = std::max(largest_other_power, power);
            }
        }
        expect(expected_power > 100 && expected_power > 10 * largest_other_power,
               scenario +
                   ": independent tone/channel ordering, channel=" + std::to_string(channel));
    }
}
void selected_windows(const std::filesystem::path& directory) {
    for (const auto* name :
         {"flac.mkv",      "flac.mp4",      "opus.mkv",      "opus.mp4",      "vorbis.mkv",
          "truehd.mkv",    "pcm-s16le.mkv", "pcm-s16be.mkv", "pcm-s24le.mkv", "pcm-s24be.mkv",
          "pcm-s32le.mkv", "pcm-s32be.mkv", "pcm-f32le.mkv", "pcm-f64le.mkv", "pcm-s16le.mov",
          "pcm-s16be.mov", "pcm-s24le.mov", "pcm-s24be.mov", "pcm-s32le.mov", "pcm-s32be.mov",
          "pcm-f32le.mov", "pcm-f32be.mov", "pcm-f64le.mov", "pcm-f64be.mov", "flac-51.mkv",
          "opus-51.mkv",   "vorbis-51.mkv", "truehd-51.mkv"}) {
        const std::string scenario(name);
        const auto channels = scenario.find("-51") != std::string::npos ? 6U : 2U;
        OwnedInput input(directory / name);
        const auto context = input.context();
        auto decoder = AudioDecoderSource::open(input.callbacks(), context, 42);
        const auto reference = read(*decoder, input, 0, full_window_frames);
        expect(reference.eof && reference.channels == channels && reference.source_track_id == 42,
               scenario + ": full decode/EOF/identity");
        expect(reference.samples.size() == audible_frames * channels,
               scenario + ": literal audible length, actual=" +
                   std::to_string(reference.samples.size() / channels));
        double energy = 0;
        for (const auto sample : reference.samples) {
            expect(std::isfinite(sample), scenario + ": full PCM is nonfinite");
            energy += sample * sample;
        }
        expect(energy / reference.samples.size() > 0.001, scenario + ": missing owned tone signal");
        known_channel_frequencies(reference, scenario);
        if (scenario.find("pcm-") == 0 || scenario.find("flac") == 0 ||
            scenario.find("truehd") == 0) {
            known_lossless_samples(reference, scenario);
        }
        for (const auto start : {270000U, 48000U, 160000U, 270000U}) {
            const auto actual = read(*decoder, input, start, 4096);
            expect(actual.samples.size() == 4096 * channels, scenario + ": bounded seek length");
            compare(reference, actual, scenario + ": seek start=" + std::to_string(start));
        }
        const auto tail = read(*decoder, input, audible_frames - 144, 4096);
        expect(tail.eof && tail.samples.size() == 144 * channels, scenario + ": literal tail trim");
        compare(reference, tail, scenario + ": EOF tail");
        OwnedInput cold(directory / name);
        const auto cold_context = cold.context();
        auto cold_decoder = AudioDecoderSource::open(cold.callbacks(), cold_context, 42);
        const auto cold_tail = read(*cold_decoder, cold, audible_frames - 144, 4096);
        expect(cold_tail.eof && cold_tail.samples.size() == 144 * channels,
               scenario + ": cold tail trim");
        compare(reference, cold_tail, scenario + ": cold tail");
        expect(input.size_calls == 1 && cold.size_calls == 1, scenario + ": source snapshot");
        std::cout << scenario << ": selected windows and literal trim passed\n";
    }
}
void pcm_metadata_and_leading_trim(const std::filesystem::path& directory) {
    OwnedInput input(directory / "pcm-leading-discard.mkv");
    const auto context = input.context();
    const auto inventory = probe_media_tracks(input.callbacks(), context);
    const auto& track = inventory.tracks.at(1);
    expect(track.id == 42 && track.audio_sample_rate == 48000 && track.audio_channels == 2 &&
               track.audio_bit_depth == 16,
           "native PCM rate/channels/depth metadata");
    auto decoder = AudioDecoderSource::open(input.callbacks(), context, 42);
    const auto actual = read(*decoder, input, 0, full_window_frames);
    expect(actual.samples.size() == audible_frames * 2 &&
               std::all_of(actual.samples.begin(), actual.samples.begin() + 192,
                           [](float value) { return value == 0; }),
           "negative block padding preserves initial presentation silence");
    known_lossless_samples(actual, "PCM negative padding", 96);
    OwnedInput unsupported(directory / "pcm-unsupported-depth.mkv");
    const auto unsupported_context = unsupported.context();
    try {
        (void)AudioDecoderSource::open(unsupported.callbacks(), unsupported_context, 42);
        throw std::runtime_error("unsigned 8-bit PCM unexpectedly accepted");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::unsupported, "unsupported PCM depth category");
    }
}
void opus_leading_padding(const std::filesystem::path& directory) {
    OwnedInput original(directory / "opus.mkv");
    const auto original_context = original.context();
    auto original_decoder = AudioDecoderSource::open(original.callbacks(), original_context, 42);
    const auto reference = read(*original_decoder, original, 0, full_window_frames);
    OwnedInput padded(directory / "opus-leading-discard.mkv");
    const auto padded_context = padded.context();
    auto decoder = AudioDecoderSource::open(padded.callbacks(), padded_context, 42);
    const auto actual = read(*decoder, padded, 0, full_window_frames);
    expect(actual.eof && actual.samples.size() == audible_frames * 2 &&
               std::all_of(actual.samples.begin(), actual.samples.begin() + 192,
                           [](float value) { return value == 0; }),
           "Opus block leading padding and track delay applied once");
    const auto tail = read(*decoder, padded, audible_frames - 144, 4096);
    expect(tail.eof && tail.samples.size() == 288, "Opus signed-padding EOF tail length");
    compare(reference, tail, "Opus signed-padding EOF tail");
}
void malformed_configuration(const std::filesystem::path& directory) {
    struct Mutation {
        const char* file;
        const char* marker;
        std::size_t offset;
        std::uint8_t value;
        AudioDecodeFailure failure;
    };
    for (const auto& mutation :
         {Mutation{"flac.mkv", "fLaC", 7, 33, AudioDecodeFailure::malformed},
          Mutation{"vorbis.mkv",
                   "\x01"
                   "vorbis",
                   0, 0, AudioDecodeFailure::malformed},
          Mutation{"flac.mp4", "dfLa", 4, 1, AudioDecodeFailure::malformed},
          Mutation{"opus.mkv", "OpusHead", 18, 255, AudioDecodeFailure::unsupported},
          Mutation{"opus.mp4", "dOps", 14, 255, AudioDecodeFailure::unsupported},
          Mutation{"pcm-s24le.mov", "frma", 4, 'z', AudioDecodeFailure::malformed}}) {
        OwnedInput input(directory / mutation.file);
        const auto marker_end = mutation.marker + std::strlen(mutation.marker);
        const auto found =
            std::search(input.bytes.begin(), input.bytes.end(), mutation.marker, marker_end);
        expect(found != input.bytes.end(),
               std::string(mutation.file) + ": missing mutation marker");
        const auto offset = static_cast<std::size_t>(found - input.bytes.begin()) + mutation.offset;
        expect(offset < input.bytes.size(), "configuration mutation bounds");
        input.bytes[offset] = mutation.value;
        const auto context = input.context();
        try {
            (void)AudioDecoderSource::open(input.callbacks(), context, 42);
            throw std::runtime_error(std::string(mutation.file) +
                                     ": malformed configuration accepted");
        } catch (const AudioDecodeException& error) {
            expect(error.reason() == mutation.failure,
                   std::string(mutation.file) + ": configuration failure category");
        }
    }
}
void packet_trim_budget(const std::filesystem::path& directory) {
    OwnedInput input(directory / "pcm-over-limit-discard.mkv");
    const auto context = input.context();
    auto decoder = AudioDecoderSource::open(input.callbacks(), context, 42);
    try {
        (void)decoder->read_window(0, full_window_frames, context);
        throw std::runtime_error("over-limit packet trim unexpectedly accepted");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::unsupported, "packet trim budget category");
    }
}
void coarse_clock_gate(const std::filesystem::path& directory) {
    OwnedInput input(directory / "flac-coarse-clock.mkv");
    const auto context = input.context();
    auto decoder = AudioDecoderSource::open(input.callbacks(), context, 42);
    const auto full = read(*decoder, input, 0, full_window_frames);
    expect(full.eof && full.samples.size() == audible_frames * 2 &&
               full.timestamp_precision_samples == 48,
           "coarse FLAC sequential decode with explicit clock precision");
    auto canonical = AudioDecoderSource::open(input.callbacks(), context, 42, true);
    expect(canonical->requires_start_rebuild(), "coarse HLS selects bounded reconstruction");
    compare(full, read(*canonical, input, 270000, 4096), "coarse FLAC canonical cold seek");
}
void matroska_output_frequency(const std::filesystem::path& directory) {
    OwnedInput input(directory / "aac-output-rate.mkv");
    const auto context = input.context();
    const auto inventory = probe_media_tracks(input.callbacks(), context);
    const auto& track = inventory.tracks.at(1);
    expect(track.audio_sample_rate == 24000 && track.audio_output_sample_rate == 48000,
           "Matroska coded and output sampling frequencies are distinct");
    auto decoder = AudioDecoderSource::open(input.callbacks(), context, 42);
    const auto window = read(*decoder, input, 48000, 4096);
    expect(window.channels == 2 && window.samples.size() == 8192,
           "AAC explicit output-frequency metadata permits decoder output");
    known_channel_frequencies(read(*decoder, input, 0, full_window_frames),
                              "AAC output-frequency metadata");
    OwnedInput invalid(directory / "aac-output-rate.mkv");
    constexpr std::uint8_t output_frequency_header[] = {0x78, 0xb5, 0x88};
    const auto field =
        std::search(invalid.bytes.begin(), invalid.bytes.end(), std::begin(output_frequency_header),
                    std::end(output_frequency_header));
    expect(field != invalid.bytes.end() && invalid.bytes.end() - field >= 11,
           "output-frequency fixture element location");
    // The literal header identifies the eight-byte big-endian floating value.
    std::fill_n(field + 3, 8, std::uint8_t{0});
    const auto invalid_context = invalid.context();
    try {
        (void)probe_media_tracks(invalid.callbacks(), invalid_context);
        throw std::runtime_error("zero OutputSamplingFrequency accepted");
    } catch (const RemuxException& error) {
        expect(error.reason() == RemuxFailure::malformed, "output-frequency range category");
    }
}
} // namespace
int main(int argc, char** argv) {
    try {
        expect(argc == 2, "expanded fixture directory argument");
        selected_windows(argv[1]);
        pcm_metadata_and_leading_trim(argv[1]);
        opus_leading_padding(argv[1]);
        malformed_configuration(argv[1]);
        packet_trim_budget(argv[1]);
        coarse_clock_gate(argv[1]);
        matroska_output_frequency(argv[1]);
        std::cout << "expanded decoder checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "expanded decoder failure: " << error.what() << '\n';
        return 1;
    }
}
