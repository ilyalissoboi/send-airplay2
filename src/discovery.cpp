// SPDX-License-Identifier: Apache-2.0
#include "discovery_transport.h"
#include <algorithm>
#include <chrono>
#include <map>
#include <stdexcept>

namespace send_airplay2 {
DiscoveryResult discover(const DiscoveryOptions& options) {
    if (!options.duration_ms || options.duration_ms > 60000) throw std::invalid_argument("duration_ms must be 1..60000");
    auto transport = detail::native_transport();
    detail::Cache cache;
    DiscoveryResult result;
    const auto start = std::chrono::steady_clock::now();
    auto elapsed = [&] { return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count()); };
    struct Schedule { std::uint64_t next = 0, interval = 1000; };
    std::map<std::string, Schedule> schedule;
    bool question_limit = false, schedule_limit = false;
    std::uint64_t next_refresh = 0;
    std::vector<detail::Interface> interfaces;
    while (elapsed() < options.duration_ms) {
        const auto now = elapsed();
        cache.expire(now);
        if (now >= next_refresh) {
            const auto previous = interfaces;
            interfaces = transport->refresh(result.warnings);
            if (interfaces.empty()) throw std::runtime_error("No usable IPv4 multicast interface (check network/firewall)");
            std::vector<std::uint32_t> active;
            for (const auto& i : interfaces) {
                // A changed address/network invalidates old interface-scoped observations.
                if (std::none_of(previous.begin(), previous.end(), [&](const detail::Interface& p) {
                    return i.index == p.index && i.ipv4 == p.ipv4 && i.netmask == p.netmask;
                })) {
                    const auto prefix = std::to_string(i.index) + ':';
                    for (auto it = schedule.begin(); it != schedule.end();) {
                        if (it->first.compare(0, prefix.size(), prefix) == 0) it = schedule.erase(it);
                        else ++it;
                    }
                }
                else active.push_back(i.index);
            }
            cache.retain_interfaces(active);
            next_refresh = now + 1000;
        }
        const auto questions = cache.questions();
        if (questions.size() == 128) question_limit = true;
        unsigned attempts = 0, sent = 0;
        for (const auto& i : interfaces) {
            std::vector<detail::Question> due;
            for (const auto& q : questions) {
                const auto key = std::to_string(i.index) + ':' + detail::name_text(q.name) + ':' + std::to_string(q.type);
                auto it = schedule.find(key);
                if (it == schedule.end()) {
                    if (schedule.size() >= 8192) { schedule_limit = true; continue; }
                    it = schedule.emplace(key, Schedule{}).first;
                }
                auto& state = it->second;
                if (now >= state.next) {
                    due.push_back(q); state.next = now + state.interval;
                    state.interval = std::min<std::uint64_t>(state.interval * 2, 16000);
                }
            }
            // Four maximal DNS names fit a bounded query; no tail starvation.
            for (std::size_t pos = 0; pos < due.size(); pos += 4) {
                const std::vector<detail::Question> batch(due.begin() + pos, due.begin() + std::min(pos + 4, due.size()));
                ++attempts;
                try { transport->send(detail::query(batch, cache.records(), i.index, now), i); ++sent; }
                catch (const std::runtime_error& e) { result.warnings.push_back("Interface " + std::to_string(i.index) + ": " + e.what()); }
            }
        }
        if (attempts && !sent) throw std::runtime_error("Unable to send discovery queries on any interface");
        detail::Datagram packet;
        const auto remaining = options.duration_ms - std::min<std::uint64_t>(elapsed(), options.duration_ms);
        if (transport->receive(packet, static_cast<std::uint32_t>(std::min<std::uint64_t>(remaining, 100)))) {
            ++result.packets_received;
            if (!cache.ingest(packet.bytes.data(), packet.bytes.size(), packet.interface_index, elapsed())) ++result.packets_rejected;
        }
    }
    cache.expire(elapsed());
    result.devices = cache.devices();
    if (cache.overflowed()) result.warnings.push_back("DNS record cache limit reached (2048); results may be incomplete");
    if (question_limit) result.warnings.push_back("DNS question limit reached (128); results may be incomplete");
    if (schedule_limit) result.warnings.push_back("Query schedule limit reached (8192); results may be incomplete");
    std::sort(result.warnings.begin(), result.warnings.end());
    result.warnings.erase(std::unique(result.warnings.begin(), result.warnings.end()), result.warnings.end());
    return result;
}
}
