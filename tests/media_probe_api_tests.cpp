// SPDX-License-Identifier: Apache-2.0
#include "send_airplay2/playback.h"
#include "mp4_fixture.h"
#include "host_fragment_store.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <iostream>
#include <string>

namespace {
using namespace send_airplay2::test::mp4_fixture;
int failures = 0;
void check(bool passed, const char* scenario) {
    if (!passed) {
        std::cerr << "FAIL [media probe C API]: " << scenario << '\n';
        ++failures;
    }
}
struct Source {
    explicit Source(Bytes value = {}) : bytes(std::move(value)) {}
    Bytes bytes;
    int reads = 0;
    int releases = 0;
    bool cancel_in_read = false;
    bool cancelled = false;
    bool control_stopped = false;
    std::function<void(const sap2_read_control*)> on_read;
    sap2_media_source table() {
        return {sizeof(sap2_media_source), this, bytes.size(),
                [](void* context, std::uint64_t offset, std::uint8_t* buffer, std::size_t capacity,
                   const sap2_read_control* control) -> std::size_t {
                    auto& source = *static_cast<Source*>(context);
                    ++source.reads;
                    if (source.on_read) {
                        source.on_read(control);
                    }
                    if (source.cancel_in_read) {
                        source.cancelled = true;
                    }
                    source.control_stopped = sap2_read_should_stop(control) != 0;
                    if (source.control_stopped || offset >= source.bytes.size()) {
                        return 0;
                    }
                    // Deliberately short reads exercise the native probe's progress loop.
                    const auto count = std::min<std::size_t>(
                        {capacity, 17, source.bytes.size() - static_cast<std::size_t>(offset)});
                    std::memcpy(buffer, source.bytes.data() + offset, count);
                    return count;
                },
                [](void* context) { ++static_cast<Source*>(context)->releases; }};
    }
};
struct Cache {
    int clears = 0, releases = 0, seals = 0, reads = 0;
    sap2_aac_fragment_store table() {
        return {sizeof(sap2_aac_fragment_store),
                this,
                [](void* context, std::uint64_t, const std::uint8_t*, std::size_t,
                   const sap2_read_control*) {
                    ++static_cast<Cache*>(context)->seals;
                    return SAP2_ERROR_MEDIA_CACHE;
                },
                [](void* context, std::uint64_t, std::uint64_t, std::uint8_t*, std::size_t,
                   const sap2_read_control*) -> std::size_t {
                    ++static_cast<Cache*>(context)->reads;
                    return 0;
                },
                [](void* context) { ++static_cast<Cache*>(context)->clears; },
                [](void* context) { ++static_cast<Cache*>(context)->releases; }};
    }
};
void probe_metadata() {
    auto audio = audio_spec();
    audio.source_id = 47;
    Source source{mp4_file({video_spec(), audio})};
    auto table = source.table();
    sap2_track_list* tracks = nullptr;
    check(sap2_media_probe(&table, 10000, nullptr, nullptr, &tracks) == SAP2_OK,
          "short-read MP4 probe");
    check(sap2_track_list_count(tracks) == 2 &&
              sap2_track_list_container(tracks) == SAP2_CONTAINER_MP4,
          "original inventory and container");
    sap2_media_track track{};
    track.struct_size = sizeof(track);
    check(sap2_track_list_get(tracks, 1, &track) == SAP2_OK && track.id == 47 &&
              track.kind == SAP2_TRACK_AUDIO && track.enabled && std::string(track.codec) == "mp4a",
          "original source ID and codec, not filtered index");
    check(sap2_track_list_get(tracks, 2, &track) == SAP2_ERROR_INVALID_ARGUMENT,
          "out-of-range list index");
    sap2_track_list_free(tracks);
    check(source.reads > 2 && !source.releases, "borrowed probe never releases source");
    source.cancel_in_read = true;
    tracks = nullptr;
    check(sap2_media_probe(
              &table, 10000, &source,
              [](void* context) { return static_cast<Source*>(context)->cancelled ? 1 : 0; },
              &tracks) == SAP2_ERROR_CANCELLED &&
              !tracks && source.control_stopped,
          "cancellation is polled inside the host read control");
    check(!source.releases, "failed probe also retains caller ownership");
}
void create_ownership() {
    Source source{};
    Cache cache;
    auto source_table = source.table();
    auto cache_table = cache.table();
    sap2_cast_options options{};
    sap2_cast_options_init_sized(&options, sizeof(options));
    options.receiver_address = "127.0.0.1";
    options.profile = "media-api-absent";
    options.delivery = SAP2_DELIVERY_STEREO_AAC_HLS;
    options.cache_quota_bytes = 1024 * 1024;
    options.fragment_store = &cache_table;
    sap2_cast* cast = nullptr;
    const auto result = sap2_cast_create(&options, &source_table, &cast);
    check(!source.reads && !cache.reads && !cache.seals && !cache.clears && !cache.releases &&
              !source.releases,
          "create performs no callbacks and transfers only on success");
    if (sap2_stereo_aac_available()) {
        check(result == SAP2_OK && cast, "enabled backend accepts explicit mode");
        sap2_cast_stop(cast);
        sap2_cast_stop(cast);
        sap2_cast_status status{};
        status.struct_size = sizeof(status);
        check(sap2_cast_get_status(cast, &status) == SAP2_OK && status.media_cleaned_up,
              "never-started Stop reports media cleanup complete");
        check(cache.clears == 1 && !cache.releases && !source.releases,
              "Stop clears once and retains ownership until destroy");
        sap2_cast_destroy(cast);
        check(cache.clears == 1 && cache.releases == 1 && source.releases == 1,
              "destroy releases cache/source once after cleanup");
        Source failed_source{};
        Cache failed_cache;
        auto failed_source_table = failed_source.table();
        auto failed_cache_table = failed_cache.table();
        sap2_credential_store credentials{sizeof(sap2_credential_store), nullptr,
                                          [](void*, const char*, std::uint8_t*, std::size_t,
                                             std::size_t*) { return SAP2_STORE_ABSENT; },
                                          [](void*, const char*, const std::uint8_t*, std::size_t) {
                                              return SAP2_STORE_UNAVAILABLE;
                                          },
                                          [](void*, const char*) { return SAP2_STORE_ABSENT; }};
        options.credential_store = &credentials;
        options.fragment_store = &failed_cache_table;
        cast = nullptr;
        check(sap2_cast_create(&options, &failed_source_table, &cast) == SAP2_OK,
              "second owned cache create");
        check(sap2_cast_start(cast) == SAP2_ERROR_PROFILE_NOT_FOUND && failed_cache.clears == 1 &&
                  !failed_source.reads,
              "failed credential gate clears cache before any source access");
        sap2_cast_destroy(cast);
        check(failed_cache.clears == 1 && failed_cache.releases == 1 && failed_source.releases == 1,
              "failed startup destroy preserves exact ownership cleanup");
    } else {
        check(result == SAP2_ERROR_UNSUPPORTED && !cast,
              "backend absent rejects before ownership or I/O");
    }
    options.delivery = SAP2_DELIVERY_HLS_REMUX;
    options.credential_store = nullptr;
    options.fragment_store = &cache_table;
    cast = nullptr;
    check(sap2_cast_create(&options, &source_table, &cast) == SAP2_ERROR_INVALID_ARGUMENT && !cast,
          "legacy delivery rejects conversion-only fields");
}
void filesystem_cache_callbacks() {
    const auto root = std::filesystem::temp_directory_path() /
                      ("sap2-cache-contract-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    if (!std::filesystem::create_directory(root)) {
        throw std::runtime_error("test cache root collision");
    }
    const auto sibling = root / "keep.txt";
    {
        std::ofstream marker(sibling);
        marker << "host-owned sibling";
    }
    {
        send_airplay2::detail::HostFragmentFiles cache(root);
        auto table = cache.table();
        Source source{mp4_file({video_spec(), audio_spec()})};
        bool checked = false;
        source.on_read = [&](const sap2_read_control* control) {
            if (checked) {
                return;
            }
            checked = true;
            // Use the control while borrowed inside this native callback only.
            const std::uint8_t bytes[]{10, 20, 30, 40, 50, 60};
            check(table.seal(table.context, 9, bytes, sizeof(bytes), control) == SAP2_OK,
                  "filesystem callback atomically seals complete bytes");
            std::uint8_t output[4]{};
            check(table.read_at(table.context, 9, 2, output, sizeof(output), control) == 4 &&
                      output[0] == 30 && output[3] == 60,
                  "filesystem callback positional bytes");
            check(table.seal(table.context, 9, bytes, sizeof(bytes), control) ==
                      SAP2_ERROR_MEDIA_CACHE,
                  "sealed fragment cannot be overwritten");
        };
        auto source_table = source.table();
        sap2_track_list* tracks = nullptr;
        check(sap2_media_probe(&source_table, 10000, nullptr, nullptr, &tracks) == SAP2_OK &&
                  checked,
              "real native read control reaches desktop host cache callbacks");
        sap2_track_list_free(tracks);
        table.clear(table.context);
        check(std::filesystem::exists(sibling) &&
                  std::distance(std::filesystem::directory_iterator(root),
                                std::filesystem::directory_iterator{}) == 1,
              "clear removes only the cast child and preserves host sibling");
    }
    std::filesystem::remove(sibling);
    std::filesystem::remove(root); // Empty test root only, never recursive deletion.
}
} // namespace
int main() {
    probe_metadata();
    create_ownership();
    filesystem_cache_callbacks();
    return failures ? 1 : 0;
}
