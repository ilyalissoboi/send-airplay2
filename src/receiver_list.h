// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_RECEIVER_LIST_H
#define SEND_AIRPLAY2_RECEIVER_LIST_H
#include "send_airplay2/discovery.h"
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace send_airplay2::detail {
/// One castable receiver for the C interface (receivers.h), with owned strings.
struct ReceiverEntry {
    std::string id;
    std::string name; // Exactly as advertised; may hold any bytes, including NUL.
    std::string model;
    std::string address; // Empty when the AirPlay service has no castable address.
    std::uint16_t port = 0;
    bool password_required = false;
    std::optional<std::uint64_t> features;
};

/// The AirPlay service type that marks a video receiver.
inline constexpr const char* airplay_service_type = "_airplay._tcp.local.";

/**
 * Receivers from a discovery snapshot that advertise AirPlay video, in snapshot
 * order. RAOP-only devices (speakers) are left out. For each device, the first
 * AirPlay service with a castable address supplies `address` and `port`: its
 * first IPv4 address, otherwise its first IPv6 address that is neither
 * link-local nor scoped (the media server cannot serve those). A device whose
 * AirPlay services have no castable address is kept with an empty address and
 * the first service's port. Pure function: no I/O.
 */
[[nodiscard]] std::vector<ReceiverEntry> castable_receivers(const DiscoveryResult& result);
} // namespace send_airplay2::detail
#endif
