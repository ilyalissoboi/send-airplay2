/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SEND_AIRPLAY2_RECEIVERS_H
#define SEND_AIRPLAY2_RECEIVERS_H
#include "send_airplay2/export.h"
#include "send_airplay2/playback.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Receiver discovery (C interface over send_airplay2/discovery.h). Results are
 * the SAP2_* codes of playback.h. Experimental, like the playback interface.
 *
 * Everything here is advertised over multicast DNS and unauthenticated: a name,
 * identity or model proves nothing about the device. Pairing (pairing.h) pins
 * the receiver's identity; discovery only finds where to connect. An empty scan
 * is not proof that no receiver exists. */

/* Scan duration in milliseconds: the C++ discover() default and its upper bound. */
#define SAP2_DEFAULT_DISCOVERY_MS 5000u
#define SAP2_MAX_DISCOVERY_MS 60000u

/* sap2_receiver_info.flags */
#define SAP2_RECEIVER_PASSWORD_REQUIRED 1u /* Advertised AirPlay password ("pw"). */
#define SAP2_RECEIVER_HAS_FEATURES 2u      /* `features` holds the advertised mask. */

/* An owning snapshot of one scan; free with sap2_receiver_list_free(). */
typedef struct sap2_receiver_list sap2_receiver_list;

/* One receiver that advertises an AirPlay video service. Strings are
 * NUL-terminated and owned by the list: valid until sap2_receiver_list_free(). */
typedef struct sap2_receiver_info {
    uint32_t struct_size; /* Caller sets sizeof(sap2_receiver_info). */
    uint32_t flags;       /* SAP2_RECEIVER_* bits. */
    const char* id;       /* Advertised identity; stable across scans, not authenticated. */
    /* Friendly name exactly as advertised: arbitrary network bytes, not
     * necessarily UTF-8, possibly with embedded NULs; use name_length. */
    const char* name;
    size_t name_length; /* Bytes in name, excluding the terminator. */
    const char* model;  /* Advertised model, or "" when unknown. */
    /* Numeric address to pass as receiver_address: the first IPv4 address of the
     * AirPlay service, otherwise its first non-link-local IPv6 address; "" when
     * it has neither (link-local IPv6 is not supported for casting). */
    const char* address;
    uint16_t port;     /* AirPlay control port for `address`. */
    uint64_t features; /* Advertised feature mask when HAS_FEATURES; not a compatibility claim. */
} sap2_receiver_info;

/** Scan active IPv4 multicast interfaces for `duration_ms` (1..SAP2_MAX_DISCOVERY_MS)
 * and return the receivers that advertise AirPlay video. Blocks for the whole
 * scan; starts no background work and touches no credentials. `*list` is set on
 * SAP2_OK and to NULL otherwise. SAP2_ERROR_CONNECTION reports a local network
 * setup or I/O failure (for example, no multicast access). */
SAP2_API int32_t sap2_discover(uint32_t duration_ms, sap2_receiver_list** list);

/** Number of receivers in the list; 0 for NULL. */
SAP2_API size_t sap2_receiver_list_count(const sap2_receiver_list* list);

/** Copy entry `index` (0..count-1) into `info`, whose struct_size the caller
 * sets. SAP2_ERROR_INVALID_ARGUMENT for NULL, a short struct or a bad index. */
SAP2_API int32_t sap2_receiver_list_get(const sap2_receiver_list* list, size_t index,
                                        sap2_receiver_info* info);

/** Free the list and every string it owns. NULL is a no-op. */
SAP2_API void sap2_receiver_list_free(sap2_receiver_list* list);

#ifdef __cplusplus
}
#endif
#endif
