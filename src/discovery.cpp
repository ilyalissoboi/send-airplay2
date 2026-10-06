// SPDX-License-Identifier: Apache-2.0
#include "discovery_transport.h"

#include <algorithm>
#include <chrono>
#include <map>
#include <stdexcept>
#include <utility>

namespace send_airplay2 {
namespace {

constexpr std::uint32_t max_scan_duration_ms = 60000;
constexpr std::uint64_t interface_refresh_ms = 1000;
constexpr std::uint64_t initial_retry_ms = 1000;
constexpr std::uint64_t max_retry_ms = 16000;
constexpr std::uint32_t receive_poll_ms = 100;
constexpr std::size_t questions_per_packet = 4;
constexpr std::size_t max_scheduled_questions = 8192;

struct QuerySchedule {
    std::uint64_t next_due_ms = 0;
    std::uint64_t retry_interval_ms = initial_retry_ms;
};

// Include the interface, escaped DNS name and RR type in the scheduling key:
// a response on one link must not postpone resolution on a different link.
using QuestionSchedules = std::map<std::string, QuerySchedule>;

bool same_network(const detail::Interface& left, const detail::Interface& right) {
    return left.index == right.index && left.ipv4 == right.ipv4 && left.netmask == right.netmask;
}

void reset_interface_queries(QuestionSchedules& schedules, std::uint32_t interface_index) {
    const auto prefix = std::to_string(interface_index) + ':';
    for (auto entry = schedules.begin(); entry != schedules.end();) {
        if (entry->first.compare(0, prefix.size(), prefix) == 0) {
            entry = schedules.erase(entry);
        } else {
            ++entry;
        }
    }
}

// Newly learned instances/hosts get immediate questions. Repeated questions use
// exponential backoff independently, so new resolution work does not accelerate
// existing browse traffic or wait for an unrelated question's retry interval.
std::vector<detail::Question> take_due_questions(const std::vector<detail::Question>& questions,
                                                 std::uint32_t interface_index,
                                                 std::uint64_t now_ms, QuestionSchedules& schedules,
                                                 bool& limit_reached) {
    std::vector<detail::Question> due;
    for (const auto& question : questions) {
        const auto key = std::to_string(interface_index) + ':' + detail::name_text(question.name) +
                         ':' + std::to_string(question.type);
        auto entry = schedules.find(key);
        if (entry == schedules.end()) {
            if (schedules.size() >= max_scheduled_questions) {
                limit_reached = true;
                continue;
            }
            entry = schedules.emplace(key, QuerySchedule{}).first;
        }
        auto& state = entry->second;
        if (now_ms >= state.next_due_ms) {
            due.push_back(question);
            state.next_due_ms = now_ms + state.retry_interval_ms;
            state.retry_interval_ms = std::min(state.retry_interval_ms * 2, max_retry_ms);
        }
    }
    return due;
}

} // namespace

DiscoveryResult discover(const DiscoveryOptions& options) {
    if (options.duration_ms == 0 || options.duration_ms > max_scan_duration_ms) {
        throw std::invalid_argument("duration_ms must be 1..60000");
    }

    // All resources and observations belong to this synchronous scan. A steady
    // clock prevents wall-clock corrections from extending or expiring TTLs.
    auto transport = detail::native_transport();
    detail::Cache cache;
    DiscoveryResult result;
    const auto start = std::chrono::steady_clock::now();
    const auto elapsed_ms = [&] {
        const auto duration = std::chrono::steady_clock::now() - start;
        return static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(duration).count());
    };

    QuestionSchedules schedules;
    bool question_limit_reached = false;
    bool schedule_limit_reached = false;
    std::uint64_t next_refresh_ms = 0;
    std::vector<detail::Interface> interfaces;

    while (elapsed_ms() < options.duration_ms) {
        const auto now_ms = elapsed_ms();
        cache.expire(now_ms);

        // Preserve observations only for interfaces whose address/subnet stayed
        // unchanged. Reset retry schedules on new or changed links.
        if (now_ms >= next_refresh_ms) {
            const auto previous_interfaces = interfaces;
            interfaces = transport->refresh(result.warnings);
            if (interfaces.empty()) {
                throw std::runtime_error(
                    "No usable IPv4 multicast interface (check network/firewall)");
            }

            std::vector<std::uint32_t> unchanged_interfaces;
            for (const auto& nic : interfaces) {
                const bool unchanged = std::any_of(
                    previous_interfaces.begin(), previous_interfaces.end(),
                    [&](const detail::Interface& previous) { return same_network(nic, previous); });
                if (unchanged) {
                    unchanged_interfaces.push_back(nic.index);
                } else {
                    reset_interface_queries(schedules, nic.index);
                }
            }
            cache.retain_interfaces(unchanged_interfaces);
            next_refresh_ms = now_ms + interface_refresh_ms;
        }

        const auto questions = cache.questions();
        if (questions.size() == detail::dns::max_questions) {
            question_limit_reached = true;
        }
        unsigned send_attempts = 0;
        unsigned successful_sends = 0;
        for (const auto& nic : interfaces) {
            const auto due =
                take_due_questions(questions, nic.index, now_ms, schedules, schedule_limit_reached);

            // Four maximal DNS names fit the packet budget. Splitting batches
            // ensures long question lists do not starve their final entries.
            for (std::size_t offset = 0; offset < due.size(); offset += questions_per_packet) {
                const auto batch_end = std::min(offset + questions_per_packet, due.size());
                const std::vector<detail::Question> batch(due.begin() + offset,
                                                          due.begin() + batch_end);
                ++send_attempts;
                try {
                    transport->send(detail::query(batch, cache.records(), nic.index, now_ms), nic);
                    ++successful_sends;
                } catch (const std::runtime_error& error) {
                    result.warnings.push_back("Interface " + std::to_string(nic.index) + ": " +
                                              error.what());
                }
            }
        }
        if (send_attempts != 0 && successful_sends == 0) {
            throw std::runtime_error("Unable to send discovery queries on any interface");
        }

        // Short receive waits let expiration, new resolution questions and
        // interface refresh proceed even when no packets arrive.
        detail::Datagram packet;
        const auto remaining_ms =
            options.duration_ms - std::min<std::uint64_t>(elapsed_ms(), options.duration_ms);
        const auto wait_ms =
            static_cast<std::uint32_t>(std::min<std::uint64_t>(remaining_ms, receive_poll_ms));
        if (transport->receive(packet, wait_ms)) {
            ++result.packets_received;
            if (!cache.ingest(packet.bytes.data(), packet.bytes.size(), packet.interface_index,
                              elapsed_ms())) {
                ++result.packets_rejected;
            }
        }
    }

    // A packet received near the deadline can have already expired by the final
    // snapshot. Limits are surfaced as warnings rather than complete-network claims.
    cache.expire(elapsed_ms());
    result.devices = cache.devices();
    if (cache.overflowed()) {
        result.warnings.push_back(
            "DNS record cache limit reached (2048); results may be incomplete");
    }
    if (question_limit_reached) {
        result.warnings.push_back("DNS question limit reached (128); results may be incomplete");
    }
    if (schedule_limit_reached) {
        result.warnings.push_back("Query schedule limit reached (8192); results may be incomplete");
    }
    std::sort(result.warnings.begin(), result.warnings.end());
    result.warnings.erase(std::unique(result.warnings.begin(), result.warnings.end()),
                          result.warnings.end());
    return result;
}
} // namespace send_airplay2
