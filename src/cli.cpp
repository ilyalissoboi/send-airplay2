// SPDX-License-Identifier: Apache-2.0
#include "send_airplay2/discovery.h"
#include <charconv>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
std::string quoted(const std::string& text) {
    // Escape terminal controls and invalid UTF-8; preserve valid UTF-8 names.
    std::ostringstream out; out << '"';
    for (std::size_t i = 0; i < text.size();) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c == '"' || c == '\\') { out << '\\' << text[i++]; continue; }
        if (c >= 0x20 && c < 0x7f) { out << text[i++]; continue; }
        unsigned length = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        bool valid = length && i + length <= text.size();
        for (unsigned j = 1; valid && j < length; ++j) {
            const auto d = static_cast<unsigned char>(text[i + j]);
            valid = d >= 0x80 && d <= 0xbf;
            if (j == 1) valid = valid && !(c == 0xe0 && d < 0xa0) && !(c == 0xed && d >= 0xa0)
                && !(c == 0xf0 && d < 0x90) && !(c == 0xf4 && d >= 0x90);
        }
        if (valid) { out << text.substr(i, length); i += length; }
        else { out << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << unsigned(c) << std::dec; ++i; }
    }
    out << '"'; return out.str();
}
std::string mask(std::uint64_t n) {
    std::ostringstream s; s << "0x" << std::hex << std::setw(16) << std::setfill('0') << n; return s.str();
}
std::string binary_hex(const std::string& bytes) {
    std::ostringstream s;
    for (unsigned char c : bytes) s << std::hex << std::setw(2) << std::setfill('0') << unsigned(c);
    return s.str();
}
void json(const send_airplay2::DiscoveryResult& result, unsigned duration) {
    std::cout << "{\"schema_version\":1,\"duration_ms\":" << duration
              << ",\"backend\":\"native-ipv4-mdns\",\"compatibility_validated\":false,\"packets_received\":"
              << result.packets_received << ",\"packets_rejected\":" << result.packets_rejected << ",\"warnings\":[";
    bool first = true;
    for (const auto& w : result.warnings) { if (!first) std::cout << ','; first = false; std::cout << quoted(w); }
    std::cout << "],\"devices\":[";
    first = true;
    for (const auto& d : result.devices) {
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"id\":" << quoted(d.id) << ",\"name\":" << quoted(d.name) << ",\"model\":" << quoted(d.model) << ",\"services\":[";
        bool service_first = true;
        for (const auto& s : d.services) {
            if (!service_first) std::cout << ',';
            service_first = false;
            std::cout << "{\"type\":" << quoted(s.type) << ",\"instance\":" << quoted(s.instance)
                      << ",\"hostname\":" << quoted(s.hostname) << ",\"interface_index\":" << s.interface_index
                      << ",\"port\":" << s.port << ",\"features\":" << (s.features ? quoted(mask(*s.features)) : "null")
                      << ",\"password_required\":" << (s.password_required ? (*s.password_required ? "true" : "false") : "null")
                      << ",\"pairing_requirement\":\"unknown\",\"addresses\":[";
            bool address_first = true;
            for (const auto& a : s.addresses) { if (!address_first) std::cout << ','; address_first = false; std::cout << quoted(a); }
            std::cout << "],\"txt\":{";
            bool txt_first = true;
            for (const auto& t : s.txt) {
                if (!txt_first) std::cout << ',';
                txt_first = false;
                std::cout << quoted(t.first) << ':' << (t.second ? quoted(*t.second) : "null");
            }
            std::cout << "},\"txt_value_hex\":{";
            txt_first = true;
            for (const auto& t : s.txt) {
                if (!txt_first) std::cout << ',';
                txt_first = false;
                std::cout << quoted(t.first) << ':' << (t.second ? quoted(binary_hex(*t.second)) : "null");
            }
            std::cout << "}}";
        }
        std::cout << "]}";
    }
    std::cout << "]}\n";
}
void readable(const send_airplay2::DiscoveryResult& result) {
    std::cout << "Discovery snapshot: " << result.devices.size() << " device(s). Advertisements do not prove playback compatibility.\n";
    for (const auto& d : result.devices) {
        std::cout << quoted(d.name) << " [" << quoted(d.id) << "] model=" << quoted(d.model) << '\n';
        for (const auto& s : d.services) {
            std::cout << "  " << s.type << " interface=" << s.interface_index << " " << quoted(s.hostname) << ':' << s.port << '\n';
            for (const auto& a : s.addresses) std::cout << "    address=" << a << '\n';
            std::cout << "    features=" << (s.features ? mask(*s.features) : "unknown")
                      << " password_required=" << (s.password_required ? (*s.password_required ? "true" : "false") : "unknown")
                      << " pairing_requirement=unknown\n";
            for (const auto& t : s.txt) std::cout << "    TXT " << quoted(t.first) << '=' << (t.second ? quoted(*t.second) : "<flag>") << '\n';
        }
    }
    for (const auto& w : result.warnings) std::cout << "Warning: " << quoted(w) << '\n';
    std::cout << "Responses=" << result.packets_received << " rejected=" << result.packets_rejected << '\n';
}
void usage() { std::cout << "Usage: airplay2-cli discover [--json] [--timeout-ms 1..60000]\n"; }
}
int main(int argc, char** argv) {
    if (argc == 2 && std::string(argv[1]) == "--help") { usage(); return 0; }
    if (argc < 2 || std::string(argv[1]) != "discover") { usage(); return 2; }
    send_airplay2::DiscoveryOptions options;
    bool json_output = false;
    try {
        for (int i = 2; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--json") json_output = true;
            else if (arg == "--timeout-ms" && i + 1 < argc) {
                const std::string value = argv[++i];
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), options.duration_ms);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()
                    || !options.duration_ms || options.duration_ms > 60000) throw std::invalid_argument("timeout must be 1..60000 milliseconds");
            } else throw std::invalid_argument("unknown or incomplete option: " + arg);
        }
        const auto result = send_airplay2::discover(options);
        if (json_output) json(result, options.duration_ms); else readable(result);
        return 0;
    } catch (const std::invalid_argument& e) { std::cerr << "Arguments: " << e.what() << '\n'; return 2; }
      catch (const std::exception& e) { std::cerr << "Discovery: " << e.what() << '\n'; return 1; }
}
