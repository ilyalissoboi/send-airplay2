// SPDX-License-Identifier: Apache-2.0
#include "../src/runtime_validation.h"
#include <algorithm>
#include <array>
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
#include <sstream>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
struct Source {
    Source() = default;
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;
    Source(Source&&) = delete;
    Source& operator=(Source&&) = delete;
    Bytes bytes;
    unsigned released = 0;
    std::size_t read_limit = 1021;
    std::mutex mutex;
    static std::size_t read(void* pointer, std::uint64_t offset, std::uint8_t* out,
                            std::size_t capacity, const sap2_read_control* control) noexcept {
        auto& self = *static_cast<Source*>(pointer);
        std::lock_guard<std::mutex> lock(self.mutex);
        if (sap2_read_should_stop(control) || offset >= self.bytes.size()) {
            return 0;
        }
        const auto size = std::min(
            {capacity, self.read_limit, self.bytes.size() - static_cast<std::size_t>(offset)});
        std::memcpy(out, self.bytes.data() + offset, size);
        return size;
    }
    static void release(void* pointer) noexcept {
        ++static_cast<Source*>(pointer)->released;
    }
    sap2_media_source table() noexcept {
        return {sizeof(sap2_media_source), this, bytes.size(), read, release};
    }
};
struct Cache {
    Cache() = default;
    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;
    Cache(Cache&&) = delete;
    Cache& operator=(Cache&&) = delete;
    std::map<std::uint64_t, Bytes> sealed;
    unsigned seals = 0, clears = 0, releases = 0;
    bool fail_read = false;
    std::mutex mutex;
    static std::int32_t seal(void* pointer, std::uint64_t index, const std::uint8_t* bytes,
                             std::size_t count, const sap2_read_control* control) noexcept {
        try {
            auto& self = *static_cast<Cache*>(pointer);
            std::lock_guard<std::mutex> lock(self.mutex);
            if (sap2_read_should_stop(control) || self.sealed.count(index)) {
                return SAP2_ERROR_MEDIA_CACHE;
            }
            self.sealed.emplace(index, Bytes(bytes, bytes + count));
            ++self.seals;
            return SAP2_OK;
        } catch (...) {
            return SAP2_ERROR_MEDIA_CACHE;
        }
    }
    static std::size_t read(void* pointer, std::uint64_t index, std::uint64_t offset,
                            std::uint8_t* out, std::size_t capacity,
                            const sap2_read_control* control) noexcept {
        auto& self = *static_cast<Cache*>(pointer);
        std::lock_guard<std::mutex> lock(self.mutex);
        const auto found = self.sealed.find(index);
        if (self.fail_read || sap2_read_should_stop(control) || found == self.sealed.end() ||
            offset >= found->second.size()) {
            return 0;
        }
        const auto count = std::min(
            {capacity, std::size_t{997}, found->second.size() - static_cast<std::size_t>(offset)});
        std::memcpy(out, found->second.data() + offset, count);
        return count;
    }
    static void clear(void* pointer) noexcept {
        auto& self = *static_cast<Cache*>(pointer);
        std::lock_guard<std::mutex> lock(self.mutex);
        ++self.clears;
        self.sealed.clear();
    }
    static void release(void* pointer) noexcept {
        ++static_cast<Cache*>(pointer)->releases;
    }
    sap2_aac_fragment_store table() noexcept {
        return {sizeof(sap2_aac_fragment_store), this, seal, read, clear, release};
    }
};
struct ValidationScenario {
    const char* name;
    std::uint32_t mode;
    std::uint32_t expected_failure;
    std::uint64_t expected_segments;
    unsigned expected_seals;
};
// Independent fixture expectations: all three owned movies have 16.5 s of video,
// hence three audio fragments; failure values are the fixed C API categories.
constexpr std::array<ValidationScenario, 2> delivery_scenarios{
    {{"cold-concurrent-cached-reads", 0, 0, 3, 3}, {"cache-cancellation-and-retry", 1, 0, 3, 3}}};
constexpr std::array<ValidationScenario, 3> failure_scenarios{
    {{"terminal-cache-read-failure", 2, 6, 3, 1},
     {"terminal-source-refusal", 3, 3, 3, 0},
     {"invalid-original-track", 4, 0, 0, 0}}};
constexpr ValidationScenario read_budget_refusal{"tiny-read-budget-refusal", 5, 5, 0, 0};
void validate_scenario(const std::filesystem::path& path, const ValidationScenario& scenario) {
    const auto mode = scenario.mode;
    Source source;
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("fixture open");
    }
    source.bytes = Bytes(std::istreambuf_iterator<char>(file), {});
    source.read_limit = mode == 5 ? 97 : 1021;
    Cache cache;
    cache.fail_read = mode == 2;
    auto source_table = source.table();
    auto cache_table = cache.table();
    sap2_runtime_report report{};
    report.struct_size = sizeof(report);
    const auto result =
        sap2_runtime_validate(&source_table, &cache_table, mode == 4 ? 11 : 42, mode, &report);
    if (result != SAP2_OK || source.released != 1 || cache.clears != 1 || cache.releases != 1 ||
        !cache.sealed.empty() || report.failure != scenario.expected_failure ||
        report.audio_segments != scenario.expected_segments ||
        cache.seals != scenario.expected_seals) {
        throw std::runtime_error(
            "mode=" + std::to_string(mode) + " result=" + std::to_string(result) + " failure=" +
            std::to_string(report.failure) + " segments=" + std::to_string(report.audio_segments) +
            " bytes=" + std::to_string(report.resource_bytes) +
            " seals=" + std::to_string(cache.seals) + " clears=" + std::to_string(cache.clears) +
            " releases=" + std::to_string(cache.releases));
    }
}
void automatic_scenarios(const std::filesystem::path& fixtures,
                         const std::filesystem::path& catalog) {
    std::ifstream cases(catalog);
    if (!cases) {
        throw std::runtime_error("automatic catalog open");
    }
    std::string line;
    unsigned count = 0;
    while (std::getline(cases, line)) {
        if (line.empty() || line.front() == '#') {
            continue;
        }
        std::istringstream row(line);
        std::string group, filename;
        std::uint32_t action = 0, family = 0, channels = 0;
        std::uint64_t segments = 0;
        bool stress = false;
        if (!(row >> group >> filename >> action >> family >> channels >> segments >> stress)) {
            throw std::runtime_error("automatic catalog row");
        }
        const bool converted = action == SAP2_AUDIO_ACTION_STEREO_AAC;
        std::vector<std::uint32_t> modes = !stress ? std::vector<std::uint32_t>{0}
                                           : converted
                                               ? std::vector<std::uint32_t>{0, 1, 2, 3, 4, 6, 7, 8}
                                               : std::vector<std::uint32_t>{0, 1, 3, 4, 6, 7};
        if (stress && family == SAP2_AUDIO_FAMILY_DTS) {
            modes.insert(modes.end(), {9, 10, 11});
        }
        for (const auto mode : modes) {
            Source source;
            std::ifstream input(fixtures / group / filename, std::ios::binary);
            if (!input) {
                throw std::runtime_error("automatic fixture open " + filename);
            }
            source.bytes = Bytes(std::istreambuf_iterator<char>(input), {});
            Cache cache;
            cache.fail_read = mode == 2;
            auto source_table = source.table();
            auto cache_table = cache.table();
            sap2_runtime_automatic_report report{};
            report.media.struct_size = sizeof(report);
            const auto selected = mode == 4                                ? 11U
                                  : action == SAP2_AUDIO_ACTION_VIDEO_ONLY ? 0U
                                                                           : 42U;
            const auto result = sap2_runtime_validate_automatic(
                &source_table, &cache_table, selected, mode == 8 ? 1ULL : 512ULL * 1024 * 1024,
                mode, &report);
            const auto expected_result = mode == 4                ? SAP2_ERROR_AUDIO_TRACK_INVALID
                                         : mode == 6 && converted ? SAP2_ERROR_MEDIA_UNSUPPORTED
                                         : mode == 7              ? SAP2_ERROR_CANCELLED
                                         : mode == 8              ? SAP2_ERROR_MEDIA_LIMIT
                                                                  : SAP2_OK;
            const bool prepared = expected_result == SAP2_OK;
            const auto expected_clears = converted && mode != 4 && mode != 6 && mode != 7 ? 1U : 0U;
            const auto expected_failure = mode == 2                ? SAP2_MEDIA_FAILURE_CACHE
                                          : mode == 3 && converted ? SAP2_MEDIA_FAILURE_SOURCE
                                          : mode == 8              ? SAP2_MEDIA_FAILURE_LIMIT
                                                                   : SAP2_MEDIA_FAILURE_NONE;
            const auto scenario = filename + " mode=" + std::to_string(mode);
            if (result != SAP2_OK || report.preparation_result != expected_result ||
                source.released != 1 || cache.releases != 1 || cache.clears != expected_clears ||
                report.media.failure != expected_failure ||
                (prepared &&
                 (report.decision.action != action || report.decision.source_family != family ||
                  report.decision.output_channels != channels ||
                  report.media.audio_segments != segments ||
                  report.decision.selected_audio_id != (action == 1 ? 0U : 42U))) ||
                (!prepared && report.decision.struct_size != 0) ||
                (!converted && (cache.seals != 0 || !cache.sealed.empty())) ||
                (mode >= 9 &&
                 (report.media.resource_bytes == 0 || report.preparation_read_calls == 0 ||
                  report.request_read_calls == 0 ||
                  report.preparation_requested_bytes < report.preparation_actual_bytes ||
                  report.request_requested_bytes < report.request_actual_bytes ||
                  cache.seals != 1 || report.post_stop_refused != 1)) ||
                (prepared && converted && mode <= 1 && report.post_stop_refused != 1)) {
                throw std::runtime_error(
                    scenario + " result=" + std::to_string(result) +
                    " preparation=" + std::to_string(report.preparation_result) +
                    " failure=" + std::to_string(report.media.failure) +
                    " segments=" + std::to_string(report.media.audio_segments) +
                    " clear=" + std::to_string(cache.clears));
            }
            ++count;
        }
    }
    std::cout << count << " automatic runtime scenarios passed\n";
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 4) {
        return 2;
    }
    static_assert(sizeof(sap2_runtime_report) == 48, "private report ABI");
    static_assert(sizeof(sap2_runtime_automatic_report) == 160, "private automatic report ABI");
    for (const auto* name : {"dts.mp4", "dts.mkv", "aac.mp4"}) {
        const auto path = std::filesystem::path(argv[1]) / name;
        const auto check = [&](const ValidationScenario& scenario) {
            try {
                validate_scenario(path, scenario);
                return true;
            } catch (const std::exception& error) {
                std::cerr << name << " " << scenario.name << " " << error.what() << '\n';
                return false;
            }
        };
        for (const auto& scenario : delivery_scenarios) {
            if (!check(scenario)) {
                return 1;
            }
        }
        for (const auto& scenario : failure_scenarios) {
            if (!check(scenario)) {
                return 1;
            }
        }
        if (std::string(name) == "dts.mp4" && !check(read_budget_refusal)) {
            return 1;
        }
    }
    std::cout << "16 offline runtime scenarios passed\n";
    try {
        automatic_scenarios(argv[2], argv[3]);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
