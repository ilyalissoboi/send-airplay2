// SPDX-License-Identifier: Apache-2.0
// Link-time test sender for the actual CLI formatter; never linked in production.
#include "send_airplay2/discovery.h"
#include "auth_cli.h"
#include "serve_cli.h"
namespace send_airplay2::detail {
// This executable exercises discovery formatting only; auth has its own tests.
int run_auth_cli(int, const char* const*) {
    return 2;
}
// Media serving has its own tests; this fixture never opens files or sockets.
int run_serve_cli(int, const char* const*) {
    return 2;
}
} // namespace send_airplay2::detail
namespace send_airplay2 {
DiscoveryResult discover(const DiscoveryOptions&) {
    Service service;
    service.type = "_airplay._tcp.local.";
    service.instance = "Fixture._airplay._tcp.local.";
    service.hostname = "fixture.local.";
    service.interface_index = 7;
    service.port = 7000;
    service.addresses = {"192.0.2.10", "fe80:0:0:0:0:0:0:1%7"};
    service.features = UINT64_MAX;
    service.password_required = false;
    service.txt = {{"flag", std::nullopt},
                   {"empty", ""},
                   {"binary", std::string("\0\xff", 2)},
                   {"utf8", std::string("\xc3\xa9", 2)},
                   // Overlong, surrogate, out-of-range and truncated sequences.
                   {"invalid_utf8", std::string("\xc0\x80\xed\xa0\x80\xf4\x90\x80\x80\xc2", 10)}};
    Device device;
    device.id = "fixture";
    device.name = "TV\"\\\n\x1b";
    device.model = "synthetic";
    device.services = {service};
    DiscoveryResult result;
    result.devices = {device};
    result.packets_received = 1;
    return result;
}
} // namespace send_airplay2
