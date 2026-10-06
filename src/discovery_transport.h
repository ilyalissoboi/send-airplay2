// SPDX-License-Identifier: Apache-2.0
#ifndef SAP2_DISCOVERY_TRANSPORT_H
#define SAP2_DISCOVERY_TRANSPORT_H
#include "discovery_core.h"
#include <memory>
namespace send_airplay2::detail {
struct Interface {
    std::uint32_t index = 0;   // OS interface index, also used for IPv6 scope IDs.
    std::uint32_t ipv4 = 0;    // Network byte order, suitable for socket APIs.
    std::uint32_t netmask = 0; // Network byte order, like ipv4.
};
struct Datagram {
    Bytes bytes;
    std::uint32_t interface_index = 0;
};
/// Native networking boundary. Cache/parser never call OS APIs.
/// A scan exclusively owns its transport; implementations need not be thread-safe.
class Transport {
public:
    virtual ~Transport() = default;
    /// Refresh multicast memberships; append per-interface failures to warnings.
    virtual std::vector<Interface> refresh(std::vector<std::string>& warnings) = 0;
    /// Send a bounded DNS query on one interface. Throws on socket failure.
    virtual void send(const Bytes& bytes, const Interface& nic) = 0;
    /// Wait at most wait_ms for a candidate response. False means timeout,
    /// interruption or an ignored datagram; packet is usable only when true.
    /// Throws on unrecoverable transport errors. Payload ownership passes to caller.
    virtual bool receive(Datagram& packet, std::uint32_t wait_ms) = 0;
};
std::unique_ptr<Transport> native_transport();
} // namespace send_airplay2::detail
#endif
