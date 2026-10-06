// SPDX-License-Identifier: Apache-2.0
#include "send_airplay2/http_range.h"
#include <cstring>
#include <iostream>
#include <limits>
#include <string>

int main() {
    struct Case { const char* header; uint64_t size; int32_t status;
                  uint64_t offset; uint64_t length; };
    const auto max = std::numeric_limits<uint64_t>::max();
    const Case cases[] = {
        {nullptr, 100, SAP2_RANGE_FULL, 0, 100},
        {"", 0, SAP2_RANGE_FULL, 0, 0},
        {"bytes=0-0", 100, SAP2_RANGE_PARTIAL, 0, 1},
        {"bytes=10-19", 100, SAP2_RANGE_PARTIAL, 10, 10},
        {"bytes=90-999", 100, SAP2_RANGE_PARTIAL, 90, 10},
        {"bytes=99-", 100, SAP2_RANGE_PARTIAL, 99, 1},
        {"bytes=-10", 100, SAP2_RANGE_PARTIAL, 90, 10},
        {"bytes=-999", 100, SAP2_RANGE_PARTIAL, 0, 100},
        {"\tBYTES=0-9 ", 100, SAP2_RANGE_PARTIAL, 0, 10},
        {"bytes=100-", 100, SAP2_RANGE_UNSATISFIABLE, 0, 0},
        {"bytes=0-", 0, SAP2_RANGE_UNSATISFIABLE, 0, 0},
        {"bytes=-1", 0, SAP2_RANGE_UNSATISFIABLE, 0, 0},
        {"bytes=-0", 100, SAP2_RANGE_UNSATISFIABLE, 0, 0},
        {"bytes=20-10", 100, SAP2_RANGE_IGNORED, 0, 100},
        {"bytes=0-1,4-5", 100, SAP2_RANGE_IGNORED, 0, 100},
        {"items=0-1", 100, SAP2_RANGE_IGNORED, 0, 100},
        {"bytes=", 100, SAP2_RANGE_IGNORED, 0, 100},
        {"bytes=-", 100, SAP2_RANGE_IGNORED, 0, 100},
        {"bytes=+1-2", 100, SAP2_RANGE_IGNORED, 0, 100},
        {"bytes=1-2\r\n", 100, SAP2_RANGE_IGNORED, 0, 100},
        {"bytes=18446744073709551616-", 100, SAP2_RANGE_IGNORED, 0, 100},
        {"bytes=0-18446744073709551615", max, SAP2_RANGE_PARTIAL, 0, max},
        {"bytes=18446744073709551614-", max, SAP2_RANGE_PARTIAL, max-1, 1},
        {"bytes=18446744073709551615-", max, SAP2_RANGE_UNSATISFIABLE, 0, 0},
        {"bytes=-18446744073709551615", max, SAP2_RANGE_PARTIAL, 0, max},
    };
    int failures = 0;
    for (const auto& c : cases) {
        sap2_byte_range r{77, 77};
        const auto status = sap2_resolve_http_range(c.header,
            c.header ? std::strlen(c.header) : 0, c.size, &r);
        if (status != c.status || r.offset != c.offset || r.length != c.length) {
            std::cerr << "Failed: " << (c.header ? c.header : "<absent>") << '\n';
            ++failures;
        }
    }
    sap2_byte_range r{77, 77};
    if (sap2_resolve_http_range(nullptr, 1, 100, &r) != SAP2_RANGE_INVALID_ARGUMENT
        || r.offset || r.length) ++failures;
    if (sap2_resolve_http_range(nullptr, 0, 100, nullptr) != SAP2_RANGE_INVALID_ARGUMENT)
        ++failures;
    const char raw[] = {'b','y','t','e','s','=','2','-','3'};
    if (sap2_resolve_http_range(raw, sizeof(raw), 100, &r) != SAP2_RANGE_PARTIAL
        || r.offset != 2 || r.length != 2) ++failures;
    const char nul[] = "bytes=1-2\0ignored";
    if (sap2_resolve_http_range(nul, sizeof(nul)-1, 100, &r) != SAP2_RANGE_IGNORED)
        ++failures;
    // Exhaustive small-domain check against interval arithmetic, including EOF.
    for (uint64_t size = 0; size < 32; ++size)
        for (uint64_t start = 0; start < 40; ++start)
            for (uint64_t end = start; end < 40; ++end) {
                const auto h = "bytes=" + std::to_string(start) + "-" + std::to_string(end);
                const auto status = sap2_resolve_http_range(h.data(), h.size(), size, &r);
                if (start >= size) {
                    if (status != SAP2_RANGE_UNSATISFIABLE || r.offset || r.length) ++failures;
                } else {
                    const auto expected_end = end < size ? end : size - 1;
                    if (status != SAP2_RANGE_PARTIAL || r.offset != start
                        || r.length != expected_end - start + 1) ++failures;
                }
            }
    std::cout << "Range tests: " << failures << " failures\n";
    return failures ? 1 : 0;
}
