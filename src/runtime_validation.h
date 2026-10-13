// SPDX-License-Identifier: Apache-2.0
#ifndef SAP2_RUNTIME_VALIDATION_H
#define SAP2_RUNTIME_VALIDATION_H
#include "send_airplay2/playback.h"
#include <stdint.h>

/** Private development hook, absent unless SAP2_RUNTIME_VALIDATION is enabled.
 * Runs the production preparation/cache callback bridge without network or credentials.
 * Takes source/store ownership after argument validation, drains resources, clears and
 * releases both before returning. Callbacks must not throw or reenter. Mode 1 cancels
 * one request then retries; mode 2 expects host-injected cache failure; mode 3 injects a source
 * refusal after preparation; mode 4 expects invalid selection; mode 5 expects read-budget refusal
 * with 97-byte source reads. This does not test CastController or receiver EOF.
 */
struct sap2_runtime_report {
    uint32_t struct_size;
    uint32_t failure;
    uint64_t selected_audio_id;
    uint64_t preparation_ms;
    uint64_t request_ms;
    uint64_t resource_bytes;
    uint64_t audio_segments;
};
#ifdef __cplusplus
extern "C" {
#endif
SAP2_API int32_t sap2_runtime_validate(const sap2_media_source* source,
                                       const sap2_aac_fragment_store* store, uint64_t audio_id,
                                       uint32_t mode, struct sap2_runtime_report* report);
#ifdef __cplusplus
}
#endif
#endif
