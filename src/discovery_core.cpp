// SPDX-License-Identifier: Apache-2.0
#include "discovery_core.h"
#include <algorithm>
#include <iomanip>
#include <sstream>

namespace send_airplay2::detail {
namespace {
constexpr std::size_t max_packet = 9000;
constexpr std::size_t max_records = 2048;
const Name airplay{"_airplay", "_tcp", "local"};
const Name raop{"_raop", "_tcp", "local"};
std::string lower(std::string s) {
    for (auto& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
    return s;
}
bool same_name(const Name& a, const Name& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) if (lower(a[i]) != lower(b[i])) return false;
    return true;
}
bool instance_of(const Name& name, const Name& type) {
    return name.size() == type.size() + 1 && same_name(Name(name.begin() + 1, name.end()), type);
}
std::uint16_t u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>((static_cast<unsigned>(p[0]) << 8) | p[1]);
}
std::uint32_t u32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(u16(p)) << 16) | u16(p + 2);
}
bool read_name(const std::uint8_t* p, std::size_t size, std::size_t& cursor, Name& out) {
    std::size_t pos = cursor, wire_size = 1, steps = 0;
    bool jumped = false;
    out.clear();
    while (pos < size && ++steps <= 128) {
        const auto length = p[pos++];
        if ((length & 0xc0) == 0xc0) {
            if (pos >= size) return false;
            const auto target = static_cast<std::size_t>(((length & 0x3f) << 8) | p[pos++]);
            if (target >= pos - 2) return false; // Backward pointers only; prevents cycles.
            if (!jumped) cursor = pos;
            jumped = true;
            pos = target;
        } else {
            if (length & 0xc0) return false;
            if (!length) {
                if (!jumped) cursor = pos;
                return true;
            }
            if (length > size - pos || (wire_size += length + 1) > 255) return false;
            out.emplace_back(reinterpret_cast<const char*>(p + pos), length);
            pos += length;
        }
    }
    return false;
}
bool valid_txt(const Bytes& bytes) {
    std::size_t pos = 0;
    while (pos < bytes.size()) {
        const auto length = bytes[pos++];
        if (length > bytes.size() - pos) return false;
        pos += length;
    }
    return true;
}
std::map<std::string, std::optional<std::string>> txt_fields(const Bytes& bytes) {
    std::map<std::string, std::optional<std::string>> fields;
    for (std::size_t pos = 0; pos < bytes.size();) {
        const auto length = bytes[pos++];
        const std::string entry(reinterpret_cast<const char*>(bytes.data() + pos), length);
        pos += length;
        const auto eq = entry.find('=');
        const auto key = entry.substr(0, eq);
        if (key.empty() || std::any_of(key.begin(), key.end(), [](unsigned char c) {
            return c < 0x20 || c > 0x7e;
        })) continue;
        // RFC 6763: first duplicate wins; flag differs from empty value.
        fields.emplace(lower(key), eq == std::string::npos ? std::optional<std::string>{}
                                                         : entry.substr(eq + 1));
    }
    return fields;
}
std::string field(const Service& s, const char* key) {
    const auto it = s.txt.find(key);
    return it != s.txt.end() && it->second ? *it->second : std::string{};
}
int hex(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
std::string mac(const std::string& value) {
    std::string digits;
    if (value.size() == 12) digits = value;
    else if (value.size() == 17) {
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (i % 3 == 2) { if (value[i] != ':') return {}; }
            else digits += value[i];
        }
    } else return {};
    if (std::any_of(digits.begin(), digits.end(), [](char c) { return hex(c) < 0; })) return {};
    std::string result;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i && i % 2 == 0) result += ':';
        result += lower(digits.substr(i, 1));
    }
    return result;
}
bool same_record(const Record& a, const Record& b) {
    return a.type == b.type && same_name(a.name, b.name) && same_name(a.target, b.target)
        && a.port == b.port && a.data == b.data;
}
void put16(Bytes& b, std::uint16_t n) {
    b.push_back(static_cast<std::uint8_t>(n >> 8)); b.push_back(static_cast<std::uint8_t>(n));
}
void put32(Bytes& b, std::uint32_t n) { put16(b, static_cast<std::uint16_t>(n >> 16)); put16(b, static_cast<std::uint16_t>(n)); }
void put_name(Bytes& b, const Name& name) {
    for (const auto& label : name) {
        b.push_back(static_cast<std::uint8_t>(label.size()));
        b.insert(b.end(), label.begin(), label.end());
    }
    b.push_back(0);
}
std::string address(const Bytes& data, std::uint32_t index) {
    std::ostringstream s;
    if (data.size() == 4) {
        s << unsigned(data[0]) << '.' << unsigned(data[1]) << '.' << unsigned(data[2]) << '.' << unsigned(data[3]);
    } else {
        for (std::size_t i = 0; i < 16; i += 2) {
            if (i) s << ':';
            s << std::hex << u16(data.data() + i);
        }
        if (data[0] == 0xfe && (data[1] & 0xc0) == 0x80) s << '%' << std::dec << index;
    }
    return s.str();
}
}

std::string name_text(const Name& name) {
    std::string text;
    for (const auto& label : name) {
        for (unsigned char c : label) {
            if (c == '.' || c == '\\' || c < 0x20 || c == 0x7f) {
                text += '\\'; text += static_cast<char>('0' + c / 100);
                text += static_cast<char>('0' + (c / 10) % 10); text += static_cast<char>('0' + c % 10);
            } else text += static_cast<char>(c);
        }
        text += '.';
    }
    return text;
}

bool parse_response(const std::uint8_t* p, std::size_t size, std::vector<Record>& out) {
    out.clear();
    if (!p || size < 12 || size > max_packet) return false;
    const auto flags = u16(p + 2);
    // Only complete, standard, successful responses; ignore query/probe data.
    if (!(flags & 0x8000) || (flags & 0x7a0f)) return false;
    const auto questions = u16(p + 4);
    const auto count = static_cast<unsigned>(u16(p + 6)) + u16(p + 8) + u16(p + 10);
    if (questions > 128 || count > 512) return false;
    std::size_t pos = 12;
    Name name;
    for (unsigned i = 0; i < questions; ++i) {
        if (!read_name(p, size, pos, name) || size - pos < 4) return false;
        pos += 4;
    }
    std::vector<Record> parsed;
    for (unsigned i = 0; i < count; ++i) {
        Record r;
        if (!read_name(p, size, pos, r.name) || size - pos < 10) return false;
        r.type = u16(p + pos);
        const auto klass = u16(p + pos + 2);
        r.flush = (klass & 0x8000) != 0;
        r.ttl = u32(p + pos + 4);
        const auto length = u16(p + pos + 8);
        pos += 10;
        if (length > size - pos) return false;
        const auto end = pos + length;
        if ((klass & 0x7fff) == 1) {
            if (r.type == 12 || r.type == 33) {
                if (r.type == 33) {
                    if (length < 7) return false;
                    r.data.assign(p + pos, p + pos + 4); // Preserve priority/weight for equality.
                    r.port = u16(p + pos + 4); pos += 6;
                }
                if (!read_name(p, size, pos, r.target) || pos != end) return false;
            } else if (r.type == 16 || r.type == 1 || r.type == 28) {
                r.data.assign(p + pos, p + end);
                if ((r.type == 1 && length != 4) || (r.type == 28 && length != 16)
                    || (r.type == 16 && !valid_txt(r.data))) return false;
            } else { pos = end; continue; }
            parsed.push_back(std::move(r));
        }
        pos = end;
    }
    if (pos != size) return false;
    out = std::move(parsed);
    return true;
}

Bytes query(const std::vector<Question>& questions, const std::vector<CachedRecord>& known,
            std::uint32_t index, std::uint64_t now) {
    Bytes b(12, 0);
    std::uint16_t qcount = 0, acount = 0;
    for (const auto& q : questions) {
        Bytes entry; put_name(entry, q.name); put16(entry, q.type); put16(entry, 1); // QM, not QU.
        if (b.size() + entry.size() > 1400) break;
        b.insert(b.end(), entry.begin(), entry.end()); ++qcount;
    }
    for (const auto& c : known) {
        const auto& r = c.record;
        if (c.interface_index != index || c.expires_ms <= now) continue;
        const auto ttl = (c.expires_ms - now) / 1000;
        if (ttl < (static_cast<std::uint64_t>(r.ttl) + 1) / 2) continue;
        if (!std::any_of(questions.begin(), questions.begin() + qcount, [&](const Question& q) {
            return q.type == r.type && same_name(q.name, r.name);
        })) continue;
        Bytes entry, data;
        put_name(entry, r.name); put16(entry, r.type); put16(entry, 1);
        put32(entry, static_cast<std::uint32_t>(ttl));
        if (r.type == 12) put_name(data, r.target);
        else if (r.type == 33) { data = r.data; put16(data, r.port); put_name(data, r.target); }
        else data = r.data;
        put16(entry, static_cast<std::uint16_t>(data.size())); entry.insert(entry.end(), data.begin(), data.end());
        if (b.size() + entry.size() > 1400) break;
        b.insert(b.end(), entry.begin(), entry.end()); ++acount;
    }
    b[4] = static_cast<std::uint8_t>(qcount >> 8); b[5] = static_cast<std::uint8_t>(qcount);
    b[6] = static_cast<std::uint8_t>(acount >> 8); b[7] = static_cast<std::uint8_t>(acount);
    return b;
}

std::optional<std::uint64_t> parse_features(const std::string& text) {
    const auto comma = text.find(',');
    auto word = [](std::string s, std::size_t max_digits) -> std::optional<std::uint64_t> {
        if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) s.erase(0, 2);
        if (s.empty() || s.size() > max_digits) return {};
        std::uint64_t n = 0;
        for (char c : s) { const auto d = hex(c); if (d < 0) return {}; n = (n << 4) | static_cast<unsigned>(d); }
        return n;
    };
    if (comma == std::string::npos) return word(text, 16);
    const auto low = word(text.substr(0, comma), 8), high = word(text.substr(comma + 1), 8);
    if (!low || !high) return {};
    return *low | (*high << 32);
}

bool Cache::ingest(const std::uint8_t* data, std::size_t size, std::uint32_t index, std::uint64_t now) {
    std::vector<Record> parsed;
    if (!index || !parse_response(data, size, parsed)) return false;
    expire(now);
    // Flush grace and recent-record protection apply to the entire RRSet.
    for (const auto& r : parsed) if (r.flush && r.ttl) {
        for (auto& c : records_) if (c.interface_index == index && c.record.type == r.type
            && same_name(c.record.name, r.name) && now - c.seen_ms >= 1000) {
            c.expires_ms = std::min(c.expires_ms, now + 1000);
        }
    }
    for (auto& r : parsed) {
        auto it = std::find_if(records_.begin(), records_.end(), [&](const CachedRecord& c) {
            return c.interface_index == index && same_record(c.record, r);
        });
        if (!r.ttl) {
            if (it != records_.end()) it->expires_ms = std::min(it->expires_ms, now + 1000);
        } else {
            const auto expires = now + static_cast<std::uint64_t>(r.ttl) * 1000;
            if (it != records_.end()) *it = {std::move(r), index, now, expires};
            else if (records_.size() < max_records) records_.push_back({std::move(r), index, now, expires});
            else overflowed_ = true;
        }
    }
    return true;
}
void Cache::expire(std::uint64_t now) {
    records_.erase(std::remove_if(records_.begin(), records_.end(), [now](const CachedRecord& c) {
        return c.expires_ms <= now;
    }), records_.end());
}
void Cache::retain_interfaces(const std::vector<std::uint32_t>& active) {
    records_.erase(std::remove_if(records_.begin(), records_.end(), [&](const CachedRecord& c) {
        return std::find(active.begin(), active.end(), c.interface_index) == active.end();
    }), records_.end());
}
std::vector<Question> Cache::questions() const {
    std::vector<Question> result{{airplay, 12}, {raop, 12}};
    auto add = [&](const Name& name, std::uint16_t type) {
        if (result.size() < 128 && std::none_of(result.begin(), result.end(), [&](const Question& q) {
            return q.type == type && same_name(q.name, name);
        })) result.push_back({name, type});
    };
    for (const auto& c : records_) {
        const auto& r = c.record;
        if (r.type != 12 || (!same_name(r.name, airplay) && !same_name(r.name, raop))
            || !instance_of(r.target, r.name)) continue;
        add(r.target, 33); add(r.target, 16);
        for (const auto& srv : records_) if (srv.interface_index == c.interface_index
            && srv.record.type == 33 && same_name(srv.record.name, r.target) && !srv.record.target.empty()) {
            add(srv.record.target, 1); add(srv.record.target, 28);
        }
    }
    return result;
}
std::vector<Device> Cache::devices() const {
    std::map<std::string, Device> grouped;
    std::vector<std::string> seen;
    for (const auto& ptr : records_) {
        const auto& r = ptr.record;
        if (r.type != 12 || (!same_name(r.name, airplay) && !same_name(r.name, raop))
            || !instance_of(r.target, r.name)) continue;
        const auto service_key = std::to_string(ptr.interface_index) + ':' + lower(name_text(r.target));
        if (std::find(seen.begin(), seen.end(), service_key) != seen.end()) continue;
        seen.push_back(service_key);
        const CachedRecord* srv = nullptr;
        const CachedRecord* txt = nullptr;
        for (const auto& c : records_) if (c.interface_index == ptr.interface_index && same_name(c.record.name, r.target)) {
            if (c.record.type == 33 && (!srv || c.seen_ms >= srv->seen_ms)) srv = &c;
            if (c.record.type == 16 && (!txt || c.seen_ms >= txt->seen_ms)) txt = &c;
        }
        if (!srv || !srv->record.port || srv->record.target.empty()) continue;
        Service s;
        s.type = lower(name_text(r.name)); s.instance = name_text(r.target);
        s.interface_index = ptr.interface_index; s.hostname = name_text(srv->record.target); s.port = srv->record.port;
        if (txt) s.txt = txt_fields(txt->record.data);
        for (const auto& c : records_) if (c.interface_index == ptr.interface_index
            && (c.record.type == 1 || c.record.type == 28) && same_name(c.record.name, srv->record.target)) {
            s.addresses.push_back(address(c.record.data, ptr.interface_index));
        }
        std::sort(s.addresses.begin(), s.addresses.end());
        s.addresses.erase(std::unique(s.addresses.begin(), s.addresses.end()), s.addresses.end());
        if (s.addresses.empty()) continue; // Only expose resolved endpoints.
        s.features = parse_features(field(s, "features").empty() ? field(s, "ft") : field(s, "features"));
        const auto pw = lower(field(s, "pw"));
        if (pw == "true" || pw == "1") s.password_required = true;
        else if (pw == "false" || pw == "0") s.password_required = false;
        auto identity = mac(field(s, "deviceid"));
        std::string display = r.target.front();
        if (same_name(r.name, raop)) {
            const auto at = display.find('@');
            if (at != std::string::npos) {
                const auto raop_identity = mac(display.substr(0, at));
                if (identity.empty()) identity = raop_identity;
                if (!raop_identity.empty()) display.erase(0, at + 1);
            }
        }
        const auto id = identity.empty() ? "instance:" + service_key : "deviceid:" + identity;
        auto& d = grouped[id]; d.id = id;
        const auto model = field(s, "model").empty() ? field(s, "am") : field(s, "model");
        if (d.name.empty() || same_name(r.name, airplay)) d.name = display;
        if (d.model.empty() || (!model.empty() && same_name(r.name, airplay))) d.model = model;
        d.services.push_back(std::move(s));
    }
    std::vector<Device> result;
    for (auto& entry : grouped) {
        auto& d = entry.second;
        std::sort(d.services.begin(), d.services.end(), [](const Service& a, const Service& b) {
            if (a.interface_index != b.interface_index) return a.interface_index < b.interface_index;
            return a.instance < b.instance;
        });
        result.push_back(std::move(d));
    }
    return result;
}
}
