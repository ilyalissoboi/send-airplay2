// SPDX-License-Identifier: Apache-2.0
// Public boundary only: owned offline fixtures, no credentials or receiver.
#include "send_airplay2/playback.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include <utility>

namespace {
void expect(bool condition, const std::string& scenario) {
    if (!condition) {
        throw std::runtime_error(scenario);
    }
}
struct Source {
    std::vector<std::uint8_t> bytes;
    std::size_t reads = 0, releases = 0;
    bool cancelled = false, cancel_on_read = false;
    explicit Source(const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        expect(input.good(), "missing owned fixture " + path.filename().string());
        bytes.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
    }
    sap2_media_source table() {
        return {sizeof(sap2_media_source), this, bytes.size(),
                [](void* context, std::uint64_t offset, std::uint8_t* buffer, std::size_t capacity,
                   const sap2_read_control* control) -> std::size_t {
                    auto& source = *static_cast<Source*>(context);
                    ++source.reads;
                    source.cancelled = source.cancelled || source.cancel_on_read;
                    if (sap2_read_should_stop(control) || offset >= source.bytes.size()) {
                        return 0;
                    }
                    const auto count = std::min<std::size_t>(
                        {capacity, 997, source.bytes.size() - static_cast<std::size_t>(offset)});
                    std::memcpy(buffer, source.bytes.data() + offset, count);
                    return count;
                },
                [](void* context) { ++static_cast<Source*>(context)->releases; }};
    }
};
struct Store {
    std::mutex mutex;
    std::map<std::uint64_t, std::vector<std::uint8_t>> fragments;
    std::size_t seals = 0, reads = 0, clears = 0, releases = 0;
    sap2_aac_fragment_store table() {
        return {sizeof(sap2_aac_fragment_store),
                this,
                [](void* context, std::uint64_t id, const std::uint8_t* bytes, std::size_t size,
                   const sap2_read_control* control) -> std::int32_t {
                    auto& store = *static_cast<Store*>(context);
                    std::lock_guard<std::mutex> lock(store.mutex);
                    ++store.seals;
                    if (sap2_read_should_stop(control)) {
                        return SAP2_ERROR_CANCELLED;
                    }
                    store.fragments.emplace(id, std::vector<std::uint8_t>(bytes, bytes + size));
                    return SAP2_OK;
                },
                [](void* context, std::uint64_t id, std::uint64_t offset, std::uint8_t* buffer,
                   std::size_t capacity, const sap2_read_control* control) -> std::size_t {
                    auto& store = *static_cast<Store*>(context);
                    std::lock_guard<std::mutex> lock(store.mutex);
                    ++store.reads;
                    const auto found = store.fragments.find(id);
                    if (sap2_read_should_stop(control) || found == store.fragments.end() ||
                        offset >= found->second.size()) {
                        return 0;
                    }
                    const auto count =
                        std::min(capacity, found->second.size() - static_cast<std::size_t>(offset));
                    std::memcpy(buffer, found->second.data() + offset, count);
                    return count;
                },
                [](void* context) {
                    auto& store = *static_cast<Store*>(context);
                    std::lock_guard<std::mutex> lock(store.mutex);
                    ++store.clears;
                    store.fragments.clear();
                },
                [](void* context) { ++static_cast<Store*>(context)->releases; }};
    }
};
sap2_audio_options options_for() {
    sap2_audio_options options{};
    sap2_audio_options_init(&options, sizeof(options));
    return options;
}
void copied_checks(const std::filesystem::path& policy) {
    const std::vector<std::pair<std::string, std::uint32_t>> fixtures{
        {"aac-48000-2.mp4", SAP2_AUDIO_FAMILY_AAC},
        {"ac3-44100-6.mp4", SAP2_AUDIO_FAMILY_AC3},
        {"eac3-48000-6.mp4", SAP2_AUDIO_FAMILY_EAC3},
        {"video-only.mp4", SAP2_AUDIO_FAMILY_NONE}};
    for (const auto& fixture : fixtures) {
        Source source(policy / fixture.first);
        auto input = source.table();
        Store store;
        auto cache = store.table();
        auto options = options_for();
        options.fragment_store = &cache; // No quota: copy must not use it.
        options.conversion_permission = SAP2_AUDIO_PRESERVE_ORIGINAL;
        sap2_audio_decision decision{};
        decision.struct_size = sizeof(decision);
        expect(sap2_media_check_audio(&input, &options, nullptr, nullptr, &decision) == SAP2_OK,
               fixture.first + ": offline preparation");
        const bool audio = fixture.second != SAP2_AUDIO_FAMILY_NONE;
        expect(decision.profile == 1 &&
                   decision.action ==
                       (audio ? SAP2_AUDIO_ACTION_COPY : SAP2_AUDIO_ACTION_VIDEO_ONLY) &&
                   decision.reason == (audio ? SAP2_AUDIO_REASON_PROFILE_COPY
                                             : SAP2_AUDIO_REASON_NO_ENABLED_AUDIO) &&
                   decision.selected_audio_id == (audio ? 42U : 0U) &&
                   decision.source_family == fixture.second &&
                   decision.output_family == fixture.second &&
                   decision.output_channels == (audio ? (fixture.second == 1 ? 2U : 6U) : 0U),
               fixture.first + ": original ID and resolved public fields");
        expect(source.releases == 0 && store.releases == 0 && store.seals == 0 &&
                   store.reads == 0 && store.clears == 0,
               fixture.first + ": borrowed copy never touches store or releases contexts");
        // Also exercise handle ownership without reading credentials or starting.
        sap2_cast_options cast_options{};
        sap2_cast_options_init_sized(&cast_options, sizeof(cast_options));
        cast_options.receiver_address = "127.0.0.1";
        cast_options.profile = "automatic-api-unused";
        cast_options.delivery = SAP2_DELIVERY_AUTOMATIC_AUDIO_HLS;
        cast_options.fragment_store = &cache;
        const auto reads_before = source.reads;
        sap2_cast* cast = nullptr;
        expect(sap2_cast_create(&cast_options, &input, &cast) == SAP2_OK &&
                   source.reads == reads_before,
               "automatic create performs no source/cache/receiver work");
        sap2_cast_destroy(cast);
        expect(source.releases == 1 && store.releases == 1 && store.clears == 0,
               "unstarted automatic handle releases unused contexts without Clear");
    }
}
void conversion_checks(const std::filesystem::path& decoder,
                       const std::filesystem::path& expanded) {
    const std::vector<std::pair<std::filesystem::path, std::uint32_t>> fixtures{
        {decoder / "dts-commentary-first.mp4", SAP2_AUDIO_FAMILY_DTS},
        {expanded / "flac.mkv", SAP2_AUDIO_FAMILY_FLAC},
        {expanded / "opus.mkv", SAP2_AUDIO_FAMILY_OPUS},
        {expanded / "vorbis.mkv", SAP2_AUDIO_FAMILY_VORBIS},
        {expanded / "pcm-s16le.mkv", SAP2_AUDIO_FAMILY_PCM},
        {expanded / "truehd.mkv", SAP2_AUDIO_FAMILY_TRUEHD}};
    for (const auto& fixture : fixtures) {
        Source source(fixture.first);
        auto input = source.table();
        Store store;
        auto cache = store.table();
        auto options = options_for();
        options.audio_track_id = 42;
        options.fragment_store = &cache;
        options.cache_quota_bytes = 16ULL << 20;
        sap2_audio_decision decision{};
        decision.struct_size = sizeof(decision);
        const auto result = sap2_media_check_audio(&input, &options, nullptr, nullptr, &decision);
        const auto scenario = fixture.first.filename().string();
        expect(result == (sap2_stereo_aac_available() ? SAP2_OK : SAP2_ERROR_UNSUPPORTED),
               scenario + ": backend result=" + std::to_string(result));
        if (result == SAP2_OK) {
            expect(decision.action == SAP2_AUDIO_ACTION_STEREO_AAC &&
                       decision.reason == SAP2_AUDIO_REASON_PROFILE_REQUIRES_STEREO &&
                       decision.source_family == fixture.second &&
                       decision.output_family == SAP2_AUDIO_FAMILY_AAC &&
                       decision.output_channels == 2 && decision.selected_audio_id == 42,
                   scenario + ": selected conversion decision");
        } else {
            expect(decision.action == 0, scenario + ": failed check has no decision");
        }
        expect(store.clears == (result == SAP2_OK ? 1U : 0U) && store.releases == 0 &&
                   source.releases == 0,
               scenario + ": only accepted conversion clears borrowed store once");
        options.conversion_permission = SAP2_AUDIO_PRESERVE_ORIGINAL;
        expect(sap2_media_check_audio(&input, &options, nullptr, nullptr, &decision) ==
                       SAP2_ERROR_MEDIA_UNSUPPORTED &&
                   decision.action == 0,
               scenario + ": preservation refusal cannot reuse stale decision");
    }
}
void refusal_checks(const std::filesystem::path& policy, const std::filesystem::path& decoder) {
    Source source(policy / "aac-48000-2.mp4");
    auto input = source.table();
    auto options = options_for();
    sap2_audio_decision decision{};
    decision.struct_size = sizeof(decision);
    options.audio_track_id = 99;
    expect(sap2_media_check_audio(&input, &options, nullptr, nullptr, &decision) ==
                   SAP2_ERROR_AUDIO_TRACK_INVALID &&
               decision.action == 0,
           "invalid original ID has no route fallback");
    options.audio_track_id = 0;
    source.cancel_on_read = true;
    expect(sap2_media_check_audio(
               &input, &options, &source,
               [](void* context) { return static_cast<Source*>(context)->cancelled ? 1 : 0; },
               &decision) == SAP2_ERROR_CANCELLED,
           "cancellation reaches host read controls");
    expect(source.releases == 0 && decision.action == 0, "failed check retains source ownership");
    Source dts(decoder / "dts-commentary-first.mp4");
    input = dts.table();
    expect(sap2_media_check_audio(&input, &options, nullptr, nullptr, &decision) ==
               SAP2_ERROR_AUDIO_SELECTION_REQUIRED,
           "ambiguity is resolved before codec filtering/backend access");
    options.audio_track_id = 42;
    expect(sap2_media_check_audio(&input, &options, nullptr, nullptr, &decision) ==
               (sap2_stereo_aac_available() ? SAP2_ERROR_MEDIA_CACHE : SAP2_ERROR_UNSUPPORTED),
           "conversion without storage refuses after action resolution");
}
void argument_and_layout_checks(const std::filesystem::path& policy) {
    Source source(policy / "aac-48000-2.mp4");
    auto input = source.table();
    struct FutureOptions {
        sap2_audio_options known;
        std::uint8_t tail[8];
    } future{};
    std::memset(&future, 0xa5, sizeof(future));
    sap2_audio_options_init(&future.known, sizeof(sap2_audio_options) - 1);
    expect(future.known.struct_size == 0xa5a5a5a5U, "undersized audio initializer does not write");
    sap2_audio_options_init(&future.known, sizeof(future));
    expect(future.known.struct_size == sizeof(sap2_audio_options) && future.known.profile == 1 &&
               future.known.timeout_ms == 120000 &&
               std::all_of(std::begin(future.tail), std::end(future.tail),
                           [](std::uint8_t byte) { return byte == 0xa5; }),
           "audio initializer preserves future bytes and named defaults");
    struct FutureDecision {
        sap2_audio_decision known;
        std::uint8_t tail[8];
    } output{};
    std::memset(&output, 0xa5, sizeof(output));
    output.known.struct_size = sizeof(output);
    expect(sap2_media_check_audio(&input, &future.known, nullptr, nullptr, &output.known) ==
                   SAP2_OK &&
               output.known.struct_size == sizeof(sap2_audio_decision) &&
               std::all_of(std::begin(output.tail), std::end(output.tail),
                           [](std::uint8_t byte) { return byte == 0xa5; }),
           "successful decision writes only its known prefix");
    future.known.profile = 99;
    const auto reads_before = source.reads;
    expect(sap2_media_check_audio(&input, &future.known, nullptr, nullptr, &output.known) ==
                   SAP2_ERROR_INVALID_ARGUMENT &&
               output.known.action == 0 && source.reads == reads_before,
           "unknown profile clears stale decision before source access");
    std::memset(&output, 0xa5, sizeof(output));
    output.known.struct_size = sizeof(sap2_audio_decision) - 1;
    expect(sap2_media_check_audio(&input, &future.known, nullptr, nullptr, &output.known) ==
                   SAP2_ERROR_INVALID_ARGUMENT &&
               output.known.action == 0xa5a5a5a5U,
           "truncated decision stays untouched");
}
void audio_only_public_checks(const std::filesystem::path& fixtures) {
    const auto mp3 = fixtures.parent_path().parent_path() / "mp3-probe" / "fixtures";
    const std::vector<std::pair<std::filesystem::path, std::uint32_t>> direct{
        {fixtures / "audio-only" / "aac.mp4", SAP2_AUDIO_FAMILY_AAC},
        {mp3 / "cbr48.mp3", SAP2_AUDIO_FAMILY_MP3},
        {mp3 / "vbr441.mp3", SAP2_AUDIO_FAMILY_MP3}};
    for (const auto& fixture : direct) {
        Source source(fixture.first);
        auto input = source.table();
        Store store;
        auto cache = store.table();
        auto options = options_for();
        options.profile = SAP2_AUDIO_PROFILE_APPLE_AUDIO_V1;
        options.fragment_store = &cache;
        options.conversion_permission = SAP2_AUDIO_PRESERVE_ORIGINAL;
        sap2_audio_decision decision{};
        decision.struct_size = sizeof(decision);
        expect(sap2_media_check_audio(&input, &options, nullptr, nullptr, &decision) == SAP2_OK &&
                   decision.profile == 2 && decision.action == 4 && decision.reason == 4 &&
                   decision.selected_audio_id == 1 && decision.source_family == fixture.second &&
                   decision.output_family == fixture.second && decision.output_channels == 2,
               fixture.first.filename().string() + ": literal API-6 progressive fields");
        options.audio_track_id = 99;
        expect(sap2_media_check_audio(&input, &options, nullptr, nullptr, &decision) ==
                       SAP2_ERROR_AUDIO_TRACK_INVALID &&
                   decision.action == 0,
               "explicit absent audio-only identity refuses and clears stale output");
        options.audio_track_id = 0;
        source.cancel_on_read = true;
        expect(sap2_media_check_audio(
                   &input, &options, &source,
                   [](void* value) { return static_cast<Source*>(value)->cancelled ? 1 : 0; },
                   &decision) == SAP2_ERROR_CANCELLED &&
                   decision.action == 0,
               "audio-only cancellation propagates during borrowed reads");
        source.cancel_on_read = false;
        source.cancelled = false;
        sap2_cast_options cast_options{};
        sap2_cast_options_init_sized(&cast_options, sizeof(cast_options));
        cast_options.receiver_address = "127.0.0.1";
        cast_options.profile = "audio-only-unused";
        cast_options.delivery = SAP2_DELIVERY_AUTOMATIC_AUDIO_HLS;
        cast_options.audio_profile = SAP2_AUDIO_PROFILE_APPLE_AUDIO_V1;
        cast_options.fragment_store = &cache;
        sap2_cast* cast = nullptr;
        expect(sap2_cast_create(&cast_options, &input, &cast) == SAP2_OK,
               "audio-only profile accepted at cast create");
        sap2_cast_destroy(cast);
        expect(source.releases == 1 && store.releases == 1 && store.clears == 0 &&
                   store.seals == 0 && store.reads == 0,
               "audio-only offline borrow/unstarted cast preserve unused storage ownership");
    }
    const std::vector<std::pair<std::filesystem::path, std::uint32_t>> converted{
        {fixtures / "audio-only" / "flac-stereo.mp4", SAP2_AUDIO_FAMILY_FLAC},
        {fixtures / "standalone-audio" / "pcm16.wav", SAP2_AUDIO_FAMILY_PCM},
        {fixtures / "ogg-audio" / "opus48.opus", SAP2_AUDIO_FAMILY_OPUS},
        {fixtures / "ogg-audio" / "vorbis441.ogg", SAP2_AUDIO_FAMILY_VORBIS},
        {fixtures / "audio-timelines" / "pcm-trim.mka", SAP2_AUDIO_FAMILY_PCM}};
    for (const auto& fixture : converted) {
        Source source(fixture.first);
        auto input = source.table();
        Store store;
        auto cache = store.table();
        auto options = options_for();
        options.profile = SAP2_AUDIO_PROFILE_APPLE_AUDIO_V1;
        options.fragment_store = &cache;
        options.cache_quota_bytes = 16ULL << 20;
        options.conversion_permission = SAP2_AUDIO_PRESERVE_ORIGINAL;
        sap2_audio_decision decision{};
        decision.struct_size = sizeof(decision);
        expect(sap2_media_check_audio(&input, &options, nullptr, nullptr, &decision) ==
                       SAP2_ERROR_MEDIA_UNSUPPORTED &&
                   store.clears == 0,
               "audio-only preservation refuses before store acceptance");
        options.conversion_permission = SAP2_AUDIO_ALLOW_STEREO;
        options.fragment_store = nullptr;
        expect(sap2_media_check_audio(&input, &options, nullptr, nullptr, &decision) ==
                   (sap2_stereo_aac_available() ? SAP2_ERROR_MEDIA_CACHE : SAP2_ERROR_UNSUPPORTED),
               "audio-only backend then storage refusal");
        options.fragment_store = &cache;
        const auto result = sap2_media_check_audio(&input, &options, nullptr, nullptr, &decision);
        expect(result == (sap2_stereo_aac_available() ? SAP2_OK : SAP2_ERROR_UNSUPPORTED),
               fixture.first.filename().string() + ": audio-only conversion preparation");
        if (result == SAP2_OK) {
            expect(decision.profile == 2 && decision.action == 3 && decision.reason == 3 &&
                       decision.source_family == fixture.second && decision.output_family == 1 &&
                       decision.output_channels == 2 && store.clears == 1,
                   "audio-only converted reporting and accepted store cleanup");
        }
        expect(source.releases == 0 && store.releases == 0,
               "offline audio-only conversion only borrows host contexts");
    }
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 4) {
        return 2;
    }
    try {
        copied_checks(argv[1]);
        conversion_checks(argv[2], argv[3]);
        refusal_checks(argv[1], argv[2]);
        argument_and_layout_checks(argv[1]);
        audio_only_public_checks(std::filesystem::path(argv[1]).parent_path());
        std::cout << "automatic audio C API checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "automatic audio C API: " << error.what() << '\n';
        return 1;
    }
}
