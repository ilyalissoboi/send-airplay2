// SPDX-License-Identifier: Apache-2.0
#include "../src/runtime_validation.h"
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

namespace {
using Bytes = std::vector<std::uint8_t>;
struct Source {
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
void run(const std::filesystem::path& path, std::uint32_t mode) {
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
        !cache.sealed.empty() ||
        (mode <= 1 && (!report.audio_segments || cache.seals != report.audio_segments))) {
        throw std::runtime_error(
            "mode=" + std::to_string(mode) + " result=" + std::to_string(result) + " failure=" +
            std::to_string(report.failure) + " segments=" + std::to_string(report.audio_segments) +
            " bytes=" + std::to_string(report.resource_bytes) +
            " seals=" + std::to_string(cache.seals) + " clears=" + std::to_string(cache.clears) +
            " releases=" + std::to_string(cache.releases));
    }
}
} // namespace
int main(int argc, char** argv) {
    if (argc != 2) {
        return 2;
    }
    static_assert(sizeof(sap2_runtime_report) == 48, "private report ABI");
    for (const auto* name : {"dts.mp4", "dts.mkv", "aac.mp4"}) {
        for (std::uint32_t mode = 0; mode <= (std::string(name) == "dts.mp4" ? 5U : 4U); ++mode) {
            try {
                run(std::filesystem::path(argv[1]) / name, mode);
            } catch (const std::exception& error) {
                std::cerr << name << " " << error.what() << '\n';
                return 1;
            }
        }
    }
    std::cout << "16 offline runtime scenarios passed\n";
}
