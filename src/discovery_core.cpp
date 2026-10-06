// SPDX-License-Identifier: Apache-2.0
#include "discovery_core.h"
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <utility>

namespace send_airplay2::detail {
namespace {
const Name airplay{"_airplay", "_tcp", "local"};
const Name raop{"_raop", "_tcp", "local"};
constexpr std::uint64_t milliseconds_per_second = 1000;
constexpr std::uint64_t cache_grace_ms = 1000;

// DNS case folding is ASCII-only; locale-sensitive tolower would alter byte labels.
std::string ascii_lower(std::string text) {
    for (auto& character : text) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character + ('a' - 'A'));
        }
    }
    return text;
}

bool names_equal(const Name& a, const Name& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (ascii_lower(a[i]) != ascii_lower(b[i])) {
            return false;
        }
    }
    return true;
}

bool is_service_instance(const Name& name, const Name& type) {
    return name.size() == type.size() + 1 && names_equal(Name(name.begin() + 1, name.end()), type);
}

std::uint16_t read_u16_be(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<unsigned>(p[0]) << 8) | p[1]);
}

std::uint32_t read_u32_be(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(read_u16_be(p)) << 16) | read_u16_be(p + 2);
}

// cursor advances over the encoded name, while position may follow compression
// pointers elsewhere in the packet. After the first pointer, cursor must stay at
// its end so the caller can continue reading the containing resource record.
bool decode_name(const std::uint8_t* packet, std::size_t packet_size, std::size_t& cursor,
                 Name& labels) {
    constexpr std::size_t max_name_traversal_steps = 128;
    constexpr std::size_t max_expanded_name_size = 255;
    constexpr std::uint8_t pointer_tag = 0xc0;
    std::size_t position = cursor;
    std::size_t expanded_size = 1; // Includes the terminating root label.
    std::size_t traversal_steps = 0;
    bool followed_pointer = false;
    labels.clear();
    while (position < packet_size && ++traversal_steps <= max_name_traversal_steps) {
        const auto length = packet[position++];
        if ((length & pointer_tag) == pointer_tag) {
            if (position >= packet_size) {
                return false;
            }
            const auto target =
                static_cast<std::size_t>(((length & 0x3f) << 8) | packet[position++]);
            if (target >= position - 2) {
                return false; // DNS compression references must point backward.
            }
            if (!followed_pointer) {
                cursor = position;
            }
            followed_pointer = true;
            position = target;
        } else {
            if (length & pointer_tag) {
                return false;
            }
            if (!length) {
                if (!followed_pointer) {
                    cursor = position;
                }
                return true;
            }
            if (length > packet_size - position) {
                return false;
            }
            expanded_size += length + 1;
            if (expanded_size > max_expanded_name_size) {
                return false;
            }
            labels.emplace_back(reinterpret_cast<const char*>(packet + position), length);
            position += length;
        }
    }
    return false;
}

bool has_valid_txt_lengths(const Bytes& bytes) {
    std::size_t pos = 0;
    while (pos < bytes.size()) {
        const auto length = bytes[pos++];
        if (length > bytes.size() - pos) {
            return false;
        }
        pos += length;
    }
    return true;
}

std::map<std::string, std::optional<std::string>> parse_txt_fields(const Bytes& bytes) {
    // The packet parser has already validated every length-prefixed entry.
    std::map<std::string, std::optional<std::string>> fields;
    for (std::size_t pos = 0; pos < bytes.size();) {
        const auto length = bytes[pos++];
        const std::string entry(reinterpret_cast<const char*>(bytes.data() + pos), length);
        pos += length;
        const auto eq = entry.find('=');
        const auto key = entry.substr(0, eq);
        if (key.empty() || std::any_of(key.begin(), key.end(),
                                       [](unsigned char c) { return c < 0x20 || c > 0x7e; })) {
            continue;
        }
        // RFC 6763: first duplicate wins; flag differs from empty value.
        fields.emplace(ascii_lower(key), eq == std::string::npos ? std::optional<std::string>{}
                                                                 : entry.substr(eq + 1));
    }
    return fields;
}

std::string txt_value(const Service& service, const char* key) {
    const auto entry = service.txt.find(key);
    return entry != service.txt.end() && entry->second ? *entry->second : std::string{};
}

int hex_digit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

std::string normalize_mac(const std::string& value) {
    std::string digits;
    if (value.size() == 12) {
        digits = value;
    } else if (value.size() == 17) {
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (i % 3 == 2) {
                if (value[i] != ':') {
                    return {};
                }
            } else {
                digits += value[i];
            }
        }
    } else {
        return {};
    }
    if (std::any_of(digits.begin(), digits.end(), [](char c) { return hex_digit(c) < 0; })) {
        return {};
    }
    std::string result;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i && i % 2 == 0) {
            result += ':';
        }
        result += ascii_lower(digits.substr(i, 1));
    }
    return result;
}

// TTL/cache-flush are freshness metadata, not part of resource-record identity.
bool same_record(const Record& a, const Record& b) {
    return a.type == b.type && names_equal(a.name, b.name) && names_equal(a.target, b.target) &&
           a.port == b.port && a.data == b.data;
}

void append_u16_be(Bytes& b, std::uint16_t n) {
    b.push_back(static_cast<std::uint8_t>(n >> 8));
    b.push_back(static_cast<std::uint8_t>(n));
}

void append_u32_be(Bytes& b, std::uint32_t n) {
    append_u16_be(b, static_cast<std::uint16_t>(n >> 16));
    append_u16_be(b, static_cast<std::uint16_t>(n));
}

void append_name(Bytes& b, const Name& name) {
    for (const auto& label : name) {
        b.push_back(static_cast<std::uint8_t>(label.size()));
        b.insert(b.end(), label.begin(), label.end());
    }
    b.push_back(0);
}

std::string address_text(const Bytes& data, std::uint32_t interface_index) {
    std::ostringstream stream;
    if (data.size() == 4) {
        stream << unsigned(data[0]) << '.' << unsigned(data[1]) << '.' << unsigned(data[2]) << '.'
               << unsigned(data[3]);
    } else {
        for (std::size_t i = 0; i < 16; i += 2) {
            if (i) {
                stream << ':';
            }
            stream << std::hex << read_u16_be(data.data() + i);
        }
        if (data[0] == 0xfe && (data[1] & 0xc0) == 0x80) {
            stream << '%' << std::dec << interface_index;
        }
    }
    return stream.str();
}

// Resolve one PTR instance only against records on its receiving interface.
// TXT is optional, but a usable SRV endpoint and at least one address are required.
std::optional<Service> resolve_service(const CachedRecord& pointer,
                                       const std::vector<CachedRecord>& records) {
    const auto& record = pointer.record;
    const CachedRecord* latest_srv = nullptr;
    const CachedRecord* latest_txt = nullptr;
    // Old unique records remain during flush grace, but metadata uses the newest
    // assertion. On equal timestamps, retain the existing last-record tie break.
    for (const auto& cached : records) {
        if (cached.interface_index == pointer.interface_index &&
            names_equal(cached.record.name, record.target)) {
            if (cached.record.type == dns::srv &&
                (!latest_srv || cached.seen_ms >= latest_srv->seen_ms)) {
                latest_srv = &cached;
            }
            if (cached.record.type == dns::txt &&
                (!latest_txt || cached.seen_ms >= latest_txt->seen_ms)) {
                latest_txt = &cached;
            }
        }
    }
    if (!latest_srv || !latest_srv->record.port || latest_srv->record.target.empty()) {
        return std::nullopt;
    }
    Service service;
    service.type = ascii_lower(name_text(record.name));
    service.instance = name_text(record.target);
    service.interface_index = pointer.interface_index;
    service.hostname = name_text(latest_srv->record.target);
    service.port = latest_srv->record.port;
    if (latest_txt) {
        service.txt = parse_txt_fields(latest_txt->record.data);
    }
    for (const auto& cached : records) {
        if (cached.interface_index == pointer.interface_index &&
            (cached.record.type == dns::a || cached.record.type == dns::aaaa) &&
            names_equal(cached.record.name, latest_srv->record.target)) {
            service.addresses.push_back(address_text(cached.record.data, pointer.interface_index));
        }
    }
    std::sort(service.addresses.begin(), service.addresses.end());
    service.addresses.erase(std::unique(service.addresses.begin(), service.addresses.end()),
                            service.addresses.end());
    if (service.addresses.empty()) {
        return std::nullopt; // Only expose resolved endpoints.
    }
    auto features_text = txt_value(service, "features");
    if (features_text.empty()) {
        features_text = txt_value(service, "ft"); // RAOP's advertised feature alias.
    }
    service.features = parse_features(features_text);
    const auto password_flag = ascii_lower(txt_value(service, "pw"));
    if (password_flag == "true" || password_flag == "1") {
        service.password_required = true;
    } else if (password_flag == "false" || password_flag == "0") {
        service.password_required = false;
    }
    return service;
}
} // namespace

std::string name_text(const Name& name) {
    std::string text;
    for (const auto& label : name) {
        for (unsigned char c : label) {
            if (c == '.' || c == '\\' || c < 0x20 || c == 0x7f) {
                text += '\\';
                text += static_cast<char>('0' + c / 100);
                text += static_cast<char>('0' + (c / 10) % 10);
                text += static_cast<char>('0' + c % 10);
            } else {
                text += static_cast<char>(c);
            }
        }
        text += '.';
    }
    return text;
}

bool parse_response(const std::uint8_t* packet, std::size_t packet_size,
                    std::vector<Record>& output) {
    output.clear();
    if (!packet || packet_size < dns::header_size || packet_size > dns::max_packet_size) {
        return false;
    }
    const auto header_flags = read_u16_be(packet + 2);
    // Only complete, standard, successful responses; ignore query/probe data.
    const auto invalid_flags = dns::opcode_mask | dns::truncated_flag | dns::response_code_mask;
    if (!(header_flags & dns::response_flag) || (header_flags & invalid_flags)) {
        return false;
    }
    const auto question_count = read_u16_be(packet + 4);
    const auto record_count = static_cast<unsigned>(read_u16_be(packet + 6)) +
                              read_u16_be(packet + 8) + read_u16_be(packet + 10);
    if (question_count > dns::max_questions || record_count > dns::max_records_per_packet) {
        return false;
    }
    std::size_t position = dns::header_size;
    Name ignored_name;
    for (unsigned i = 0; i < question_count; ++i) {
        if (!decode_name(packet, packet_size, position, ignored_name) ||
            packet_size - position < 4) {
            return false;
        }
        position += 4;
    }
    std::vector<Record> parsed_records;
    for (unsigned i = 0; i < record_count; ++i) {
        Record record;
        if (!decode_name(packet, packet_size, position, record.name) ||
            packet_size - position < 10) {
            return false;
        }
        record.type = read_u16_be(packet + position);
        const auto record_class = read_u16_be(packet + position + 2);
        record.flush = (record_class & dns::cache_flush_flag) != 0;
        record.ttl = read_u32_be(packet + position + 4);
        const auto data_length = read_u16_be(packet + position + 8);
        position += 10;
        if (data_length > packet_size - position) {
            return false;
        }
        const auto data_end = position + data_length;
        if ((record_class & dns::class_mask) == dns::internet_class) {
            if (record.type == dns::ptr || record.type == dns::srv) {
                if (record.type == dns::srv) {
                    if (data_length < 7) {
                        return false;
                    }
                    // Preserve the SRV priority/weight bytes for record equality.
                    record.data.assign(packet + position, packet + position + 4);
                    record.port = read_u16_be(packet + position + 4);
                    position += 6;
                }
                if (!decode_name(packet, packet_size, position, record.target) ||
                    position != data_end) {
                    return false;
                }
            } else if (record.type == dns::txt || record.type == dns::a ||
                       record.type == dns::aaaa) {
                record.data.assign(packet + position, packet + data_end);
                if ((record.type == dns::a && data_length != 4) ||
                    (record.type == dns::aaaa && data_length != 16) ||
                    (record.type == dns::txt && !has_valid_txt_lengths(record.data))) {
                    return false;
                }
            } else {
                position = data_end;
                continue;
            }
            parsed_records.push_back(std::move(record));
        }
        position = data_end;
    }
    if (position != packet_size) {
        return false;
    }
    // Publish only after all sections and the exact datagram length validate.
    output = std::move(parsed_records);
    return true;
}

Bytes query(const std::vector<Question>& questions, const std::vector<CachedRecord>& known,
            std::uint32_t interface_index, std::uint64_t now_ms) {
    Bytes packet(dns::header_size, 0);
    std::uint16_t question_count = 0;
    std::uint16_t answer_count = 0;
    for (const auto& question : questions) {
        Bytes entry;
        append_name(entry, question.name);
        append_u16_be(entry, question.type);
        append_u16_be(entry, dns::internet_class); // QM, not QU.
        if (packet.size() + entry.size() > dns::max_query_size) {
            break;
        }
        packet.insert(packet.end(), entry.begin(), entry.end());
        ++question_count;
    }
    for (const auto& cached : known) {
        const auto& record = cached.record;
        if (cached.interface_index != interface_index || cached.expires_ms <= now_ms) {
            continue;
        }
        const auto remaining_ttl = (cached.expires_ms - now_ms) / milliseconds_per_second;
        // RFC 6762 known-answer suppression requires at least half the original TTL.
        if (remaining_ttl < (static_cast<std::uint64_t>(record.ttl) + 1) / 2) {
            continue;
        }
        if (!std::any_of(questions.begin(), questions.begin() + question_count,
                         [&](const Question& question) {
                             return question.type == record.type &&
                                    names_equal(question.name, record.name);
                         })) {
            continue;
        }
        Bytes entry, data;
        append_name(entry, record.name);
        append_u16_be(entry, record.type);
        append_u16_be(entry, dns::internet_class);
        append_u32_be(entry, static_cast<std::uint32_t>(remaining_ttl));
        if (record.type == dns::ptr) {
            append_name(data, record.target);
        } else if (record.type == dns::srv) {
            data = record.data;
            append_u16_be(data, record.port);
            append_name(data, record.target);
        } else {
            data = record.data;
        }
        append_u16_be(entry, static_cast<std::uint16_t>(data.size()));
        entry.insert(entry.end(), data.begin(), data.end());
        if (packet.size() + entry.size() > dns::max_query_size) {
            break;
        }
        packet.insert(packet.end(), entry.begin(), entry.end());
        ++answer_count;
    }
    packet[4] = static_cast<std::uint8_t>(question_count >> 8);
    packet[5] = static_cast<std::uint8_t>(question_count);
    packet[6] = static_cast<std::uint8_t>(answer_count >> 8);
    packet[7] = static_cast<std::uint8_t>(answer_count);
    return packet;
}

std::optional<std::uint64_t> parse_features(const std::string& text) {
    const auto comma = text.find(',');
    const auto parse_hex_word = [](std::string digits,
                                   std::size_t max_digits) -> std::optional<std::uint64_t> {
        if (digits.size() >= 2 && digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X')) {
            digits.erase(0, 2);
        }
        if (digits.empty() || digits.size() > max_digits) {
            return {};
        }
        std::uint64_t value = 0;
        for (char character : digits) {
            const auto digit = hex_digit(character);
            if (digit < 0) {
                return {};
            }
            value = (value << 4) | static_cast<unsigned>(digit);
        }
        return value;
    };
    if (comma == std::string::npos) {
        return parse_hex_word(text, 16);
    }
    const auto low = parse_hex_word(text.substr(0, comma), 8);
    const auto high = parse_hex_word(text.substr(comma + 1), 8);
    if (!low || !high) {
        return {};
    }
    return *low | (*high << 32);
}

bool Cache::ingest(const std::uint8_t* data, std::size_t size, std::uint32_t interface_index,
                   std::uint64_t now_ms) {
    std::vector<Record> parsed_records;
    if (!interface_index || !parse_response(data, size, parsed_records)) {
        return false;
    }
    expire(now_ms);
    // Mark the whole old RRSet first, then refresh/insert every record in this
    // packet. This prevents one member of a new multi-address RRSet from flushing
    // another. Protect records seen within the preceding one-second burst.
    for (const auto& record : parsed_records) {
        if (record.flush && record.ttl) {
            for (auto& cached : records_) {
                if (cached.interface_index == interface_index &&
                    cached.record.type == record.type &&
                    names_equal(cached.record.name, record.name) &&
                    now_ms - cached.seen_ms >= cache_grace_ms) {
                    cached.expires_ms = std::min(cached.expires_ms, now_ms + cache_grace_ms);
                }
            }
        }
    }
    for (auto& record : parsed_records) {
        auto existing =
            std::find_if(records_.begin(), records_.end(), [&](const CachedRecord& cached) {
                return cached.interface_index == interface_index &&
                       same_record(cached.record, record);
            });
        if (!record.ttl) {
            // A goodbye shortens existing lifetime but never introduces a record.
            // Another responder can rescue it with a refresh during the grace.
            if (existing != records_.end()) {
                existing->expires_ms = std::min(existing->expires_ms, now_ms + cache_grace_ms);
            }
        } else {
            const auto expires_ms =
                now_ms + static_cast<std::uint64_t>(record.ttl) * milliseconds_per_second;
            if (existing != records_.end()) {
                *existing = {std::move(record), interface_index, now_ms, expires_ms};
            } else if (records_.size() < dns::max_cached_records) {
                records_.push_back({std::move(record), interface_index, now_ms, expires_ms});
            } else {
                overflowed_ = true;
            }
        }
    }
    return true;
}

void Cache::expire(std::uint64_t now_ms) {
    records_.erase(std::remove_if(records_.begin(), records_.end(),
                                  [now_ms](const CachedRecord& cached) {
                                      return cached.expires_ms <= now_ms;
                                  }),
                   records_.end());
}

void Cache::retain_interfaces(const std::vector<std::uint32_t>& active) {
    records_.erase(std::remove_if(records_.begin(), records_.end(),
                                  [&](const CachedRecord& cached) {
                                      return std::find(active.begin(), active.end(),
                                                       cached.interface_index) == active.end();
                                  }),
                   records_.end());
}

std::vector<Question> Cache::questions() const {
    std::vector<Question> result{{airplay, dns::ptr}, {raop, dns::ptr}};
    auto add = [&](const Name& name, std::uint16_t type) {
        if (result.size() < dns::max_questions &&
            std::none_of(result.begin(), result.end(), [&](const Question& question) {
                return question.type == type && names_equal(question.name, name);
            })) {
            result.push_back({name, type});
        }
    };
    for (const auto& cached : records_) {
        const auto& record = cached.record;
        if (record.type != dns::ptr ||
            (!names_equal(record.name, airplay) && !names_equal(record.name, raop)) ||
            !is_service_instance(record.target, record.name)) {
            continue;
        }
        add(record.target, dns::srv);
        add(record.target, dns::txt);
        for (const auto& service_record : records_) {
            if (service_record.interface_index == cached.interface_index &&
                service_record.record.type == dns::srv &&
                names_equal(service_record.record.name, record.target) &&
                !service_record.record.target.empty()) {
                add(service_record.record.target, dns::a);
                add(service_record.record.target, dns::aaaa);
            }
        }
    }
    return result;
}

std::vector<Device> Cache::devices() const {
    std::map<std::string, Device> grouped;
    std::vector<std::string> seen;
    for (const auto& pointer : records_) {
        const auto& record = pointer.record;
        if (record.type != dns::ptr ||
            (!names_equal(record.name, airplay) && !names_equal(record.name, raop)) ||
            !is_service_instance(record.target, record.name)) {
            continue;
        }
        const auto service_key =
            std::to_string(pointer.interface_index) + ':' + ascii_lower(name_text(record.target));
        if (std::find(seen.begin(), seen.end(), service_key) != seen.end()) {
            continue;
        }
        seen.push_back(service_key);
        auto resolved = resolve_service(pointer, records_);
        if (!resolved) {
            continue;
        }
        Service service = std::move(*resolved);
        // Merge only by a validated advertised identity. Friendly names and SRV
        // hostnames are not sufficient evidence that two services share a device.
        auto identity = normalize_mac(txt_value(service, "deviceid"));
        std::string display = record.target.front();
        if (names_equal(record.name, raop)) {
            const auto identity_separator = display.find('@');
            if (identity_separator != std::string::npos) {
                const auto raop_identity = normalize_mac(display.substr(0, identity_separator));
                if (identity.empty()) {
                    identity = raop_identity;
                }
                if (!raop_identity.empty()) {
                    display.erase(0, identity_separator + 1);
                }
            }
        }
        const auto id = identity.empty() ? "instance:" + service_key : "deviceid:" + identity;
        auto& device = grouped[id];
        device.id = id;
        auto model = txt_value(service, "model");
        if (model.empty()) {
            model = txt_value(service, "am");
        }
        // AirPlay supplies the preferred display metadata; keep RAOP endpoints.
        if (device.name.empty() || names_equal(record.name, airplay)) {
            device.name = display;
        }
        if (device.model.empty() || (!model.empty() && names_equal(record.name, airplay))) {
            device.model = model;
        }
        device.services.push_back(std::move(service));
    }
    std::vector<Device> result;
    for (auto& entry : grouped) {
        auto& device = entry.second;
        std::sort(device.services.begin(), device.services.end(),
                  [](const Service& a, const Service& b) {
                      if (a.interface_index != b.interface_index) {
                          return a.interface_index < b.interface_index;
                      }
                      return a.instance < b.instance;
                  });
        result.push_back(std::move(device));
    }
    return result;
}
} // namespace send_airplay2::detail
