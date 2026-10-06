// SPDX-License-Identifier: Apache-2.0
#ifndef SAP2_DISCOVERY_CORE_H
#define SAP2_DISCOVERY_CORE_H
#include "send_airplay2/discovery.h"
#include <cstddef>

namespace send_airplay2::detail {
// Keep label boundaries: a literal dot in an instance is part of one label,
// not a separator. Bytes holds network-order wire data.
using Name = std::vector<std::string>;
using Bytes = std::vector<std::uint8_t>;

// DNS wire values and resource limits shared by parsing, scanning and transport.
namespace dns {
constexpr std::uint16_t a = 1;
constexpr std::uint16_t ptr = 12;
constexpr std::uint16_t txt = 16;
constexpr std::uint16_t aaaa = 28;
constexpr std::uint16_t srv = 33;
constexpr std::uint16_t internet_class = 1;
constexpr std::uint16_t response_flag = 0x8000;
constexpr std::uint16_t opcode_mask = 0x7800;
constexpr std::uint16_t truncated_flag = 0x0200;
constexpr std::uint16_t response_code_mask = 0x000f;
constexpr std::uint16_t cache_flush_flag = 0x8000;
constexpr std::uint16_t class_mask = 0x7fff;
constexpr std::size_t header_size = 12;
constexpr std::size_t max_packet_size = 9000;
constexpr std::size_t max_questions = 128;
constexpr std::size_t max_records_per_packet = 512;
constexpr std::size_t max_cached_records = 2048;
constexpr std::size_t max_query_size = 1400;
} // namespace dns

/// Validated IN-class record. TTL is in seconds; only supported RR types are stored.
struct Record {
    Name name;
    std::uint16_t type = 0;
    std::uint32_t ttl = 0;
    bool flush = false;
    Name target; // PTR or SRV
    std::uint16_t port = 0;
    Bytes data; // TXT, A, AAAA, or SRV priority/weight
};
struct CachedRecord {
    Record record;
    std::uint32_t interface_index = 0;
    std::uint64_t seen_ms = 0; // Monotonic time supplied by the scan owner.
    std::uint64_t expires_ms = 0;
};
struct Question {
    Name name;
    std::uint16_t type = 0;
};

/// Render a fully qualified name with literal dots, backslashes and controls escaped.
std::string name_text(const Name& name);

/// Decode a complete response atomically. On failure, out is empty.
/// Unknown RR types/classes are skipped; no output points into the input buffer.
bool parse_response(const std::uint8_t* data, std::size_t size, std::vector<Record>& out);

/// Encode bounded QM questions and eligible known answers for one interface.
/// Callers pass validated names in batches that fit dns::max_query_size.
Bytes query(const std::vector<Question>& questions, const std::vector<CachedRecord>& known,
            std::uint32_t interface_index, std::uint64_t now_ms);

/// Parse one hex word or low/high 32-bit hex words; nullopt means malformed/overflow.
std::optional<std::uint64_t> parse_features(const std::string& text);

/// Interface-scoped record cache. The scan owns synchronization and monotonic time.
/// Call expire before requesting snapshots; getters do not advance time themselves.
class Cache {
public:
    // Caller supplies monotonic time. Malformed packets have no cache effects.
    bool ingest(const std::uint8_t* data, std::size_t size, std::uint32_t interface_index,
                std::uint64_t now_ms);
    /// Remove records whose TTL or goodbye/cache-flush grace has elapsed.
    void expire(std::uint64_t now_ms);
    /// Drop observations from removed or changed interfaces.
    void retain_interfaces(const std::vector<std::uint32_t>& active);
    /// Return resolved services grouped by advertised identity, in deterministic order.
    std::vector<Device> devices() const;
    /// Browse both service types and resolve learned instances/hostnames.
    std::vector<Question> questions() const;
    const std::vector<CachedRecord>& records() const {
        return records_;
    }
    bool overflowed() const {
        return overflowed_;
    }

private:
    std::vector<CachedRecord> records_;
    bool overflowed_ = false;
};
} // namespace send_airplay2::detail
#endif
