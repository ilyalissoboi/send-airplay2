// SPDX-License-Identifier: Apache-2.0
#include "file_media_source.h"
#include <algorithm>
#include <fstream>
#include <ios>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace send_airplay2::detail {
namespace {
/// One shared stream; std::ifstream positions are not thread-safe, so every
/// seek/read pair runs under the mutex.
struct SharedFile {
    std::mutex mutex;
    std::ifstream stream;
};

void record_min(std::atomic<std::uint64_t>& target, std::uint64_t value) {
    auto current = target.load(std::memory_order_relaxed);
    while (value < current && !target.compare_exchange_weak(current, value)) {
    }
}

void record_max(std::atomic<std::uint64_t>& target, std::uint64_t value) {
    auto current = target.load(std::memory_order_relaxed);
    while (value > current && !target.compare_exchange_weak(current, value)) {
    }
}

/// Returns 0..capacity bytes. Zero means end of file, a stream error or a
/// cancellation; the server treats zero before the declared end as a failure.
std::size_t read_shared(SharedFile& file, std::uint64_t offset, std::uint8_t* output,
                        std::size_t capacity, const MediaReadContext& context) {
    std::lock_guard<std::mutex> lock(file.mutex);
    // Check after acquiring the lock: waiting for another worker can outlast a deadline.
    if (context.should_stop()) {
        return 0;
    }
    // Clear EOF/fail bits left by an earlier short read before seeking again.
    file.stream.clear();
    // The size snapshot is bounded to streamoff at open, so every in-range offset fits.
    file.stream.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file.stream) {
        return 0;
    }
    const auto request = std::min<std::size_t>(
        capacity, static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()));
    file.stream.read(reinterpret_cast<char*>(output), static_cast<std::streamsize>(request));
    return static_cast<std::size_t>(file.stream.gcount());
}
} // namespace

MediaSource open_file_media_source(const std::filesystem::path& path,
                                   std::shared_ptr<FileReadStats> stats) {
    if (!stats) {
        throw std::invalid_argument("file source requires a statistics owner");
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        throw std::invalid_argument("media path is not an existing regular file");
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error) {
        throw std::runtime_error("media file size is unavailable");
    }
    if (size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamoff>::max())) {
        throw std::runtime_error("media file is too large for stream offsets");
    }
    auto file = std::make_shared<SharedFile>();
    file->stream.open(path, std::ios::in | std::ios::binary);
    if (!file->stream.is_open()) {
        throw std::runtime_error("media file cannot be opened for reading");
    }
    const auto snapshot = static_cast<std::uint64_t>(size);
    return {[snapshot] { return snapshot; },
            [file = std::move(file),
             stats = std::move(stats)](std::uint64_t offset, std::uint8_t* output,
                                       std::size_t capacity, const MediaReadContext& context) {
                const auto count = read_shared(*file, offset, output, capacity, context);
                if (count == 0) {
                    ++stats->failures;
                    return count;
                }
                ++stats->reads;
                stats->bytes += count;
                record_min(stats->lowest_offset, offset);
                record_max(stats->highest_end, offset + count);
                return count;
            }};
}
} // namespace send_airplay2::detail
