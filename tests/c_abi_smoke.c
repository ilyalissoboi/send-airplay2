/* SPDX-License-Identifier: Apache-2.0 */
#include "send_airplay2/http_range.h"
int main(void) {
    sap2_byte_range range;
    const char header[] = "bytes=2-4";
    return sap2_resolve_http_range(header, sizeof(header)-1, 10, &range)
        != SAP2_RANGE_PARTIAL || range.offset != 2 || range.length != 3;
}
