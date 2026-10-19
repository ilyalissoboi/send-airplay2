// SPDX-License-Identifier: Apache-2.0
#include "hls_test_support.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>

namespace {
using namespace send_airplay2;
using namespace send_airplay2::detail;
using sap2_test::expect;

void check_ogg_windows(const std::filesystem::path& path, std::uint32_t channels,
                       std::uint32_t expected_frames) {
    const auto scenario = path.filename().string();
    sap2_test::Context context;
    sap2_test::Source source(path);
    auto decoder = AudioDecoderSource::open(source.callbacks(), context.value, 1, false, true);
    expect(decoder->audio_only_duration_samples(context.value) == expected_frames,
           scenario + ": native granule-derived duration");
    // A linear decode shorter than the ten-second window budget is an independent
    // history reference for seeks. A separate owner starts cold near the end.
    const auto reference_frames = std::min(expected_frames, 9U * 48000);
    const auto reference = decoder->read_window(0, reference_frames, context.value);
    expect(reference.channels == channels &&
               reference.samples.size() == reference_frames * channels,
           scenario + ": start/pre-skip reference coverage");
    if (channels == 2) {
        // Original authored tones, independent of demuxer timestamps and skip
        // data. Lossy encoding permits small error but not an overlap-sized
        // shift of the audible zero point or swapped stereo channels.
        constexpr double pi = 3.14159265358979323846;
        double tone_error = 0;
        for (std::size_t frame = 4800; frame < 48000; ++frame) {
            for (std::size_t channel = 0; channel < 2; ++channel) {
                const auto frequency = channel == 0 ? 173 : 227;
                const auto expected = 0.125 * std::sin(2 * pi * frequency * frame / 48000);
                const auto difference = reference.samples[frame * 2 + channel] - expected;
                tone_error += difference * difference;
            }
        }
        expect(tone_error / (43200 * 2) < 0.0001,
               scenario + ": independent tone origin/channel mismatch");
    }
    auto cold = AudioDecoderSource::open(source.callbacks(), context.value, 1, false, true);
    const auto tail = cold->read_window(expected_frames - 1000, 1001, context.value);
    expect(tail.eof && tail.samples.size() == 1000 * channels,
           scenario + ": literal cold-end granule trim");
    for (const auto first :
         {reference_frames - 8192, 0U, 48000U, 48001U, 96001U, reference_frames - 8192}) {
        const auto window = cold->read_window(first, 8192, context.value);
        expect(window.channels == channels && window.source_track_id == 1 &&
                   window.samples.size() == 8192 * channels,
               scenario + ": seek coverage at " + std::to_string(first));
        double squared_error = 0;
        for (std::size_t index = 0; index < window.samples.size(); ++index) {
            const auto error = window.samples[index] - reference.samples[first * channels + index];
            expect(std::isfinite(error), scenario + ": finite seek PCM");
            squared_error += error * error;
        }
        expect(squared_error / window.samples.size() < 0.000001,
               scenario + ": seek/reopen PCM mismatch at " + std::to_string(first));
    }
}

void check_rendition(const std::filesystem::path& path, std::size_t segment_count,
                     std::size_t expected_frames) {
    const auto scenario = path.filename().string();
    sap2_test::Context context;
    sap2_test::Source source(path);
    sap2_test::Store store;
    auto hls = StereoAacHls::open_audio_only(source.callbacks(), store.callbacks(), 8ULL << 20,
                                             context.value);
    const auto& presentation = hls->presentation();
    expect(presentation.segment_count == segment_count &&
               hls->audio_segment_count() == segment_count,
           scenario + ": audio-only segment count");
    expect(presentation.video_boundaries_48khz.empty() &&
               presentation.video_segment_bytes.empty() && presentation.text_track_count == 0,
           scenario + ": no video timeline or subtitle resources");
    const auto master = sap2_test::text(sap2_test::resource(*hls, "main.m3u8"), context.value);
    expect(master.find("CODECS=\"mp4a.40.2\"") != std::string::npos &&
               master.find("\naudio.m3u8\n") != std::string::npos &&
               master.find("RESOLUTION") == std::string::npos &&
               master.find("FRAME-RATE") == std::string::npos &&
               master.find("VIDEO-RANGE") == std::string::npos &&
               master.find("EXT-X-MEDIA") == std::string::npos &&
               master.find("INDEPENDENT-SEGMENTS") == std::string::npos,
           scenario + ": one direct audio variant without video or independence claims");
    const auto playlist = sap2_test::text(sap2_test::resource(*hls, "audio.m3u8"), context.value);
    expect(playlist.find("#EXT-X-ENDLIST\n") != std::string::npos &&
               playlist.find("#EXT-X-MAP:URI=\"audio-init.mp4\"") != std::string::npos,
           scenario + ": finite audio-only media playlist");
    if (segment_count == 3) {
        // Literal 48-kHz AAC-frame cuts: 287744, 575488, then rounded end 624144.
        expect(playlist.find("#EXTINF:5.994667,") != std::string::npos &&
                   playlist.find("#EXTINF:1.013667,") != std::string::npos,
               scenario + ": sample-clock segment and partial final duration");
    }
    expect(store.seals == 0, scenario + ": startup calibration does not seal full audio");
    const auto late_name = "a" + std::to_string(segment_count - 1) + ".m4s";
    const auto late_resource = sap2_test::resource(*hls, late_name);
    const auto late = sap2_test::read(late_resource, context.value);
    auto bytes = sap2_test::read(sap2_test::resource(*hls, "audio-init.mp4"), context.value);
    for (std::size_t index = 0; index < segment_count; ++index) {
        const auto fragment = sap2_test::read(
            sap2_test::resource(*hls, "a" + std::to_string(index) + ".m4s"), context.value);
        bytes.insert(bytes.end(), fragment.begin(), fragment.end());
    }
    const auto decoded = sap2_test::MovieDecoder(bytes).decode();
    expect(decoded.size() == expected_frames * 2,
           scenario + ": independently decoded stereo trim, actual frames=" +
               std::to_string(decoded.size() / 2));
    double energy = 0;
    for (std::size_t index = 96000; index < 104192; ++index) {
        energy += decoded.at(index) * decoded.at(index);
    }
    expect(energy / 8192 > 0.0001, scenario + ": audible converted tones");
    expect(sap2_test::read(late_resource, context.value) == late && store.seals == segment_count,
           scenario + ": cold final/backward reads are stable and generated once");
    expect(hls->authoring().copied_rates.peak == 0 &&
               hls->authoring().representative_audio_rates.peak > 0 &&
               hls->failure() == StereoAacHlsFailure::none,
           scenario + ": audio-only representative authoring and worker status");
    hls->stop();
    hls->stop();
    expect(store.clears == 1 && store.sealed.empty(), scenario + ": exact-once cleanup");
    try {
        (void)media_resource_size(late_resource, context.value);
        throw std::runtime_error(scenario + ": retained resource remained active after Stop");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == AudioDecodeFailure::stopped,
               scenario + ": retained resource stopped category");
    }
    std::cout << scenario << ": cold seek, decoded trim, playlists and cleanup passed\n";
}

void expect_refusal(const std::filesystem::path& path, AudioDecodeFailure reason,
                    std::optional<std::uint64_t> id = {}, std::uint64_t quota = 8ULL << 20,
                    bool cancelled = false) {
    const auto scenario = path.filename().string();
    sap2_test::Context context;
    context.cancelled = cancelled;
    sap2_test::Source source(path);
    sap2_test::Store store;
    try {
        (void)StereoAacHls::open_audio_only(source.callbacks(), store.callbacks(), quota,
                                            context.value, id);
        throw std::runtime_error(scenario + ": unexpected audio-only admission");
    } catch (const AudioDecodeException& error) {
        expect(error.reason() == reason, scenario + ": wrong refusal category " +
                                             std::to_string(static_cast<int>(error.reason())));
    }
    expect(store.clears == 1 && store.seals == 0, scenario + ": cleanup on startup refusal");
}

void check_ogg_refusals(const std::filesystem::path& directory) {
    for (const auto* name : {"chained.opus", "missing-eos.opus", "bad-crc.opus", "bad-end.opus",
                             "bad-vorbis-end.ogg"}) {
        sap2_test::Context context;
        sap2_test::Source source(directory / name);
        sap2_test::Store store;
        bool refused = false;
        try {
            (void)StereoAacHls::open_audio_only(source.callbacks(), store.callbacks(), 8ULL << 20,
                                                context.value);
        } catch (const RemuxException&) {
            refused = true; // Native framing/clock refusal before decoder admission.
        } catch (const AudioDecodeException&) {
            refused = true; // Decoder/tail/coverage refusal for inconsistent encoded content.
        }
        expect(refused && store.clears == 1 && store.seals == 0,
               std::string(name) + ": failed startup owns cleanup and exposes no fragments");
    }
    expect_refusal(directory / "opus48.opus", AudioDecodeFailure::invalid_selection, 99);
    expect_refusal(directory / "opus48.opus", AudioDecodeFailure::too_large, {}, 1);
    expect_refusal(directory / "opus48.opus", AudioDecodeFailure::stopped, {}, 8ULL << 20, true);
    sap2_test::Context context;
    sap2_test::Source source(directory / "opus48.opus");
    try {
        (void)AudioDecoderSource::open(source.callbacks(), context.value);
        throw std::logic_error("default decoder unexpectedly admits Ogg");
    } catch (const RemuxException&) {
    }
}

void check_selection_and_failure(const std::filesystem::path& directory,
                                 const std::filesystem::path& video_directory) {
    expect_refusal(directory / "aac.mp4", AudioDecodeFailure::unsupported);
    expect_refusal(directory / "edited-pcm.mov", AudioDecodeFailure::unsupported);
    expect_refusal(directory / "multiple-flac.mp4", AudioDecodeFailure::selection_required);
    expect_refusal(directory / "flac-stereo.mp4", AudioDecodeFailure::invalid_selection, 99);
    expect_refusal(directory / "flac-stereo.mp4", AudioDecodeFailure::too_large, {}, 1);
    expect_refusal(directory / "flac-stereo.mp4", AudioDecodeFailure::stopped, {}, 8ULL << 20,
                   true);
    expect_refusal(video_directory / "flac.mp4", AudioDecodeFailure::unsupported);
    expect_refusal(video_directory / "flac.mkv", AudioDecodeFailure::unsupported);
    {
        sap2_test::Context legacy_context;
        sap2_test::Source audio_only(directory / "flac-stereo.mp4");
        sap2_test::Store legacy_store;
        try {
            (void)StereoAacHls::open(audio_only.callbacks(), legacy_store.callbacks(), 8ULL << 20,
                                     legacy_context.value);
            throw std::runtime_error(
                "legacy video composition unexpectedly admitted audio-only input");
        } catch (const RemuxException& error) {
            expect(error.reason() == RemuxFailure::unsupported,
                   "legacy video composition: unsupported missing video category");
        }
        expect(legacy_store.clears == 1, "legacy video composition: failed startup cleanup");
    }
    sap2_test::Context context;
    sap2_test::Source source(directory / "multiple-flac.mp4");
    sap2_test::Store store;
    auto hls = StereoAacHls::open_audio_only(source.callbacks(), store.callbacks(), 8ULL << 20,
                                             context.value, 42);
    store.fail_seal = true;
    try {
        (void)media_resource_size(sap2_test::resource(*hls, "a0.m4s"), context.value);
        throw std::runtime_error("explicit audio selection: injected seal failure was ignored");
    } catch (const AudioDecodeException&) {
        expect(hls->failure() == StereoAacHlsFailure::cache_error,
               "explicit audio selection: terminal cache failure retained");
    }
    hls->stop();
    expect(store.clears == 1, "explicit audio selection: cleanup after worker failure");
}
} // namespace

int main(int argc, char** argv) {
    try {
        expect(argc == 5, "MP4, video, standalone and Ogg fixture directories required");
        const std::filesystem::path directory(argv[1]);
        check_rendition(directory / "flac-stereo.mp4", 3, 624144);
        check_rendition(directory / "flac-44100.mp4", 3, 624144);
        check_rendition(directory / "pcm-51.mov", 1, 144144);
        const std::filesystem::path standalone(argv[3]);
        for (const auto* name : {"flac48.flac", "flac441.flac", "pcm16.wav", "seektable.flac"}) {
            check_rendition(standalone / name, 3, 624144);
        }
        for (const auto* name : {"flac51.flac", "pcm24-51.wav", "pcm32.wav", "float32.wav",
                                 "float64.wav", "mono441.wav", "rf64.wav", "oddchunk.wav"}) {
            check_rendition(standalone / name, 1, 144144);
        }
        {
            sap2_test::Context legacy_context;
            sap2_test::Source standalone_source(standalone / "pcm16.wav");
            try {
                (void)AudioDecoderSource::open(standalone_source.callbacks(), legacy_context.value);
                throw std::runtime_error(
                    "default decoder probe unexpectedly admitted standalone WAVE");
            } catch (const RemuxException&) { /* Opt-in is required by the private owner. */
            }
        }
        expect_refusal(standalone / "too-long.flac", AudioDecodeFailure::malformed);
        expect_refusal(standalone / "too-short.flac", AudioDecodeFailure::malformed);
        const std::filesystem::path ogg(argv[4]);
        for (const auto* name : {"opus48.opus", "vorbis48.ogg", "vorbis441.ogg"}) {
            check_ogg_windows(ogg / name, 2, 624144);
            check_rendition(ogg / name, 3, 624144);
        }
        for (const auto* name : {"opus-mono.opus", "opus51.opus", "vorbis51.ogg"}) {
            check_ogg_windows(ogg / name, std::string(name) == "opus-mono.opus" ? 1 : 6, 144144);
            check_rendition(ogg / name, 1, 144144);
        }
        check_ogg_refusals(ogg);
        check_selection_and_failure(directory, argv[2]);
        std::cout << "Audio-only admission, cancellation and terminal failure checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Audio-only HLS failure: " << error.what() << '\n';
        return 1;
    }
}
