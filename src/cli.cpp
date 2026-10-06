// SPDX-License-Identifier: Apache-2.0
#include "send_airplay2/discovery.h"
#include "auth_cli.h"
#include "serve_cli.h"

#include <charconv>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

// Return the size of a valid UTF-8 sequence at offset, or zero for an invalid
// byte. Reject overlong encodings, surrogate code points and values above U+10FFFF.
unsigned utf8_sequence_length(std::string_view text, std::size_t offset) {
    const auto first = static_cast<unsigned char>(text[offset]);
    unsigned length = 0;
    if (first >= 0xc2 && first <= 0xdf) {
        length = 2;
    } else if (first >= 0xe0 && first <= 0xef) {
        length = 3;
    } else if (first >= 0xf0 && first <= 0xf4) {
        length = 4;
    }

    if (length == 0 || length > text.size() - offset) {
        return 0;
    }
    for (unsigned index = 1; index < length; ++index) {
        const auto continuation = static_cast<unsigned char>(text[offset + index]);
        if (continuation < 0x80 || continuation > 0xbf) {
            return 0;
        }
        if (index == 1 &&
            ((first == 0xe0 && continuation < 0xa0) || (first == 0xed && continuation >= 0xa0) ||
             (first == 0xf0 && continuation < 0x90) || (first == 0xf4 && continuation >= 0x90))) {
            return 0;
        }
    }
    return length;
}

// Use the same escaped representation in JSON and terminal diagnostics. Valid
// UTF-8 stays readable; controls and invalid bytes cannot escape into the terminal.
// Invalid bytes are displayed as \u00XX; txt_value_hex carries their exact bytes.
std::string quote_string(std::string_view text) {
    std::ostringstream output;
    output << '"';
    for (std::size_t offset = 0; offset < text.size();) {
        const auto byte = static_cast<unsigned char>(text[offset]);
        if (byte == '"' || byte == '\\') {
            output << '\\' << text[offset++];
        } else if (byte >= 0x20 && byte < 0x7f) {
            output << text[offset++];
        } else if (const auto length = utf8_sequence_length(text, offset)) {
            output << text.substr(offset, length);
            offset += length;
        } else {
            output << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                   << static_cast<unsigned>(byte) << std::dec;
            ++offset;
        }
    }
    output << '"';
    return output.str();
}

std::string feature_mask_text(std::uint64_t features) {
    std::ostringstream output;
    output << "0x" << std::hex << std::setw(16) << std::setfill('0') << features;
    return output.str();
}

std::string bytes_as_hex(std::string_view bytes) {
    std::ostringstream output;
    for (unsigned char byte : bytes) {
        output << std::hex << std::setw(2) << std::setfill('0') << static_cast<unsigned>(byte);
    }
    return output.str();
}

// Preserve the three states of an advertised boolean: true, false and unknown.
// Testing the optional's presence must not collapse an engaged false to null.
const char* json_boolean(const std::optional<bool>& value) {
    if (!value.has_value()) {
        return "null";
    }
    return *value ? "true" : "false";
}

void write_txt_json(const send_airplay2::Service& service, bool hex_values) {
    std::cout << '{';
    bool first = true;
    for (const auto& [key, value] : service.txt) {
        if (!first) {
            std::cout << ',';
        }
        first = false;
        std::cout << quote_string(key) << ':';
        if (value.has_value()) {
            std::cout << quote_string(hex_values ? bytes_as_hex(*value) : *value);
        } else {
            std::cout << "null"; // TXT flag, distinct from a key with an empty value.
        }
    }
    std::cout << '}';
}

void write_service_json(const send_airplay2::Service& service) {
    std::cout << "{\"type\":" << quote_string(service.type)
              << ",\"instance\":" << quote_string(service.instance)
              << ",\"hostname\":" << quote_string(service.hostname)
              << ",\"interface_index\":" << service.interface_index << ",\"port\":" << service.port
              << ",\"features\":";
    // Hex strings avoid losing 64-bit feature masks to JSON number precision.
    if (service.features.has_value()) {
        std::cout << quote_string(feature_mask_text(*service.features));
    } else {
        std::cout << "null";
    }
    std::cout << ",\"password_required\":" << json_boolean(service.password_required)
              << ",\"pairing_requirement\":\"unknown\",\"addresses\":[";
    bool first = true;
    for (const auto& address : service.addresses) {
        if (!first) {
            std::cout << ',';
        }
        first = false;
        std::cout << quote_string(address);
    }
    std::cout << "],\"txt\":";
    write_txt_json(service, false);
    std::cout << ",\"txt_value_hex\":";
    write_txt_json(service, true);
    std::cout << '}';
}

void write_json(const send_airplay2::DiscoveryResult& result, std::uint32_t duration_ms) {
    std::cout << "{\"schema_version\":1,\"duration_ms\":" << duration_ms
              << ",\"backend\":\"native-ipv4-mdns\",\"compatibility_validated\":false"
              << ",\"packets_received\":" << result.packets_received
              << ",\"packets_rejected\":" << result.packets_rejected << ",\"warnings\":[";
    bool first = true;
    for (const auto& warning : result.warnings) {
        if (!first) {
            std::cout << ',';
        }
        first = false;
        std::cout << quote_string(warning);
    }
    std::cout << "],\"devices\":[";
    first = true;
    for (const auto& device : result.devices) {
        if (!first) {
            std::cout << ',';
        }
        first = false;
        std::cout << "{\"id\":" << quote_string(device.id)
                  << ",\"name\":" << quote_string(device.name)
                  << ",\"model\":" << quote_string(device.model) << ",\"services\":[";
        bool first_service = true;
        for (const auto& service : device.services) {
            if (!first_service) {
                std::cout << ',';
            }
            first_service = false;
            write_service_json(service);
        }
        std::cout << "]}";
    }
    std::cout << "]}\n";
}

void write_readable(const send_airplay2::DiscoveryResult& result) {
    std::cout << "Discovery snapshot: " << result.devices.size()
              << " device(s). Advertisements do not prove playback compatibility.\n";
    for (const auto& device : result.devices) {
        std::cout << quote_string(device.name) << " [" << quote_string(device.id)
                  << "] model=" << quote_string(device.model) << '\n';
        for (const auto& service : device.services) {
            std::cout << "  " << service.type << " interface=" << service.interface_index << " "
                      << quote_string(service.hostname) << ':' << service.port << '\n';
            for (const auto& address : service.addresses) {
                std::cout << "    address=" << address << '\n';
            }
            std::cout << "    features="
                      << (service.features ? feature_mask_text(*service.features) : "unknown")
                      << " password_required="
                      << (service.password_required.has_value()
                              ? (*service.password_required ? "true" : "false")
                              : "unknown")
                      << " pairing_requirement=unknown\n";
            for (const auto& [key, value] : service.txt) {
                std::cout << "    TXT " << quote_string(key) << '='
                          << (value ? quote_string(*value) : "<flag>") << '\n';
            }
        }
    }
    for (const auto& warning : result.warnings) {
        std::cout << "Warning: " << quote_string(warning) << '\n';
    }
    std::cout << "Responses=" << result.packets_received << " rejected=" << result.packets_rejected
              << '\n';
}

std::uint32_t parse_timeout(std::string_view value) {
    std::uint32_t duration_ms = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), duration_ms);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || duration_ms == 0 ||
        duration_ms > 60000) {
        throw std::invalid_argument("timeout must be 1..60000 milliseconds");
    }
    return duration_ms;
}

void write_usage() {
    std::cout << "Usage: airplay2-cli discover [--json] [--timeout-ms 1..60000]\n";
    std::cout << "       airplay2-cli pair --address IP --profile NAME [--port 7000] [--scope-id "
                 "N] [--timeout-ms 1..60000] [--pin-timeout-ms 1..60000]\n"
                 "       airplay2-cli verify --address IP --profile NAME [--port 7000] [--scope-id "
                 "N] [--timeout-ms 1..60000]\n"
                 "       airplay2-cli forget --profile NAME\n"
                 "       airplay2-cli serve --address IP --file PATH [--port 7000] "
                 "[--listen-port 0..65535] [--timeout-ms 1..600000] [--max-connections 1..16] "
                 "[--content-type video/mp4]\n"
                 "Auth storage supports Windows desktop; pair prompts for a hidden PIN. serve "
                 "hosts one file for a receiver to fetch. Playback is not implemented.\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc >= 2 &&
        (std::string_view(argv[1]) == "pair" || std::string_view(argv[1]) == "verify" ||
         std::string_view(argv[1]) == "forget")) {
        return send_airplay2::detail::run_auth_cli(argc - 1, argv + 1);
    }
    if (argc >= 2 && std::string_view(argv[1]) == "serve") {
        return send_airplay2::detail::run_serve_cli(argc - 1, argv + 1);
    }
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        write_usage();
        return 0;
    }
    if (argc < 2 || std::string_view(argv[1]) != "discover") {
        write_usage();
        return 2;
    }
    send_airplay2::DiscoveryOptions options;
    bool json_output = false;
    try {
        for (int index = 2; index < argc; ++index) {
            const std::string_view argument = argv[index];
            if (argument == "--json") {
                json_output = true;
            } else if (argument == "--timeout-ms" && index + 1 < argc) {
                options.duration_ms = parse_timeout(argv[++index]);
            } else {
                throw std::invalid_argument("unknown or incomplete option: " +
                                            std::string(argument));
            }
        }
        const auto result = send_airplay2::discover(options);
        if (json_output) {
            write_json(result, options.duration_ms);
        } else {
            write_readable(result);
        }
        return 0;
    } catch (const std::invalid_argument& error) {
        std::cerr << "Arguments: " << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "Discovery: " << error.what() << '\n';
        return 1;
    }
}
