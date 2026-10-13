// SPDX-License-Identifier: Apache-2.0
#include "host_fragment_store.h"
#include <algorithm>
#include <fstream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>

namespace send_airplay2::detail {
namespace {
constexpr std::size_t max_fragment_bytes = 1024 * 1024;
constexpr std::size_t read_chunk_bytes = 64 * 1024;
std::filesystem::path fragment_path(const std::filesystem::path& directory, std::uint64_t index) {
    return directory / (std::to_string(index) + ".m4s");
}
} // namespace
HostFragmentFiles::HostFragmentFiles(std::filesystem::path root)
    : root_(std::filesystem::absolute(std::move(root))) {}
HostFragmentFiles::~HostFragmentFiles() {
    clear();
}
void HostFragmentFiles::reserve_directory() {
    if (!directory_.empty()) {
        return;
    }
    std::random_device random;
    for (unsigned attempt = 0; attempt < 16; ++attempt) {
        auto candidate =
            root_ / ("sap2-" + std::to_string(random()) + "-" + std::to_string(random()));
        // create_directory atomically refuses an existing child; never adopt it.
        if (std::filesystem::create_directory(candidate)) {
            directory_ = std::move(candidate);
            return;
        }
    }
    throw std::runtime_error("cache reservation failed");
}
sap2_aac_fragment_store HostFragmentFiles::table() noexcept {
    return {sizeof(sap2_aac_fragment_store),
            this,
            &seal,
            &read,
            [](void* context) { static_cast<HostFragmentFiles*>(context)->clear(); },
            nullptr};
}
std::int32_t HostFragmentFiles::seal(void* context, std::uint64_t index, const std::uint8_t* bytes,
                                     std::size_t length,
                                     const sap2_read_control* control) noexcept {
    try {
        if (!bytes || !length || length > max_fragment_bytes || sap2_read_should_stop(control)) {
            return SAP2_ERROR_MEDIA_CACHE;
        }
        auto& store = *static_cast<HostFragmentFiles*>(context);
        std::lock_guard<std::mutex> lock(store.mutex_);
        store.reserve_directory();
        const auto path = fragment_path(store.directory_, index);
        const auto temporary = store.directory_ / (std::to_string(index) + ".partial");
        if (std::filesystem::exists(path) || std::filesystem::exists(temporary)) {
            return SAP2_ERROR_MEDIA_CACHE;
        }
        store.files_.push_back(temporary);
        std::ofstream stream(temporary, std::ios::binary | std::ios::out);
        if (!stream) {
            return SAP2_ERROR_MEDIA_CACHE;
        }
        for (std::size_t offset = 0; offset < length;) {
            if (sap2_read_should_stop(control)) {
                return SAP2_ERROR_CANCELLED;
            }
            const auto count = std::min(read_chunk_bytes, length - offset);
            stream.write(reinterpret_cast<const char*>(bytes + offset),
                         static_cast<std::streamsize>(count));
            if (!stream) {
                return SAP2_ERROR_MEDIA_CACHE;
            }
            offset += count;
        }
        stream.close();
        if (!stream || sap2_read_should_stop(control)) {
            return SAP2_ERROR_MEDIA_CACHE;
        }
        store.files_.push_back(path);
        std::filesystem::rename(temporary, path);
        return SAP2_OK;
    } catch (...) {
        return SAP2_ERROR_MEDIA_CACHE;
    }
}
std::size_t HostFragmentFiles::read(void* context, std::uint64_t index, std::uint64_t offset,
                                    std::uint8_t* buffer, std::size_t capacity,
                                    const sap2_read_control* control) noexcept {
    try {
        if (!buffer || !capacity || capacity > read_chunk_bytes ||
            offset > static_cast<std::uint64_t>(std::numeric_limits<std::streamoff>::max()) ||
            sap2_read_should_stop(control)) {
            return 0;
        }
        auto& store = *static_cast<HostFragmentFiles*>(context);
        std::lock_guard<std::mutex> lock(store.mutex_);
        if (store.directory_.empty()) {
            return 0;
        }
        std::ifstream stream(fragment_path(store.directory_, index), std::ios::binary);
        stream.seekg(static_cast<std::streamoff>(offset));
        stream.read(reinterpret_cast<char*>(buffer), static_cast<std::streamsize>(capacity));
        return sap2_read_should_stop(control) ? 0 : static_cast<std::size_t>(stream.gcount());
    } catch (...) {
        return 0;
    }
}
void HostFragmentFiles::clear() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    std::error_code error;
    for (const auto& file : files_) {
        std::filesystem::remove(file, error);
    }
    files_.clear();
    if (!directory_.empty()) {
        std::filesystem::remove(directory_, error);
        directory_.clear();
    }
}
} // namespace send_airplay2::detail
