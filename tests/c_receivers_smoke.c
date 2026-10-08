/* SPDX-License-Identifier: Apache-2.0 */
/* Compiles send_airplay2/receivers.h as C and checks the refusals that need no
 * network: no scan starts, so nothing is sent or bound. Scan results and their
 * mapping are covered by receiver_list_tests with a scripted discover(). */
#include "send_airplay2/receivers.h"
#include <stdio.h>

static int failures = 0;
static void check(int passed, const char* scenario) {
    if (!passed) {
        fprintf(stderr, "FAIL: %s\n", scenario);
        ++failures;
    }
}

int main(void) {
    sap2_receiver_list* list = (sap2_receiver_list*)&failures;
    sap2_receiver_info info = {0};
    info.struct_size = sizeof(info);

    /* Two uint32, five pointer-sized fields, a uint16, then a naturally aligned
     * uint64: bindings (C#) mirror this layout, so a change must be deliberate. */
    check(sizeof(sap2_receiver_info) == (sizeof(void*) == 8 ? 64u : 40u),
          "receiver info layout size");
    check(SAP2_DEFAULT_DISCOVERY_MS >= 1u && SAP2_DEFAULT_DISCOVERY_MS <= SAP2_MAX_DISCOVERY_MS,
          "default duration within bounds");
    check(sap2_discover(SAP2_DEFAULT_DISCOVERY_MS, NULL) == SAP2_ERROR_INVALID_ARGUMENT,
          "null list pointer refused");
    check(sap2_discover(0u, &list) == SAP2_ERROR_INVALID_ARGUMENT && list == NULL,
          "zero duration refused and list cleared");
    list = (sap2_receiver_list*)&failures;
    check(sap2_discover(SAP2_MAX_DISCOVERY_MS + 1u, &list) == SAP2_ERROR_INVALID_ARGUMENT &&
              list == NULL,
          "duration above the maximum refused and list cleared");
    check(sap2_receiver_list_count(NULL) == 0u, "count of a null list");
    check(sap2_receiver_list_get(NULL, 0u, &info) == SAP2_ERROR_INVALID_ARGUMENT,
          "get from a null list refused");
    check(info.id == NULL && info.flags == 0u, "refused get leaves info unwritten");
    sap2_receiver_list_free(NULL);

    if (failures) {
        fprintf(stderr, "%d receivers smoke check(s) failed\n", failures);
        return 1;
    }
    puts("receivers C smoke passed");
    return 0;
}
