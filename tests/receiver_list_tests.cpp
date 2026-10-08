// SPDX-License-Identifier: Apache-2.0
// Receiver selection and the C discovery boundary (receivers.h), without a
// network: this file defines send_airplay2::discover() itself and links only
// receiver_list.cpp and discovery_api.cpp. All devices are synthetic; addresses
// come from the RFC 5737 and RFC 3849 documentation ranges.
#include "send_airplay2/receivers.h"
#include "send_airplay2/discovery.h"
#include "receiver_list.h"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

using namespace send_airplay2;
using send_airplay2::detail::airplay_service_type;
using send_airplay2::detail::castable_receivers;

namespace {
int failures = 0;
void check(bool passed, const std::string& scenario) {
    if (!passed) {
        std::cerr << "FAIL: " << scenario << '\n';
        ++failures;
    }
}

constexpr const char* raop_service_type = "_raop._tcp.local.";
constexpr std::uint16_t airplay_port = 7000;

/// What the scripted discover() does on its next call.
enum class ScanScript { result, invalid_argument, runtime_error, out_of_memory, unknown };
ScanScript scan_script = ScanScript::result;
DiscoveryResult scripted_result;
std::uint32_t last_duration_ms = 0;
int scans = 0;

Service service(const char* type, std::uint16_t port, std::vector<std::string> addresses) {
    Service result;
    result.type = type;
    result.port = port;
    result.addresses = std::move(addresses);
    return result;
}

Device device(const char* id, std::string name, std::vector<Service> services) {
    Device result;
    result.id = id;
    result.name = std::move(name);
    result.model = "Model1,1";
    result.services = std::move(services);
    return result;
}

DiscoveryResult snapshot(std::vector<Device> devices) {
    DiscoveryResult result;
    result.devices = std::move(devices);
    return result;
}

/// Address chosen for one device with a single AirPlay service, or "<none>".
std::string chosen_address(std::vector<std::string> addresses) {
    const auto receivers = castable_receivers(
        snapshot({device("AA:BB:CC:DD:EE:01", "TV",
                         {service(airplay_service_type, airplay_port, std::move(addresses))})}));
    return receivers.size() == 1 ? receivers[0].address : "<none>";
}

void address_selection_tests() {
    check(chosen_address({"2001:db8::1", "192.0.2.10"}) == "192.0.2.10",
          "IPv4 preferred over a global IPv6 listed first");
    check(chosen_address({"fe80::1%7", "2001:db8::2"}) == "2001:db8::2",
          "scoped link-local skipped for a global IPv6");
    check(chosen_address({"FE80::1", "febf::1", "2001:db8::3"}) == "2001:db8::3",
          "unscoped fe80::/10, in either case, skipped");
    check(chosen_address({"fe8::1"}) == "fe8::1", "fe8:: is 0fe8::, outside fe80::/10");
    check(chosen_address({"fec0::1"}) == "fec0::1", "fec0:: is outside fe80::/10");
    check(chosen_address({"2001:db8::4%3"}) == "", "any scoped address is not castable");
    check(chosen_address({"fe80::1%7"}) == "", "link-local only: kept with no address");
    check(chosen_address({}) == "", "no addresses: kept with no address");
}

void service_selection_tests() {
    const auto receivers = castable_receivers(snapshot({
        device("AA:BB:CC:DD:EE:01", "Speaker", {service(raop_service_type, 7000, {"192.0.2.1"})}),
        device("AA:BB:CC:DD:EE:02", "Zero port", {service(airplay_service_type, 0, {"192.0.2.2"})}),
        device("AA:BB:CC:DD:EE:03", "Second service",
               {service(raop_service_type, 7001, {"192.0.2.3"}),
                service(airplay_service_type, 7002, {"fe80::3%4"}),
                service(airplay_service_type, 7003, {"192.0.2.33"})}),
        device("AA:BB:CC:DD:EE:04", "Unreachable",
               {service(airplay_service_type, 7004, {"fe80::4%4"}),
                service(airplay_service_type, 7005, {})}),
    }));
    check(receivers.size() == 2, "RAOP-only and zero-port devices are left out");
    if (receivers.size() != 2) {
        return;
    }
    check(receivers[0].id == "AA:BB:CC:DD:EE:03" && receivers[0].address == "192.0.2.33" &&
              receivers[0].port == 7003,
          "first castable AirPlay service supplies address and port");
    check(receivers[1].id == "AA:BB:CC:DD:EE:04" && receivers[1].address.empty() &&
              receivers[1].port == 7004,
          "no castable service: first AirPlay service's port, empty address");
}

void metadata_tests() {
    auto airplay = service(airplay_service_type, airplay_port, {"192.0.2.5"});
    airplay.features = 0x1234'5678'9abc'def0ULL;
    airplay.password_required = true;
    const std::string name("Living\0Room", 11);
    const auto receivers = castable_receivers(
        snapshot({device("AA:BB:CC:DD:EE:05", name, {airplay}),
                  device("AA:BB:CC:DD:EE:06", "Plain",
                         {service(airplay_service_type, airplay_port, {"192.0.2.6"})})}));
    check(receivers.size() == 2, "metadata devices kept");
    if (receivers.size() != 2) {
        return;
    }
    check(receivers[0].name == name && receivers[0].model == "Model1,1",
          "name kept byte-exact, including an embedded NUL");
    check(receivers[0].features == 0x1234'5678'9abc'def0ULL && receivers[0].password_required,
          "features and password flag copied");
    check(!receivers[1].features && !receivers[1].password_required,
          "absent features and password stay absent");
}

/// Scan once through the C interface with the given script.
int32_t scan(ScanScript script, sap2_receiver_list** list, std::uint32_t duration_ms = 1000) {
    scan_script = script;
    return sap2_discover(duration_ms, list);
}

void c_argument_tests() {
    const int scans_before = scans;
    sap2_receiver_list* list = reinterpret_cast<sap2_receiver_list*>(&scans);
    check(sap2_discover(1000, nullptr) == SAP2_ERROR_INVALID_ARGUMENT, "null list pointer");
    check(sap2_discover(0, &list) == SAP2_ERROR_INVALID_ARGUMENT && !list,
          "zero duration refused and list cleared");
    list = reinterpret_cast<sap2_receiver_list*>(&scans);
    check(sap2_discover(SAP2_MAX_DISCOVERY_MS + 1, &list) == SAP2_ERROR_INVALID_ARGUMENT && !list,
          "duration above the maximum refused and list cleared");
    check(scans == scans_before, "refused arguments never start a scan");
    check(sap2_receiver_list_count(nullptr) == 0, "count of a null list");
    sap2_receiver_info info{};
    info.struct_size = sizeof(info);
    check(sap2_receiver_list_get(nullptr, 0, &info) == SAP2_ERROR_INVALID_ARGUMENT,
          "get from a null list");
    sap2_receiver_list_free(nullptr);
}

void c_failure_mapping_tests() {
    const std::pair<ScanScript, int32_t> cases[] = {
        {ScanScript::invalid_argument, SAP2_ERROR_INVALID_ARGUMENT},
        {ScanScript::runtime_error, SAP2_ERROR_CONNECTION},
        {ScanScript::out_of_memory, SAP2_ERROR_OUT_OF_MEMORY},
        {ScanScript::unknown, SAP2_ERROR_INTERNAL},
    };
    int index = 0;
    for (const auto& [script, expected] : cases) {
        sap2_receiver_list* list = reinterpret_cast<sap2_receiver_list*>(&scans);
        const auto result = scan(script, &list);
        check(result == expected && !list, "scan failure case " + std::to_string(index) +
                                               " maps to result " + std::to_string(expected) +
                                               " (got " + std::to_string(result) + ")");
        ++index;
    }
}

void c_list_access_tests() {
    auto airplay = service(airplay_service_type, 7010, {"192.0.2.10"});
    airplay.features = 0x5A39FFF7ULL;
    airplay.password_required = true;
    const std::string name("TV\0two", 6);
    scripted_result = snapshot(
        {device("AA:BB:CC:DD:EE:10", name, {airplay}),
         device("AA:BB:CC:DD:EE:11", "Speaker", {service(raop_service_type, 7000, {"192.0.2.11"})}),
         device("AA:BB:CC:DD:EE:12", "Link-local",
                {service(airplay_service_type, 7012, {"fe80::12%2"})})});
    sap2_receiver_list* list = nullptr;
    check(scan(ScanScript::result, &list, 2500) == SAP2_OK && list, "scripted scan succeeds");
    check(last_duration_ms == 2500, "duration passed through to discover()");
    scripted_result = {}; // The list must own copies, not refer to the snapshot.
    check(sap2_receiver_list_count(list) == 2, "speaker left out of the C list");

    sap2_receiver_info info{};
    info.struct_size = sizeof(info);
    check(sap2_receiver_list_get(list, 0, &info) == SAP2_OK, "get first receiver");
    check(std::strcmp(info.id, "AA:BB:CC:DD:EE:10") == 0 && info.name_length == name.size() &&
              std::memcmp(info.name, name.data(), name.size()) == 0 &&
              info.name[info.name_length] == '\0',
          "id and length-delimited name with its terminator");
    check(std::strcmp(info.model, "Model1,1") == 0 &&
              std::strcmp(info.address, "192.0.2.10") == 0 && info.port == 7010,
          "model, address and port");
    check(info.flags == (SAP2_RECEIVER_PASSWORD_REQUIRED | SAP2_RECEIVER_HAS_FEATURES) &&
              info.features == 0x5A39FFF7ULL,
          "flags and feature mask");

    check(sap2_receiver_list_get(list, 1, &info) == SAP2_OK && info.flags == 0 &&
              info.features == 0 && std::strcmp(info.address, "") == 0 && info.port == 7012,
          "second receiver: no flags, empty address");

    const sap2_receiver_info before = info;
    check(sap2_receiver_list_get(list, 2, &info) == SAP2_ERROR_INVALID_ARGUMENT,
          "index equal to count refused");
    sap2_receiver_info short_info{};
    short_info.struct_size = sizeof(short_info) - 1;
    check(sap2_receiver_list_get(list, 0, &short_info) == SAP2_ERROR_INVALID_ARGUMENT &&
              short_info.id == nullptr,
          "short struct refused and left unwritten");
    check(sap2_receiver_list_get(list, 0, nullptr) == SAP2_ERROR_INVALID_ARGUMENT, "null info");
    check(std::memcmp(&before, &info, sizeof(info)) == 0, "refused get leaves info unchanged");
    sap2_receiver_list_free(list);
}

void c_empty_scan_tests() {
    scripted_result = {};
    sap2_receiver_list* list = nullptr;
    check(scan(ScanScript::result, &list) == SAP2_OK && list, "empty scan still returns a list");
    check(sap2_receiver_list_count(list) == 0, "empty list count");
    sap2_receiver_info info{};
    info.struct_size = sizeof(info);
    check(sap2_receiver_list_get(list, 0, &info) == SAP2_ERROR_INVALID_ARGUMENT,
          "get from an empty list");
    sap2_receiver_list_free(list);
}
} // namespace

namespace send_airplay2 {
DiscoveryResult discover(const DiscoveryOptions& options) {
    ++scans;
    last_duration_ms = options.duration_ms;
    switch (scan_script) {
    case ScanScript::invalid_argument:
        throw std::invalid_argument("scripted");
    case ScanScript::runtime_error:
        throw std::runtime_error("scripted");
    case ScanScript::out_of_memory:
        throw std::bad_alloc();
    case ScanScript::unknown:
        throw 1;
    case ScanScript::result:
        break;
    }
    return scripted_result;
}
} // namespace send_airplay2

int main() {
    address_selection_tests();
    service_selection_tests();
    metadata_tests();
    c_argument_tests();
    c_failure_mapping_tests();
    c_list_access_tests();
    c_empty_scan_tests();
    if (failures) {
        std::cerr << failures << " receiver list test(s) failed\n";
        return 1;
    }
    std::cout << "receiver list tests passed\n";
    return 0;
}
