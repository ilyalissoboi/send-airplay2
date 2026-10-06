// SPDX-License-Identifier: Apache-2.0
#ifndef SAP2_DISCOVERY_CORE_H
#define SAP2_DISCOVERY_CORE_H
#include "send_airplay2/discovery.h"
#include <cstddef>

namespace send_airplay2::detail {
using Name = std::vector<std::string>;
using Bytes = std::vector<std::uint8_t>;
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
    std::uint32_t interface_index;
    std::uint64_t seen_ms;
    std::uint64_t expires_ms;
};
struct Question { Name name; std::uint16_t type; };
std::string name_text(const Name& name);
bool parse_response(const std::uint8_t* data, std::size_t size, std::vector<Record>& out);
Bytes query(const std::vector<Question>& questions, const std::vector<CachedRecord>& known,
            std::uint32_t interface_index, std::uint64_t now_ms);
std::optional<std::uint64_t> parse_features(const std::string& text);
class Cache {
public:
    // Caller supplies monotonic time. Malformed packets have no cache effects.
    bool ingest(const std::uint8_t* data, std::size_t size, std::uint32_t interface_index,
                std::uint64_t now_ms);
    void expire(std::uint64_t now_ms);
    void retain_interfaces(const std::vector<std::uint32_t>& active);
    std::vector<Device> devices() const;
    std::vector<Question> questions() const;
    const std::vector<CachedRecord>& records() const { return records_; }
    bool overflowed() const { return overflowed_; }
private:
    std::vector<CachedRecord> records_;
    bool overflowed_ = false;
};
}
#endif
