// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_DISCOVERY_H
#define SEND_AIRPLAY2_DISCOVERY_H
#include "send_airplay2/http_range.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace send_airplay2 {
// Experimental C++17 API. Shared-library callers must use a compatible C++
// runtime/ABI. A versioned C discovery interface is a later integration gate.
struct Service {
    std::string type; // _airplay._tcp.local. or _raop._tcp.local.
    std::string instance;
    std::string hostname;
    std::uint32_t interface_index = 0;
    std::uint16_t port = 0;
    std::vector<std::string> addresses; // IPv6 link-local addresses have %scope.
    std::map<std::string, std::optional<std::string>> txt; // Binary values; null = flag.
    std::optional<std::uint64_t> features; // Advertised mask, never a compatibility claim.
    std::optional<bool> password_required; // pw only; pairing requirements remain unknown.
};
struct Device {
    std::string id; // Normalized advertised MAC identity, or interface-scoped instance.
    std::string name;
    std::string model;
    std::vector<Service> services; // Preserve protocol-specific endpoints and TXT.
};
struct DiscoveryOptions {
    std::uint32_t duration_ms = 5000; // 1..60000, bounded synchronous scan.
};
struct DiscoveryResult {
    std::vector<Device> devices; // Snapshot at the end of the scan.
    std::vector<std::string> warnings;
    std::uint32_t packets_received = 0;
    std::uint32_t packets_rejected = 0;
};
// Uses active IPv4 multicast interfaces; reads both A and AAAA records.
// Does not pair, authenticate, play, retain credentials or run background work.
// Throws std::invalid_argument for options, std::runtime_error for transport
// failure. Empty results are successful scans, not proof no receiver exists.
SAP2_API DiscoveryResult discover(const DiscoveryOptions& options = {});
}
#endif
