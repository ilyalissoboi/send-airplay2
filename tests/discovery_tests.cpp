// SPDX-License-Identifier: Apache-2.0
// Synthetic fixtures authored here from RFC 6762/6763 wire formats; no receiver
// captures or third-party implementation code. These are not interoperability tests.
#include "discovery_core.h"
#include <algorithm>
#include <iostream>
#include <random>

using namespace send_airplay2::detail;
namespace {
int failures = 0;
void check(bool value, const char* message) {
    if (!value) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void u16(Bytes& b, unsigned n) {
    b.push_back(static_cast<std::uint8_t>(n >> 8));
    b.push_back(static_cast<std::uint8_t>(n));
}

void u32(Bytes& b, std::uint32_t n) {
    u16(b, n >> 16);
    u16(b, n);
}

void name(Bytes& b, const Name& n) {
    for (const auto& l : n) {
        b.push_back(static_cast<std::uint8_t>(l.size()));
        b.insert(b.end(), l.begin(), l.end());
    }
    b.push_back(0);
}

Bytes target(const Name& n) {
    Bytes b;
    name(b, n);
    return b;
}

Bytes srv(const Name& n, unsigned port) {
    Bytes b(4, 0);
    u16(b, port);
    name(b, n);
    return b;
}

Bytes txt(const std::vector<std::string>& fields) {
    Bytes b;
    for (const auto& s : fields) {
        b.push_back(static_cast<std::uint8_t>(s.size()));
        b.insert(b.end(), s.begin(), s.end());
    }
    return b;
}
struct Wire {
    Name owner;
    unsigned type;
    Bytes data;
    std::uint32_t ttl = 120;
    bool flush = false;
};
Bytes packet(const std::vector<Wire>& records) {
    Bytes b{0, 0, 0x84, 0, 0, 0};
    u16(b, static_cast<unsigned>(records.size()));
    b.resize(12);
    for (const auto& r : records) {
        name(b, r.owner);
        u16(b, r.type);
        u16(b, r.flush ? 0x8001 : 1);
        u32(b, r.ttl);
        u16(b, static_cast<unsigned>(r.data.size()));
        b.insert(b.end(), r.data.begin(), r.data.end());
    }
    return b;
}

const Name type{"_airplay", "_tcp", "local"}, audio{"_raop", "_tcp", "local"};
const Name instance{"Living.Room", "_airplay", "_tcp", "local"}, host{"receiver", "local"};
std::vector<Wire> fixture(std::string id = "AA:BB:CC:DD:EE:FF") {
    return {{type, 12, target(instance)},
            {instance, 33, srv(host, 7000), 120, true},
            {instance, 16,
             txt({"deviceid=" + id, "model=AppleTV-test", "features=0x1234,0x2", "pw=false"}), 120,
             true},
            {host, 1, {192, 0, 2, 10}, 120, true}};
}

bool feed(Cache& cache, const Bytes& b, std::uint64_t now = 0, std::uint32_t index = 7) {
    return cache.ingest(b.data(), b.size(), index, now);
}

void parser_tests() {
    const auto good = packet(fixture());
    std::vector<Record> records;
    check(parse_response(good.data(), good.size(), records) && records.size() == 4,
          "complete response");
    check(name_text(instance) == "Living\\046Room._airplay._tcp.local.",
          "label dots escaped without changing label boundaries");
    for (std::size_t length = 0; length < good.size(); ++length) {
        check(!parse_response(good.data(), length, records) && records.empty(),
              "truncation rejected atomically");
    }
    Bytes compressed{0, 0, 0x84, 0, 0, 1, 0, 1, 0, 0, 0, 0};
    name(compressed, type);
    u16(compressed, 12);
    u16(compressed, 1);
    compressed.push_back(0xc0);
    compressed.push_back(12); // RR owner -> question.
    u16(compressed, 12);
    u16(compressed, 1);
    u32(compressed, 120);
    u16(compressed, 5);
    compressed.insert(compressed.end(), {2, 'T', 'V', 0xc0, 12});
    check(parse_response(compressed.data(), compressed.size(), records) && records.size() == 1 &&
              records[0].target.front() == "TV",
          "compressed owner and PTR target");
    auto malformed = compressed;
    malformed.back() = static_cast<std::uint8_t>(malformed.size() - 2);
    check(!parse_response(malformed.data(), malformed.size(), records),
          "compression self cycle rejected");
    malformed = compressed;
    malformed[13] = 0x40;
    // Reserved label tags must be checked at label length positions.
    malformed[12] = 0x40;
    check(!parse_response(malformed.data(), malformed.size(), records),
          "reserved DNS label rejected");
    malformed = good;
    malformed[2] |= 2;
    check(!parse_response(malformed.data(), malformed.size(), records),
          "truncated response flag rejected");
    malformed = good;
    malformed[2] = 0;
    check(!parse_response(malformed.data(), malformed.size(), records),
          "query does not populate cache");
    malformed = good;
    malformed[3] = 3;
    check(!parse_response(malformed.data(), malformed.size(), records),
          "DNS error response rejected");
    const auto bad_txt = packet({{instance, 16, {5, 'x'}}});
    check(!parse_response(bad_txt.data(), bad_txt.size(), records), "truncated TXT rejected");
    const auto bad_a = packet({{host, 1, {1, 2, 3}}});
    check(!parse_response(bad_a.data(), bad_a.size(), records), "invalid A length rejected");
    const auto bad_srv = packet({{instance, 33, {0, 0, 0}}});
    check(!parse_response(bad_srv.data(), bad_srv.size(), records), "short SRV rejected");
    Bytes oversized(9001);
    check(!parse_response(oversized.data(), oversized.size(), records), "packet cap");
    const auto unknown = packet({{host, 99, {1, 2, 3}}});
    check(parse_response(unknown.data(), unknown.size(), records) && records.empty(),
          "unknown RR skipped");
    Name excessive{std::string(63, 'a'), std::string(63, 'b'), std::string(63, 'c'),
                   std::string(63, 'd')};
    const auto long_name = packet({{excessive, 1, {1, 2, 3, 4}}});
    check(!parse_response(long_name.data(), long_name.size(), records), "expanded name bound");
    // Deterministic mutation/corpus run exercises parser bounds with arbitrary bytes.
    std::mt19937 rng(6762);
    for (unsigned i = 0; i < 10000; ++i) {
        Bytes bytes = i % 2 ? good : Bytes(rng() % 512);
        for (unsigned j = 0; j < 8 && !bytes.empty(); ++j) {
            bytes[rng() % bytes.size()] = static_cast<std::uint8_t>(rng());
        }
        if (!parse_response(bytes.data(), bytes.size(), records)) {
            check(records.empty(), "failed parser has no partial records");
        }
    }
}

void feature_tests() {
    check(parse_features("0xffffffff,0xffffffff") == UINT64_MAX, "split 64-bit features");
    check(parse_features("0x1,0x2") == 0x200000001ULL, "feature word order");
    check(parse_features("0XABC") == 0xabc, "uppercase prefix");
    check(parse_features("0") == 0, "known zero mask");
    check(parse_features("ffffffffffffffff") == UINT64_MAX, "single 64-bit mask");
    for (const auto& s :
         {"", "0x", "1,", ",1", "1,2,3", "-1", "100000000,0", "10000000000000000", "1junk", " 1"}) {
        check(!parse_features(s), "malformed/overflow features rejected");
    }
}

void cache_update_tests() {
    Cache cache;
    const auto good = packet(fixture());
    check(feed(cache, good), "ingest fixture");
    auto devices = cache.devices();
    check(devices.size() == 1 && devices[0].id == "deviceid:aa:bb:cc:dd:ee:ff",
          "identity normalization");
    if (devices.empty()) {
        return;
    }
    check(devices[0].name == "Living.Room" && devices[0].model == "AppleTV-test", "name and model");
    check(devices[0].services[0].port == 7000 &&
              devices[0].services[0].addresses[0] == "192.0.2.10",
          "endpoint resolution");
    check(devices[0].services[0].features == 0x200001234ULL &&
              devices[0].services[0].password_required == false,
          "advertisement interpretation");
    feed(cache, good, 100);
    check(cache.devices().size() == 1 && cache.records().size() == 4,
          "duplicate refresh without growth");
    const auto updated = packet({{instance, 33, srv(host, 7100), 120, true},
                                 {instance, 16,
                                  txt({"MODEL=New", "model=Ignored", "pw=true", "flag",
                                       "empty=", std::string("binary=\0\xff", 9)}),
                                  120, true}});
    feed(cache, updated, 2000);
    devices = cache.devices();
    check(devices.size() == 1 && devices[0].model == "New" && devices[0].services[0].port == 7100,
          "SRV/TXT replacement");
    if (!devices.empty()) {
        const auto& s = devices[0].services[0];
        check(s.password_required == true && s.txt.count("flag") && !s.txt.at("flag") &&
                  s.txt.at("empty") == "" && s.txt.at("binary")->size() == 2,
              "TXT duplicates/flags/empty/binary preserved");
    }
    cache.expire(3000);
    check(cache.records().size() == 4, "outdated unique records removed after flush grace");
    auto truncated = good;
    truncated.pop_back();
    check(!feed(cache, truncated, 4000) && cache.records().size() == 4,
          "malformed packet has no cache effects");
}

// Resolution completes only when an instance has SRV data and scoped addresses.
void cache_resolution_tests() {
    Cache incomplete;
    const auto f = fixture();
    feed(incomplete, packet({f[0]}));
    check(incomplete.devices().empty() && incomplete.questions().size() == 4,
          "PTR requests SRV and TXT");
    feed(incomplete, packet({f[1], f[2]}));
    check(incomplete.devices().empty() && incomplete.questions().size() == 6,
          "SRV requests A and AAAA");
    feed(incomplete, packet({f[3]}));
    check(incomplete.devices().size() == 1, "out-of-order resolution");
    Bytes v6(16, 0);
    v6[0] = 0xfe;
    v6[1] = 0x80;
    v6[15] = 1;
    feed(incomplete, packet({{host, 28, v6, 120, true}}));
    check(incomplete.devices()[0].services[0].addresses.size() == 2 &&
              incomplete.devices()[0].services[0].addresses[1] == "fe80:0:0:0:0:0:0:1%7",
          "IPv6 scope preserved");
}

// Goodbye and unique-RRSet flush use rescue grace rather than immediate deletion.
void cache_lifecycle_tests() {
    const auto good = packet(fixture());
    const auto f = fixture();
    Cache expiry;
    feed(expiry, good);
    expiry.expire(120000);
    check(expiry.devices().empty() && expiry.records().empty(), "TTL expiry removes receiver");
    feed(expiry, good);
    feed(expiry, packet({{type, 12, target(instance), 0}}), 1000);
    expiry.expire(1999);
    check(expiry.devices().size() == 1, "goodbye grace");
    expiry.expire(2000);
    check(expiry.devices().empty(), "goodbye expiry");
    feed(expiry, good, 3000);
    feed(expiry, packet({{type, 12, target(instance), 0}}), 4000);
    feed(expiry, packet({f[0]}), 4500);
    expiry.expire(5000);
    check(expiry.devices().size() == 1, "goodbye rescue by refreshed PTR");
    expiry.retain_interfaces({8});
    check(expiry.devices().empty(), "removed interface invalidates records");
    Cache rrset;
    feed(rrset, good);
    feed(rrset,
         packet({{host, 1, {192, 0, 2, 11}, 120, true}, {host, 1, {192, 0, 2, 12}, 120, true}}),
         2000);
    check(rrset.devices()[0].services[0].addresses.size() == 3,
          "flush retains old address during grace");
    rrset.expire(3000);
    check(rrset.devices()[0].services[0].addresses.size() == 2,
          "flush preserves whole new multi-address RRSet");
    feed(rrset, packet({{host, 1, {192, 0, 2, 13}, 120, true}}), 3500);
    feed(rrset, packet({{host, 1, {192, 0, 2, 14}, 120, true}}), 3600);
    rrset.expire(4500);
    check(rrset.devices()[0].services[0].addresses.size() == 2,
          "back-to-back RRSet members protected");
    Cache unavailable;
    auto no_service = fixture();
    no_service[1].data = srv({}, 7000);
    feed(unavailable, packet(no_service));
    check(unavailable.devices().empty(), "SRV root means unavailable");
    Cache isolation;
    feed(isolation, packet({f[0], f[1], f[2]}), 0, 7);
    feed(isolation, packet({f[3]}), 0, 8);
    check(isolation.devices().empty(), "addresses do not cross interfaces");
}

// A matching MAC identity merges services; a hostname match alone does not.
void cache_identity_tests() {
    const auto good = packet(fixture());
    const Name audio_instance{"AABBCCDDEEFF@Living Room", "_raop", "_tcp", "local"};
    const auto audio_records = packet({{audio, 12, target(audio_instance)},
                                       {audio_instance, 33, srv(host, 5000)},
                                       {audio_instance, 16, txt({"am=AudioModel", "ft=1"})}});
    Cache merge;
    feed(merge, good);
    feed(merge, audio_records);
    const auto devices = merge.devices();
    check(devices.size() == 1 && devices[0].services.size() == 2 &&
              devices[0].model == "AppleTV-test",
          "RAOP identity merges and AirPlay metadata preferred");
    Cache separate;
    feed(separate, packet(fixture("invalid")));
    feed(separate, audio_records);
    check(separate.devices().size() == 2, "hostname alone never merges services");
}

void cache_limit_tests() {
    Cache bounded;
    for (unsigned i = 0; i < 2050; ++i) {
        feed(bounded, packet({{{"host" + std::to_string(i), "local"}, 1, {1, 2, 3, 4}}}));
    }
    check(bounded.overflowed() && bounded.records().size() == 2048, "cache resource cap");
}

void query_tests() {
    Cache cache;
    feed(cache, packet(fixture()));
    auto b = query(cache.questions(), cache.records(), 7, 1000);
    check(b[5] == 6 && b[7] == 4 && b.size() <= 1400, "bounded questions with known answers");
    std::vector<Record> records;
    b[2] = 0x84;
    check(parse_response(b.data(), b.size(), records) && records.size() == 4,
          "known answer encoding round trip");
    b = query(cache.questions(), cache.records(), 8, 1000);
    check(b[7] == 0, "known answers scoped to interface");
    b = query(cache.questions(), cache.records(), 7, 61000);
    check(b[7] == 0, "stale known answers omitted");
}
} // namespace
int main() {
    parser_tests();
    feature_tests();
    cache_update_tests();
    cache_resolution_tests();
    cache_lifecycle_tests();
    cache_identity_tests();
    cache_limit_tests();
    query_tests();
    if (failures) {
        return 1;
    }
    std::cout << "Discovery parser/cache/query tests passed (synthetic fixtures; no receiver "
                 "validation).\n";
    return 0;
}
