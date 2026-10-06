// SPDX-License-Identifier: Apache-2.0
#ifndef SAP2_DISCOVERY_TRANSPORT_H
#define SAP2_DISCOVERY_TRANSPORT_H
#include "discovery_core.h"
#include <memory>
namespace send_airplay2::detail {
struct Interface { std::uint32_t index; std::uint32_t ipv4; std::uint32_t netmask; };
struct Datagram { Bytes bytes; std::uint32_t interface_index = 0; };
// Native networking stays behind this boundary; cache/parser have no OS calls.
class Transport {
public:
    virtual ~Transport() = default;
    virtual std::vector<Interface> refresh(std::vector<std::string>& warnings) = 0;
    virtual void send(const Bytes& bytes, const Interface& interface) = 0;
    virtual bool receive(Datagram& packet, std::uint32_t wait_ms) = 0;
};
std::unique_ptr<Transport> native_transport();
}
#endif
