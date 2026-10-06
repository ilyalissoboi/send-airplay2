// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_FILE_MEDIA_SOURCE_H
#define SEND_AIRPLAY2_FILE_MEDIA_SOURCE_H
#include "send_airplay2/media_server.h"
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>

namespace send_airplay2::detail {
/** Aggregate read counters for diagnostics. Records offsets and byte counts only,
 * never file contents or URLs. Updated concurrently by source workers; each field
 * is individually atomic, so a snapshot taken during serving may be inconsistent
 * across fields. Read it after MediaServer::stop() for a final summary. */
struct FileReadStats {
    std::atomic<std::uint64_t> reads{0};                  ///< read_at calls that returned bytes.
    std::atomic<std::uint64_t> bytes{0};                  ///< Total bytes returned by those calls.
    std::atomic<std::uint64_t> failures{0};               ///< Zero-byte calls (EOF/error/cancel).
    std::atomic<std::uint64_t> lowest_offset{UINT64_MAX}; ///< First byte of any read.
    std::atomic<std::uint64_t> highest_end{0};            ///< One past the last byte of any read.
};

/** Open a regular file as an immutable MediaSource for the development CLI.
 *
 * The file is opened once, in binary mode, and its size is snapshotted here; the
 * returned size() reports that snapshot. The host must not modify the file while
 * it is served: a shorter file makes the affected request fail (read_at returns
 * zero), and appended bytes are never served. Reads take one internal mutex and
 * seek/read the shared stream, so concurrent workers are serialized; this keeps
 * the adapter portable at the cost of parallel throughput.
 *
 * The returned callbacks own the stream and stats reference, and are safe to call
 * from multiple threads. read_at honors MediaReadContext::should_stop() before
 * each read; one read is bounded by the server's 64-KiB capacity.
 *
 * @throws std::invalid_argument if the path is not an existing regular file.
 * @throws std::runtime_error if the file cannot be opened or its size exceeds the
 *         stream offset range. Messages omit the path.
 */
[[nodiscard]] MediaSource open_file_media_source(const std::filesystem::path& path,
                                                 std::shared_ptr<FileReadStats> stats);
} // namespace send_airplay2::detail
#endif
