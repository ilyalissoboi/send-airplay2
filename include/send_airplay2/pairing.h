/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SEND_AIRPLAY2_PAIRING_H
#define SEND_AIRPLAY2_PAIRING_H
#include "send_airplay2/credentials.h"
#include "send_airplay2/playback.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PIN pairing and local profile removal (D49). Results are the SAP2_* codes of
 * playback.h. Experimental, like the playback interface. */

#define SAP2_MIN_PIN_DIGITS 4u
#define SAP2_MAX_PIN_DIGITS 8u
/* Each network phase of pairing has its own deadline. */
#define SAP2_DEFAULT_PAIR_TIMEOUT_MS 10000u
#define SAP2_MAX_PAIR_TIMEOUT_MS 60000u
/* Time allowed for read_pin. It is checked when read_pin returns; a callback
 * cannot be interrupted, so the host enforces its own UI timeout. */
#define SAP2_DEFAULT_PIN_TIMEOUT_MS 60000u
#define SAP2_MAX_PIN_TIMEOUT_MS 60000u

typedef struct sap2_pair_options {
    uint32_t struct_size;
    /* Numeric IPv4 or global/ULA IPv6 receiver address, no DNS or scope ID. */
    const char* receiver_address;
    uint16_t receiver_port;  /* Default SAP2_DEFAULT_RECEIVER_PORT. */
    const char* profile;     /* The new profile; see credentials.h for the format. */
    uint32_t timeout_ms;     /* 1..SAP2_MAX_PAIR_TIMEOUT_MS per network phase. */
    uint32_t pin_timeout_ms; /* 1..SAP2_MAX_PIN_TIMEOUT_MS for read_pin. */
    /* NULL selects the built-in platform store. */
    const sap2_credential_store* credential_store;
    void* pin_context;
    /* Called once, on the sap2_pair() thread, after the receiver has been asked
     * to display its PIN. Write the 4..8 ASCII digits the user entered into
     * `digits` (capacity SAP2_MAX_PIN_DIGITS, no terminator needed), set
     * `*length` and return SAP2_OK; return SAP2_ERROR_CANCELLED if the user
     * cancels. Leading zeros are significant. The library erases `digits`
     * after use; never log the PIN. */
    int32_t (*read_pin)(void* context, char* digits, size_t capacity, size_t* length);
} sap2_pair_options;

/** Fill defaults and struct_size; strings, store and callback are left NULL.
 * No-op for NULL. */
SAP2_API void sap2_pair_options_init(sap2_pair_options* options);

/** Pair with a receiver by PIN and save the credentials under a new profile.
 *
 * Blocks. Refuses an existing profile (SAP2_ERROR_PROFILE_EXISTS) before any
 * network work, asks the receiver to show its PIN, calls read_pin, completes the
 * authenticated exchange, and saves only authenticated credentials with
 * save_new. It then reloads them from the store and verifies a fresh connection.
 * A wrong PIN reports SAP2_ERROR_AUTHENTICATION and saves nothing.
 *
 * If the final verification fails, the saved profile is kept and the failure is
 * returned; a later sap2_cast_start() uses it, and nothing is re-paired
 * automatically. Removing the local profile does not revoke the receiver's
 * pairing. There is no cancellation besides read_pin returning
 * SAP2_ERROR_CANCELLED; each network phase is bounded by timeout_ms.
 * The store and callback must stay valid until this call returns. */
SAP2_API int32_t sap2_pair(const sap2_pair_options* options);

/** Delete a profile's local credentials from `credential_store`, or from the
 * built-in store when it is NULL. Returns SAP2_OK when a record was deleted and
 * SAP2_ERROR_PROFILE_NOT_FOUND when there was none. No network work; the
 * receiver's pairing is unchanged. */
SAP2_API int32_t sap2_forget_profile(const char* profile,
                                     const sap2_credential_store* credential_store);

#ifdef __cplusplus
}
#endif
#endif
