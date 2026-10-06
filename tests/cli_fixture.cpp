// SPDX-License-Identifier: Apache-2.0
// Link-time test sender for the actual CLI formatter; never linked in production.
#include "send_airplay2/discovery.h"
namespace send_airplay2 {
DiscoveryResult discover(const DiscoveryOptions&) {
    Service s;
    s.type = "_airplay._tcp.local."; s.instance = "Fixture._airplay._tcp.local.";
    s.hostname = "fixture.local."; s.interface_index = 7; s.port = 7000;
    s.addresses = {"192.0.2.10", "fe80:0:0:0:0:0:0:1%7"};
    s.features = UINT64_MAX; s.password_required = false;
    s.txt = {{"flag", std::nullopt}, {"empty", ""}, {"binary", std::string("\0\xff", 2)}};
    Device d; d.id = "fixture"; d.name = "TV\"\\\n\x1b"; d.model = "synthetic"; d.services = {s};
    DiscoveryResult r; r.devices = {d}; r.packets_received = 1; return r;
}
}
