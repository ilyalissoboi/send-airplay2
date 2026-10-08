// SPDX-License-Identifier: Apache-2.0
#include "receiver_list.h"

#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

namespace send_airplay2::detail {
namespace {
/// Discovery formats IPv4 as dotted quads and IPv6 with colons.
bool is_ipv4(std::string_view address) {
    return !address.empty() && address.find(':') == std::string_view::npos;
}

/// fe80::/10 covers first hextets fe80 through febf: four hex digits whose third
/// is 8, 9, a or b (a shorter hextet such as fe8 is 0fe8, outside the range).
/// Discovery appends %scope to link-local addresses; any scoped address is
/// refused as well, since a receiver cannot use our scope id.
bool is_link_local_ipv6(std::string_view address) {
    if (address.find('%') != std::string_view::npos) {
        return true;
    }
    constexpr std::size_t hextet_digits = 4;
    if (address.find(':') != hextet_digits) {
        return false;
    }
    const auto lower = [](char digit) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(digit)));
    };
    const char third = lower(address[2]);
    return lower(address[0]) == 'f' && lower(address[1]) == 'e' &&
           (third == '8' || third == '9' || third == 'a' || third == 'b');
}

bool is_castable_ipv6(std::string_view address) {
    return address.find(':') != std::string_view::npos && !is_link_local_ipv6(address);
}

/// The address a cast to this service should use, or empty when none is castable.
std::string castable_address(const Service& service) {
    for (const auto& address : service.addresses) {
        if (is_ipv4(address)) {
            return address;
        }
    }
    for (const auto& address : service.addresses) {
        if (is_castable_ipv6(address)) {
            return address;
        }
    }
    return {};
}
} // namespace

std::vector<ReceiverEntry> castable_receivers(const DiscoveryResult& result) {
    std::vector<ReceiverEntry> receivers;
    for (const auto& device : result.devices) {
        const Service* chosen = nullptr;
        std::string address;
        for (const auto& service : device.services) {
            if (service.type != airplay_service_type || service.port == 0) {
                continue;
            }
            auto candidate = castable_address(service);
            if (!candidate.empty()) {
                chosen = &service;
                address = std::move(candidate);
                break;
            }
            if (!chosen) {
                chosen = &service; // Fallback when no AirPlay service is castable.
            }
        }
        if (!chosen) {
            continue; // No AirPlay video service: RAOP-only speakers are not castable.
        }
        ReceiverEntry entry;
        entry.id = device.id;
        entry.name = device.name;
        entry.model = device.model;
        entry.address = std::move(address);
        entry.port = chosen->port;
        entry.password_required = chosen->password_required.value_or(false);
        entry.features = chosen->features;
        receivers.push_back(std::move(entry));
    }
    return receivers;
}
} // namespace send_airplay2::detail
