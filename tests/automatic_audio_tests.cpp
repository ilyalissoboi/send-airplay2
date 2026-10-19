// SPDX-License-Identifier: Apache-2.0
#include "prepared_automatic_media.h"
#include "mkv_demux.h"
#ifdef SAP2_HAS_STEREO_AAC
#include "../tools/audio-codec-spike/hls_test_support.h"
#endif
#include <algorithm>
#include <array>
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
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace send_airplay2;
using namespace send_airplay2::detail;
void expect(bool passed, const std::string& scenario) {
    if (!passed) {
        throw std::runtime_error(scenario);
    }
}
struct Context {
    std::atomic_bool stopped{false}, cancelled{false};
    MediaReadContext value{&stopped, &cancelled,
                           std::chrono::steady_clock::now() + std::chrono::seconds(60)};
};
struct Source {
    Bytes bytes;
    std::atomic<std::size_t> size_calls{0}, calls{0};
    bool fail = false;
    explicit Source(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        expect(input.good(), "missing owned policy fixture: " + path.filename().string());
        bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    RandomReader reader() {
        return [this](std::uint64_t offset, std::uint8_t* output, std::size_t length) {
            expect(offset <= bytes.size() && length <= bytes.size() - offset,
                   "native fixture read bounds");
            std::memcpy(output, bytes.data() + offset, length);
        };
    }
    MediaSource callbacks() {
        return {[this] {
                    ++size_calls;
                    return static_cast<std::uint64_t>(bytes.size());
                },
                [this](std::uint64_t offset, std::uint8_t* output, std::size_t capacity,
                       const MediaReadContext& context) {
                    ++calls;
                    if (fail || context.should_stop() || offset >= bytes.size()) {
                        return std::size_t{0};
                    }
                    const auto count = static_cast<std::size_t>(
                        std::min<std::uint64_t>({capacity, 997, bytes.size() - offset}));
                    std::memcpy(output, bytes.data() + offset, count);
                    return count;
                }};
    }
};
struct Store {
    std::mutex mutex;
    std::map<std::size_t, Bytes> fragments;
    std::atomic<std::size_t> seals{0}, reads{0}, clears{0};
    bool fail_seal = false;
    AacFragmentStore callbacks() {
        return {[this](std::size_t id, const Bytes& bytes, const AacCacheContext&) {
                    std::lock_guard<std::mutex> lock(mutex);
                    ++seals;
                    if (fail_seal) {
                        throw std::runtime_error("owned test seal failure");
                    }
                    fragments.emplace(id, bytes);
                },
                [this](std::size_t id, std::uint64_t offset, std::uint8_t* output,
                       std::size_t capacity, const AacCacheContext&) {
                    std::lock_guard<std::mutex> lock(mutex);
                    ++reads;
                    const auto& bytes = fragments.at(id);
                    const auto count = static_cast<std::size_t>(
                        std::min<std::uint64_t>({capacity, 503, bytes.size() - offset}));
                    std::memcpy(output, bytes.data() + offset, count);
                    return count;
                },
                [this] {
                    std::lock_guard<std::mutex> lock(mutex);
                    ++clears;
                    fragments.clear();
                }};
    }
};
const MediaResource& resource(const RemuxedHls& hls, const std::string& name) {
    const auto found = std::find_if(hls.resources.begin(), hls.resources.end(),
                                    [&](const auto& value) { return value.name == name; });
    expect(found != hls.resources.end(), "missing policy resource " + name);
    return *found;
}
Bytes read(const MediaResource& resource, const MediaReadContext& context) {
    Bytes bytes(static_cast<std::size_t>(media_resource_size(resource, context)));
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const auto count =
            resource.source.read_at(offset, bytes.data() + offset,
                                    std::min<std::size_t>(65536, bytes.size() - offset), context);
        expect(count > 0 && count <= bytes.size() - offset, "resource source read failed");
        offset += count;
    }
    return bytes;
}
std::unique_ptr<PreparedAutomaticMedia>
prepare(Source& source, Store& store, Context& context, std::optional<std::uint64_t> id = {},
        AudioConversionPermission permission = AudioConversionPermission::allow_stereo) {
    return PreparedAutomaticMedia::open(source.callbacks(), store.callbacks(), 16ULL << 20,
                                        context.value, AudioDeliveryProfile::apple_hls_v1,
                                        permission, id);
}
template <class Function>
void refusal(MediaPreparationFailure expected, const std::string& scenario, Function function) {
    try {
        function();
        throw std::runtime_error(scenario + ": unexpectedly prepared");
    } catch (const MediaPreparationException& error) {
        expect(error.reason() == expected, scenario + ": wrong refusal category " +
                                               std::to_string(static_cast<int>(error.reason())));
    }
}
Bytes payload(const Bytes& segment) {
    std::size_t offset = 0;
    while (offset < segment.size()) {
        expect(segment.size() - offset >= 8, "fMP4 test box header bounds");
        const auto size = (static_cast<std::uint32_t>(segment[offset]) << 24) |
                          (static_cast<std::uint32_t>(segment[offset + 1]) << 16) |
                          (static_cast<std::uint32_t>(segment[offset + 2]) << 8) |
                          segment[offset + 3];
        expect(size >= 8 && size <= segment.size() - offset, "fMP4 test box size bounds");
        if (std::memcmp(segment.data() + offset + 4, "mdat", 4) == 0) {
            return {segment.begin() + offset + 8, segment.begin() + offset + size};
        }
        offset += size;
    }
    throw std::runtime_error("missing copied mdat");
}
void copied_movie(const std::filesystem::path& path, std::uint32_t channels,
                  bool unknown_unselected = false) {
    Source source(path);
    Context context;
    Store store;
    if (unknown_unselected) {
        if (path.extension() == ".mkv") {
            const Bytes original{'A', '_', 'A', 'A', 'C'}, replacement{'A', '_', 'B', 'A', 'D'};
            const auto position = std::search(source.bytes.begin(), source.bytes.end(),
                                              original.begin(), original.end());
            expect(position != source.bytes.end(), "unselected MKV codec field exists");
            std::copy(replacement.begin(), replacement.end(), position);
        } else {
            const auto original_inventory = probe_mp4_tracks(source.reader(), source.bytes.size());
            const auto selected = select_source_audio(original_inventory, 7);
            const auto& entry = original_inventory.tracks.at(selected.track_index).configuration;
            const auto position =
                std::search(source.bytes.begin(), source.bytes.end(), entry.begin(), entry.end());
            expect(position != source.bytes.end(), "unselected MP4 sample entry exists");
            std::copy_n("zzzz", 4, position + 4);
        }
    }
    const bool mkv = starts_like_matroska(source.bytes.data(), source.bytes.size());
    const auto inventory = mkv ? probe_mkv_tracks(source.reader(), source.bytes.size())
                               : probe_mp4_tracks(source.reader(), source.bytes.size());
    try {
        (void)plan_audio_delivery(inventory, AudioDeliveryProfile::apple_hls_v1, 42);
    } catch (const RemuxException& error) {
        throw std::runtime_error(path.filename().string() + ": metadata: " + error.what());
    }
    const auto movie = mkv ? read_mkv_selected_audio(source.reader(), source.bytes.size(), 42)
                           : read_mp4_selected_audio(source.reader(), source.bytes.size(), 42);
    try {
        (void)validate_profile_copy_audio(movie.tracks.at(1), source.reader());
    } catch (const RemuxException& error) {
        throw std::runtime_error(path.filename().string() + ": copy table: " + error.what());
    }
    auto media = prepare(source, store, context, 42);
    const auto& decision = media->decision();
    expect(decision.source_id == 42 && decision.action == AudioDeliveryAction::copy &&
               decision.reason == AudioDeliveryReason::profile_copy &&
               decision.output_channels == channels,
           path.filename().string() + ": selected copy decision");
    expect(source.size_calls == 1 && store.seals == 0 && store.reads == 0 && store.clears == 0,
           "copy uses no decoder/cache and samples host size once");
    const auto segments = plan_segments(movie);
    for (std::size_t id = 0; id < segments.size(); ++id) {
        Bytes expected;
        for (std::size_t track = 0; track < movie.tracks.size(); ++track) {
            const auto& range = segments[id].ranges[track];
            for (auto packet = range.first; packet < range.end; ++packet) {
                const auto& sample = movie.tracks[track].samples[packet];
                expected.insert(expected.end(), source.bytes.begin() + sample.offset,
                                source.bytes.begin() + sample.offset + sample.size);
            }
        }
        expect(payload(read(resource(media->presentation(), "s" + std::to_string(id) + ".m4s"),
                            context.value)) == expected,
               path.filename().string() +
                   ": exact selected video/audio payload, segment=" + std::to_string(id));
    }
    if (path.filename() == "aac-subtitles.mp4") {
        expect(media->presentation().text_track_count == 1,
               "selected copy keeps subtitle association");
        const auto text = read(resource(media->presentation(), "t0s0.vtt"), context.value);
        expect(std::string(text.begin(), text.end()).find("WEBVTT") == 0,
               "copied subtitle rendition survives");
    }
    media->stop();
    media->stop();
    media.reset();
    expect(store.seals == 0 && store.reads == 0 && store.clears == 0,
           "copy never takes offered store ownership");
}
void selection_and_resource_refusals(const std::filesystem::path& decoder,
                                     const std::filesystem::path& policy) {
    Source source(decoder / "dts-commentary-first.mp4");
    Context context;
    Store store;
    refusal(MediaPreparationFailure::selection_required, "ambiguous MP4 before codec filtering",
            [&] { (void)prepare(source, store, context); });
    for (const auto id : {0ULL, 11ULL, 99ULL}) {
        refusal(MediaPreparationFailure::invalid_selection,
                "invalid original ID=" + std::to_string(id),
                [&] { (void)prepare(source, store, context, id); });
    }
    refusal(MediaPreparationFailure::unsupported, "preserve original DTS refuses conversion", [&] {
        (void)prepare(source, store, context, 42, AudioConversionPermission::preserve_original);
    });
    expect(store.clears == 0 && store.seals == 0,
           "pre-routing refusal leaves offered store untouched");
    Source silent(policy / "video-only.mp4");
    auto video = prepare(silent, store, context);
    expect(video->decision().selection == AudioSelectionStatus::video_only &&
               video->decision().source_id == 0 && video->decision().output_channels == 0 &&
               video->decision().action == AudioDeliveryAction::video_only,
           "no audio gives copied video only, without an invented AAC track");
    video->stop();
    video.reset();
    refusal(MediaPreparationFailure::invalid_selection, "explicit ID cannot become video-only",
            [&] { (void)prepare(silent, store, context, 42); });
    context.cancelled = true;
    const auto calls = source.calls.load();
    refusal(MediaPreparationFailure::cancelled, "cancel before any source work",
            [&] { (void)prepare(source, store, context, 42); });
    expect(source.calls == calls, "pre-cancel performs no source callback");
    context.cancelled = false;
    auto broken = source.callbacks();
    broken.read_at = [](std::uint64_t, std::uint8_t*, std::size_t,
                        const MediaReadContext&) -> std::size_t {
        throw std::runtime_error("owned test source fault");
    };
    refusal(MediaPreparationFailure::source_error, "source fault never falls back", [&] {
        (void)PreparedAutomaticMedia::open(broken, store.callbacks(), 16ULL << 20, context.value,
                                           AudioDeliveryProfile::apple_hls_v1,
                                           AudioConversionPermission::allow_stereo, 42);
    });
    Source encrypted(decoder / "aac-protected.mp4");
    refusal(MediaPreparationFailure::unsupported, "selected protection never falls back",
            [&] { (void)prepare(encrypted, store, context, 42); });
    Source rates(decoder / "ac3-commentary-first.mkv");
    auto rate_inventory = probe_mkv_tracks(rates.reader(), rates.bytes.size());
    const auto choice = select_source_audio(rate_inventory, 7);
    auto& rate_track = rate_inventory.tracks.at(choice.track_index);
    rate_track.audio_sample_rate = 24000;
    rate_track.audio_output_sample_rate = 48000;
    expect(plan_audio_delivery(rate_inventory, AudioDeliveryProfile::apple_hls_v1, 7).action ==
               AudioDeliveryAction::copy,
           "AAC-LC metadata plan retains coded/effective output-rate distinction");
    Source changed(decoder / "ac3-format-change.mkv");
    refusal(MediaPreparationFailure::malformed, "copied format change never retries stereo",
            [&] { (void)prepare(changed, store, context, 42); });
    expect(store.clears == 0 && store.seals == 0,
           "copy/refusal/video-only never call conversion store");
#ifdef SAP2_HAS_STEREO_AAC
    refusal(MediaPreparationFailure::cache_error, "conversion requires storage after resolution",
            [&] {
                (void)PreparedAutomaticMedia::open(source.callbacks(), {}, 0, context.value,
                                                   AudioDeliveryProfile::apple_hls_v1,
                                                   AudioConversionPermission::allow_stereo, 42);
            });
#else
    refusal(MediaPreparationFailure::backend_unavailable,
            "DTS requires missing backend, not earlier AAC",
            [&] { (void)prepare(source, store, context, 42); });
#endif
}
void finite_copy_edit_refusals(const std::filesystem::path& policy) {
    Context context;
    Store store;
    for (const bool wide_edit : {false, true}) {
        Source source(policy / (wide_edit ? "aac-subtitles.mp4" : "aac-48000-2.mp4"));
        // Independent owned elst bytes: one media edit beginning at AAC tick
        // 1024, covering three seconds (3000 at 1 kHz, 2304000000 at 768 MHz).
        const Bytes signature =
            wide_edit
                ? Bytes{'e', 'l', 's',  't',  1,    0, 0, 0, 0, 0, 0, 1, 0, 0,
                        0,   0,   0x89, 0x54, 0x40, 0, 0, 0, 0, 0, 0, 0, 4, 0}
                : Bytes{'e', 'l', 's', 't', 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0x0b, 0xb8, 0, 0, 4, 0};
        const auto position = std::search(source.bytes.begin(), source.bytes.end(),
                                          signature.begin(), signature.end());
        expect(position != source.bytes.end(), "owned selected AAC elst signature");
        constexpr std::size_t duration_offset = 12; // Type, FullBox, entry count.
        const std::size_t duration_bytes = wide_edit ? 8 : 4;
        std::uint64_t duration = wide_edit ? 768'000'000 : 1000; // One second.
        for (std::size_t index = duration_bytes; index > 0; --index) {
            *(position + duration_offset + index - 1) = static_cast<std::uint8_t>(duration);
            duration >>= 8;
        }
        refusal(MediaPreparationFailure::unsupported,
                std::string("finite selected AAC trim, elst version=") + (wide_edit ? "1" : "0"),
                [&] { (void)prepare(source, store, context, 42); });
    }
    expect(store.seals == 0 && store.reads == 0 && store.clears == 0,
           "finite copy edits never take conversion store ownership or retry conversion");
}

void copy_table_refusals(const std::filesystem::path& policy) {
    Context context;
    Store store;
    Source copy(policy / "aac-48000-2.mp4");
    auto without_store = PreparedAutomaticMedia::open(
        copy.callbacks(), {}, 0, context.value, AudioDeliveryProfile::apple_hls_v1,
        AudioConversionPermission::preserve_original, 42);
    expect(without_store->decision().action == AudioDeliveryAction::copy &&
               without_store->decision().output_family == SourceAudioFamily::aac,
           "copy works without storage and preservation permits copying");
    without_store->stop();
    auto wrong_clock = read_mp4_source_audio(copy.reader(), copy.bytes.size(), 42);
    expect(wrong_clock.samples.size() > 1 && wrong_clock.samples.front().duration == 1024,
           "independent AAC first duration is 1024");
    wrong_clock.samples.front().duration = 512;
    for (std::size_t index = 1; index < wrong_clock.samples.size(); ++index) {
        wrong_clock.samples[index].decode_time -= 512;
    }
    try {
        (void)validate_profile_copy_audio(wrong_clock, copy.reader());
        throw std::runtime_error("short interior AAC duration copied under fixed-frame metadata");
    } catch (const RemuxException& error) {
        expect(error.reason() == RemuxFailure::unsupported, "fixed AAC packet clock refusal");
    }
    Source eac3(policy / "eac3-48000-6.mp4");
    const auto selected = read_mp4_source_audio(eac3.reader(), eac3.bytes.size(), 42);
    expect(!selected.samples.empty(), "E-AC-3 owned first unit");
    // Literal TS 102 366 bit 53 follows the admitted absent optional BSI fields.
    // Set presence without claiming to decode or advertise JOC/object metadata.
    const auto first_offset = selected.samples.front().offset;
    eac3.bytes.at(static_cast<std::size_t>(first_offset + 6)) |= 0x04;
    refusal(MediaPreparationFailure::unsupported,
            "additional E-AC-3 BSI is not implicitly copy eligible",
            [&] { (void)prepare(eac3, store, context, 42); });
    Source truncated(policy / "aac-48000-2.mp4");
    const auto marker = Bytes{'e', 's', 'd', 's'};
    const auto position =
        std::search(truncated.bytes.begin(), truncated.bytes.end(), marker.begin(), marker.end());
    expect(position != truncated.bytes.end() && truncated.bytes.end() - position > 10,
           "AAC ES descriptor field");
    // FullBox fields occupy four bytes after type; outer descriptor begins at +8.
    // A zero-length enclosing ES descriptor cannot contain its three prefix bytes.
    expect(*(position + 8) == 3, "literal ES descriptor tag");
    *(position + 9) = 0;
    refusal(MediaPreparationFailure::malformed, "AAC descriptor length cannot escape parent bounds",
            [&] { (void)prepare(truncated, store, context, 42); });
    Source missing(policy / "aac-48000-2.mp4");
    const auto invalid_context = MediaReadContext{nullptr, nullptr, context.value.deadline};
    refusal(MediaPreparationFailure::source_error, "invalid context before any dereference", [&] {
        (void)PreparedAutomaticMedia::open(missing.callbacks(), store.callbacks(), 0,
                                           invalid_context, AudioDeliveryProfile::apple_hls_v1,
                                           AudioConversionPermission::allow_stereo, 42);
    });
    context.value.deadline = std::chrono::steady_clock::now() - std::chrono::seconds(1);
    refusal(MediaPreparationFailure::timeout, "expired context before source access",
            [&] { (void)prepare(missing, store, context, 42); });
    expect(missing.calls == 0 && missing.size_calls == 0 && store.seals == 0 && store.clears == 0,
           "preflight/copy refusals never create conversion ownership");
}

#ifdef SAP2_HAS_STEREO_AAC
void converted_movie(const std::filesystem::path& path, bool main_dts) {
    Source source(path);
    Context context;
    Store store;
    const bool matroska = starts_like_matroska(source.bytes.data(), source.bytes.size());
    auto media =
        prepare(source, store, context,
                matroska ? std::optional<std::uint64_t>{} : std::optional<std::uint64_t>{42});
    expect(media->decision().action == AudioDeliveryAction::stereo_aac &&
               media->decision().source_id == 42 && media->decision().output_channels == 2 &&
               source.size_calls == 1,
           path.filename().string() + ": automatic selected stereo decision");
    const auto& hls = media->presentation();
    auto combined = read(resource(hls, "audio-init.mp4"), context.value);
    Bytes first;
    for (std::size_t id = 0; id < hls.segment_count; ++id) {
        const auto fragment = read(resource(hls, "a" + std::to_string(id) + ".m4s"), context.value);
        if (id == 0) {
            first = fragment;
        }
        combined.insert(combined.end(), fragment.begin(), fragment.end());
    }
    const auto decoded = sap2_test::MovieDecoder(combined).decode();
    expect(!decoded.empty() && media->failure() == StereoAacHlsFailure::none,
           "automatic generated AAC decodes");
    double energy = 0;
    for (std::size_t sample = 48000 * 2; sample < 96000 * 2; ++sample) {
        energy += decoded.at(sample) * decoded.at(sample);
    }
    expect(energy / 96000 > 0.0001, "selected conversion contains an audible signal");
    if (main_dts) {
        constexpr double pi = 3.14159265358979323846;
        const auto amplitude = [&](double frequency) {
            double real = 0, imaginary = 0;
            for (std::size_t sample = 48000; sample < 96000; ++sample) {
                const auto phase = 2 * pi * frequency * sample / 48000;
                real += decoded.at(sample * 2) * std::cos(phase);
                imaginary += decoded.at(sample * 2) * std::sin(phase);
            }
            return 2 * std::hypot(real, imaginary) / 48000;
        };
        expect(amplitude(310) > 0.015 && amplitude(173) < 0.003,
               "independent main DTS tone survives; earlier AAC commentary is absent");
    } else {
        expect(decoded.size() == 312000 * 2, "expanded input retains literal 6.5-second video end");
    }
    const auto seals = store.seals.load();
    expect(read(resource(hls, "a0.m4s"), context.value) == first && store.seals == seals,
           "automatic cache reuse is stable");
    media->stop();
    media->stop();
    media.reset();
    expect(store.clears == 1, "automatic conversion clears its owned store once");
}
void conversion_failure(const std::filesystem::path& path) {
    Source source(path);
    Context context;
    Store store;
    store.fail_seal = true;
    auto media = prepare(source, store, context, 42);
    bool failed = false;
    try {
        (void)read(resource(media->presentation(), "a0.m4s"), context.value);
    } catch (const std::exception&) {
        failed = true;
    }
    expect(failed && media->failure() == StereoAacHlsFailure::cache_error &&
               media->decision().action == AudioDeliveryAction::stereo_aac &&
               media->decision().source_id == 42 && store.seals == 1,
           "conversion seal failure is terminal and never retries copied commentary");
    media->stop();
    media.reset();
    expect(store.clears == 1, "failed conversion clears its accepted store once");
}

#endif
void audio_only_progressive_checks(const std::filesystem::path& fixtures) {
    const auto mp3 = fixtures.parent_path().parent_path() / "mp3-probe" / "fixtures";
    for (const auto& path :
         {fixtures / "audio-only" / "aac.mp4", mp3 / "cbr48.mp3", mp3 / "vbr441.mp3"}) {
        Source source(path);
        Store store;
        Context context;
        for (const auto permission : {AudioConversionPermission::allow_stereo,
                                      AudioConversionPermission::preserve_original}) {
            auto media = PreparedAutomaticMedia::open(
                source.callbacks(), store.callbacks(), 0, context.value,
                AudioDeliveryProfile::apple_audio_v1, permission);
            const auto& plan = media->decision();
            expect(plan.profile == AudioDeliveryProfile::apple_audio_v1 &&
                       plan.action == AudioDeliveryAction::progressive &&
                       plan.reason == AudioDeliveryReason::profile_progressive &&
                       plan.output_channels == 2 && plan.source_id == 1,
                   path.filename().string() + ": progressive decision with either permission");
            const auto& presentation = media->presentation();
            expect(presentation.resources.size() == 1 && presentation.segment_count == 0 &&
                       media->authoring_flags() == 0,
                   "progressive route has one original resource, no HLS/authoring estimates");
            const auto& original = resource(presentation, presentation.playlist_name);
            expect(original.content_type ==
                           (path.extension() == ".mp3" ? "audio/mpeg" : "audio/mp4") &&
                       read(original, context.value) == source.bytes,
                   path.filename().string() + ": exact original bytes and inferred MIME");
            media->stop();
            media->stop();
            expect(store.seals == 0 && store.reads == 0 && store.clears == 0,
                   "progressive Stop never takes an offered conversion store");
        }
        expect(source.size_calls == 2, "one immutable host size per automatic preparation");
    }
}
void audio_only_conversion_checks(const std::filesystem::path& fixtures) {
    const std::vector<std::pair<std::filesystem::path, std::size_t>> converted{
        {fixtures / "audio-only" / "flac-stereo.mp4", 624144},
        {fixtures / "audio-only" / "pcm-51.mov", 144144},
        {fixtures / "standalone-audio" / "flac48.flac", 624144},
        {fixtures / "standalone-audio" / "pcm16.wav", 624144},
        {fixtures / "ogg-audio" / "opus48.opus", 624144},
        {fixtures / "ogg-audio" / "vorbis441.ogg", 624144},
        {fixtures / "audio-timelines" / "pcm-edit.mov", 108000},
        {fixtures / "audio-timelines" / "flac-edit441.mp4", 108000},
        {fixtures / "audio-timelines" / "pcm-trim.mka", 143904}};
    for (const auto& fixture : converted) {
        Source source(fixture.first);
        Store store;
        Context context;
        const auto scenario = fixture.first.filename().string();
        const auto open = [&](AudioConversionPermission permission) {
            return PreparedAutomaticMedia::open(source.callbacks(), store.callbacks(), 16ULL << 20,
                                                context.value, AudioDeliveryProfile::apple_audio_v1,
                                                permission);
        };
        refusal(MediaPreparationFailure::unsupported, scenario + ": preservation",
                [&] { (void)open(AudioConversionPermission::preserve_original); });
        expect(store.clears == 0 && store.seals == 0,
               "preservation refuses before store ownership");
#ifdef SAP2_HAS_STEREO_AAC
        auto media = open(AudioConversionPermission::allow_stereo);
        expect(media->decision().profile == AudioDeliveryProfile::apple_audio_v1 &&
                   media->decision().action == AudioDeliveryAction::stereo_aac &&
                   media->decision().output_channels == 2,
               scenario + ": stereo AAC decision");
        const auto& presentation = media->presentation();
        expect(presentation.video_boundaries_48khz.empty(), "audio profile has no video timeline");
        Bytes combined = read(resource(presentation, "audio-init.mp4"), context.value);
        for (std::size_t index = 0; index < presentation.segment_count; ++index) {
            const auto fragment =
                read(resource(presentation, "a" + std::to_string(index) + ".m4s"), context.value);
            combined.insert(combined.end(), fragment.begin(), fragment.end());
        }
        const auto decoded = sap2_test::MovieDecoder(combined).decode();
        expect(decoded.size() == fixture.second * 2,
               scenario + ": literal independently decoded stereo endpoint, actual=" +
                   std::to_string(decoded.size() / 2));
        media->stop();
        media->stop();
        expect(store.clears == 1 && store.fragments.empty(),
               scenario + ": one joined store cleanup");
#else
        refusal(MediaPreparationFailure::backend_unavailable, scenario + ": codec-free",
                [&] { (void)open(AudioConversionPermission::allow_stereo); });
        expect(store.clears == 0, "missing backend never accepts cache ownership");
#endif
    }
}
void audio_only_refusal_checks(const std::filesystem::path& fixtures) {
    Source video(fixtures / "audio-policy" / "aac-48000-2.mp4");
    Store store;
    Context context;
    refusal(MediaPreparationFailure::unsupported, "audio profile refuses a video container", [&] {
        (void)PreparedAutomaticMedia::open(video.callbacks(), store.callbacks(), 0, context.value,
                                           AudioDeliveryProfile::apple_audio_v1,
                                           AudioConversionPermission::allow_stereo);
    });
    expect(store.clears == 0, "video refusal precedes store ownership");
    Source protected_audio(fixtures / "audio-only" / "aac.mp4");
    const std::array<std::uint8_t, 4> plain_entry{'m', 'p', '4', 'a'},
        protected_entry{'e', 'n', 'c', 'a'};
    const auto entry = std::search(protected_audio.bytes.begin(), protected_audio.bytes.end(),
                                   plain_entry.begin(), plain_entry.end());
    expect(entry != protected_audio.bytes.end(), "owned AAC sample entry exists");
    std::copy(protected_entry.begin(), protected_entry.end(), entry);
    refusal(
        MediaPreparationFailure::unsupported, "protected audio never routes progressively", [&] {
            (void)PreparedAutomaticMedia::open(protected_audio.callbacks(), store.callbacks(), 0,
                                               context.value, AudioDeliveryProfile::apple_audio_v1,
                                               AudioConversionPermission::allow_stereo);
        });
    Source multiple(fixtures / "audio-only" / "multiple-flac.mp4");
    refusal(
        MediaPreparationFailure::selection_required, "audio-only original identity ambiguity", [&] {
            (void)PreparedAutomaticMedia::open(multiple.callbacks(), store.callbacks(), 0,
                                               context.value, AudioDeliveryProfile::apple_audio_v1,
                                               AudioConversionPermission::allow_stereo);
        });
    const auto mp3 = fixtures.parent_path().parent_path() / "mp3-probe" / "fixtures";
    for (const auto& fixture : {mp3 / "mono24.mp3", fixtures / "ogg-audio" / "chained.opus"}) {
        Source source(fixture);
        refusal(MediaPreparationFailure::unsupported,
                fixture.filename().string() + ": finite subset", [&] {
                    (void)PreparedAutomaticMedia::open(source.callbacks(), store.callbacks(), 0,
                                                       context.value,
                                                       AudioDeliveryProfile::apple_audio_v1,
                                                       AudioConversionPermission::allow_stereo);
                });
    }
    Source source(mp3 / "cbr48.mp3");
    source.bytes.pop_back();
    refusal(MediaPreparationFailure::malformed, "truncated MP3 never retries conversion", [&] {
        (void)PreparedAutomaticMedia::open(source.callbacks(), store.callbacks(), 0, context.value,
                                           AudioDeliveryProfile::apple_audio_v1,
                                           AudioConversionPermission::allow_stereo);
    });
    context.value.deadline = std::chrono::steady_clock::now();
    refusal(MediaPreparationFailure::timeout, "audio-only absolute preparation deadline", [&] {
        (void)PreparedAutomaticMedia::open(source.callbacks(), store.callbacks(), 0, context.value,
                                           AudioDeliveryProfile::apple_audio_v1,
                                           AudioConversionPermission::allow_stereo);
    });
    expect(store.clears == 0 && store.seals == 0, "all pre-route refusals leave store unused");
#ifdef SAP2_HAS_STEREO_AAC
    Context fresh;
    Source flac(fixtures / "audio-only" / "flac-stereo.mp4");
    store.fail_seal = true;
    auto media = PreparedAutomaticMedia::open(flac.callbacks(), store.callbacks(), 16ULL << 20,
                                              fresh.value, AudioDeliveryProfile::apple_audio_v1,
                                              AudioConversionPermission::allow_stereo);
    bool failed = false;
    try {
        (void)read(resource(media->presentation(), "a0.m4s"), fresh.value);
    } catch (const std::exception&) {
        failed = true;
    }
    expect(failed && media->failure() == StereoAacHlsFailure::cache_error &&
               media->decision().action == AudioDeliveryAction::stereo_aac,
           "audio-only cache failure is terminal with no progressive retry");
    media->stop();
    expect(store.clears == 1, "failed audio-only worker joins and clears once");
#endif
}
} // namespace
int main(int argc, char** argv) {
    try {
        expect(argc == 4, "policy, decoder and expanded fixture directories");
        const std::filesystem::path policy(argv[1]), decoder(argv[2]), expanded(argv[3]);
        audio_only_progressive_checks(policy.parent_path());
        audio_only_conversion_checks(policy.parent_path());
        audio_only_refusal_checks(policy.parent_path());
        for (const auto* codec : {"aac", "ac3", "eac3"}) {
            for (const auto rate : {44100, 48000}) {
                for (const auto channels : {1, 2, 6}) {
                    copied_movie(policy / (std::string(codec) + "-" + std::to_string(rate) + "-" +
                                           std::to_string(channels) + ".mp4"),
                                 channels);
                }
            }
        }
        copied_movie(policy / "aac-subtitles.mp4", 6);
        copied_movie(decoder / "aac-commentary-first.mp4", 6);
        copied_movie(decoder / "ac3-commentary-first.mkv", 6);
        copied_movie(decoder / "eac3-commentary-first.mkv", 6);
        copied_movie(decoder / "aac-commentary-first.mp4", 6, true);
        copied_movie(decoder / "ac3-commentary-first.mkv", 6, true);
        selection_and_resource_refusals(decoder, policy);
        copy_table_refusals(policy);
        finite_copy_edit_refusals(policy);
#ifdef SAP2_HAS_STEREO_AAC
        converted_movie(decoder / "dts-commentary-first.mp4", true);
        converted_movie(decoder / "dts-commentary-first.mkv", true);
        conversion_failure(decoder / "dts-commentary-first.mp4");
        for (const auto* name : {"flac.mkv", "flac.mp4", "opus.mkv", "opus.mp4", "vorbis.mkv",
                                 "pcm-s16le.mkv", "truehd.mkv"}) {
            converted_movie(expanded / name, false);
        }
#endif
        std::cout
            << "automatic policy: copy table, selected payloads, lifecycle and refusals passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "automatic policy failure: " << error.what() << '\n';
        return 1;
    }
}
