/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SEND_AIRPLAY2_CREDENTIALS_H
#define SEND_AIRPLAY2_CREDENTIALS_H
#include "send_airplay2/export.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Host-provided credential store (D49, docs/credential-interface.md).
 *
 * A host whose secure storage is only practical from its own code (a UWP app's
 * PasswordVault, Android Keystore-backed storage, a headless service's vault)
 * supplies this callback table. Without one, the library uses its built-in
 * platform store; with neither, calls report SAP2_ERROR_UNSUPPORTED.
 *
 * Records are opaque. Store and return them byte for byte; never parse them.
 * A record contains the controller's private signing seed, so it belongs only
 * in secure storage, never in plain files, logs or app settings. A store whose
 * secrets are text encodes the bytes itself (for example as Base64). A host
 * store may sync or roam credentials; whether that is acceptable is the host's
 * decision. Built-in stores do not roam.
 *
 * Profiles are 1..64 lower-case ASCII letters, digits and ._- characters,
 * starting with a letter or digit, passed NUL-terminated and borrowed for the
 * call. They select a local trust slot, not a receiver.
 *
 * Threading and lifetime: callbacks run synchronously on the thread that called
 * the library function, only inside sap2_cast_start(), sap2_pair() and
 * sap2_forget_profile(), never on session worker threads. The library copies
 * this table when it validates the options that carry it. The callbacks and
 * whatever `context` points to must stay valid from sap2_cast_create() until
 * sap2_cast_destroy() returns, or until a sap2_pair()/sap2_forget_profile()
 * call returns. Buffers passed to callbacks are borrowed for that call only and
 * erased by the library afterwards. */

/* Store callback results. */
#define SAP2_STORE_OK 0
#define SAP2_STORE_ABSENT 1      /* No record under that profile. */
#define SAP2_STORE_EXISTS 2      /* save_new: a record already exists; it is not replaced. */
#define SAP2_STORE_UNAVAILABLE 3 /* The store cannot be used now; nothing was changed. */

/* Upper bound of an encoded record, in bytes (version 1). */
#define SAP2_CREDENTIAL_RECORD_MAX 203u

typedef struct sap2_credential_store {
    uint32_t struct_size; /* sizeof(sap2_credential_store) */
    void* context;
    /* Copy the profile's record into `buffer` (`capacity` bytes, at least
     * SAP2_CREDENTIAL_RECORD_MAX) and set `*length`. Return SAP2_STORE_ABSENT if
     * there is none. A length of zero or above `capacity` is treated as a
     * malformed record. */
    int32_t (*load)(void* context, const char* profile, uint8_t* buffer, size_t capacity,
                    size_t* length);
    /* Store a copy of `length` bytes from `record`. Return SAP2_STORE_EXISTS,
     * without changing anything, if the profile already has a record. */
    int32_t (*save_new)(void* context, const char* profile, const uint8_t* record, size_t length);
    /* Delete the profile's record; SAP2_STORE_ABSENT if there is none. */
    int32_t (*erase)(void* context, const char* profile);
} sap2_credential_store;

#ifdef __cplusplus
}
#endif
#endif
