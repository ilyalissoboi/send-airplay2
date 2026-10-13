// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_HOST_FRAGMENT_STORE_H
#define SEND_AIRPLAY2_HOST_FRAGMENT_STORE_H
#include "send_airplay2/playback.h"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <vector>

namespace send_airplay2::detail {
/** Desktop host cache under an existing host-selected root. Creates an exclusive
 * child lazily; removes only known files and its empty child, never recursively.
 * Must outlive the native cast handle. The host retains this object's ownership.
 */
class HostFragmentFiles {
public:
    explicit HostFragmentFiles(std::filesystem::path root);
    ~HostFragmentFiles();
    HostFragmentFiles(const HostFragmentFiles&) = delete;
    HostFragmentFiles& operator=(const HostFragmentFiles&) = delete;
    HostFragmentFiles(HostFragmentFiles&&) = delete;
    HostFragmentFiles& operator=(HostFragmentFiles&&) = delete;
    [[nodiscard]] sap2_aac_fragment_store table() noexcept;

private:
    void reserve_directory();
    void clear() noexcept;
    static std::int32_t seal(void*, std::uint64_t, const std::uint8_t*, std::size_t,
                             const sap2_read_control*) noexcept;
    static std::size_t read(void*, std::uint64_t, std::uint64_t, std::uint8_t*, std::size_t,
                            const sap2_read_control*) noexcept;
    std::filesystem::path root_;
    std::filesystem::path directory_;
    std::vector<std::filesystem::path> files_;
    std::mutex mutex_;
};
} // namespace send_airplay2::detail
#endif
