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
/**
 * @brief Resolved DNS-SD endpoint and its unauthenticated advertised metadata.
 *
 * Records own their strings/buffers; they remain valid after discover() returns.
 * This is an experimental C++17 API. Shared-library callers must use a compatible
 * compiler/runtime ABI. A versioned C discovery interface is a later gate.
 */
struct Service {
    std::string type;     // _airplay._tcp.local. or _raop._tcp.local.
    std::string instance; // Fully qualified instance name; literal dots within labels are escaped.
    std::string hostname; // Fully qualified SRV target, using the same name escaping.
    std::uint32_t interface_index = 0;  // OS interface index where records were received.
    std::uint16_t port = 0;             // Host byte order; zero-port services are omitted.
    std::vector<std::string> addresses; // IPv6 link-local addresses have %scope.
    std::map<std::string, std::optional<std::string>> txt; // Binary values; null = flag.
    std::optional<std::uint64_t> features; // Advertised mask, never a compatibility claim.
    std::optional<bool> password_required; // pw only; pairing requirements remain unknown.
};
/// Device assembled from matching advertised identities. Identity is not authenticated.
struct Device {
    std::string id;    // Normalized advertised MAC identity, or interface-scoped instance.
    std::string name;  // Friendly instance label; may contain arbitrary network-supplied bytes.
    std::string model; // Advertised model/model alias, empty when unknown.
    std::vector<Service> services; // Preserve protocol-specific endpoints and TXT.
};
struct DiscoveryOptions {
    std::uint32_t duration_ms = 5000; // 1..60000, bounded synchronous scan.
};
/// Owned final snapshot; it does not report live lifecycle events.
struct DiscoveryResult {
    std::vector<Device> devices; // Snapshot at the end of the scan.
    std::vector<std::string> warnings;
    std::uint32_t packets_received = 0; // Candidate responses accepted by the transport filter.
    std::uint32_t packets_rejected = 0; // Candidates rejected by the DNS parser.
};
/**
 * @brief Scan active IPv4 multicast interfaces and resolve AirPlay/RAOP services.
 * @param options Scan options; duration_ms must be from 1 through 60000.
 * @return Final resolved-device snapshot, warnings and response counters.
 * @throws std::invalid_argument When the requested duration is out of range.
 * @throws std::runtime_error When native transport setup or I/O fails.
 *
 * Reads both A and AAAA records but does not query IPv6-only networks. This call
 * blocks for the scan, does not retain credentials, and starts no background work.
 * An empty completed scan is not proof that no receiver exists. Advertisements
 * establish neither pairing requirements nor authenticated playback compatibility.
 */
[[nodiscard]] SAP2_API DiscoveryResult discover(const DiscoveryOptions& options = {});
} // namespace send_airplay2
#endif
