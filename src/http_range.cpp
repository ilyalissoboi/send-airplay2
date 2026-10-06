// SPDX-License-Identifier: Apache-2.0
#include "send_airplay2/http_range.h"
#include <limits>
#include <string_view>

namespace {
bool number(std::string_view text, uint64_t& value) noexcept {
    if (text.empty()) return false;
    value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<uint64_t>(c - '0');
        if (value > (std::numeric_limits<uint64_t>::max() - digit) / 10)
            return false;
        value = value * 10 + digit;
    }
    return true;
}
bool bytes_unit(std::string_view unit) noexcept {
    constexpr std::string_view expected = "bytes";
    if (unit.size() != expected.size()) return false;
    for (size_t i = 0; i < unit.size(); ++i) {
        char c = unit[i];
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
        if (c != expected[i]) return false;
    }
    return true;
}
}

int32_t sap2_resolve_http_range(const char* header, size_t header_length,
                              uint64_t size, sap2_byte_range* output) {
    if (!output) return SAP2_RANGE_INVALID_ARGUMENT;
    *output = {0, 0};
    if (!header && header_length) return SAP2_RANGE_INVALID_ARGUMENT;
    *output = {0, size};
    if (!header_length) return SAP2_RANGE_FULL;
    std::string_view value(header, header_length);
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))
        value.remove_prefix(1);
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))
        value.remove_suffix(1);
    const auto equal = value.find('=');
    if (equal == value.npos || !bytes_unit(value.substr(0, equal)))
        return SAP2_RANGE_IGNORED;
    value.remove_prefix(equal + 1);
    const auto dash = value.find('-');
    if (dash == value.npos) return SAP2_RANGE_IGNORED;
    const auto left = value.substr(0, dash);
    const auto right = value.substr(dash + 1);
    uint64_t first = 0, last = 0;
    if (left.empty()) {
        uint64_t suffix = 0;
        if (!number(right, suffix)) return SAP2_RANGE_IGNORED;
        if (!suffix || !size) {
            *output = {0, 0};
            return SAP2_RANGE_UNSATISFIABLE;
        }
        const auto length = suffix < size ? suffix : size;
        *output = {size - length, length};
    } else {
        if (!number(left, first)) return SAP2_RANGE_IGNORED;
        if (!right.empty() && (!number(right, last) || last < first))
            return SAP2_RANGE_IGNORED;
        if (first >= size) {
            *output = {0, 0};
            return SAP2_RANGE_UNSATISFIABLE;
        }
        if (right.empty() || last >= size) last = size - 1;
        *output = {first, last - first + 1};
    }
    return SAP2_RANGE_PARTIAL;
}
