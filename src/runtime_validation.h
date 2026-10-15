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
/** Automatic-profile extension of the private report. A preparation refusal can
 * be the expected scenario; preparation_result distinguishes it from a completed
 * presentation. Copy callbacks are drained/dropped, not probed after Stop.
 */
struct sap2_runtime_automatic_report {
    struct sap2_runtime_report media;
    sap2_audio_decision decision;
    int32_t preparation_result;
    uint32_t post_stop_refused; ///< Converted retained callback refused after Stop.
    uint64_t preparation_read_calls, preparation_requested_bytes, preparation_actual_bytes;
    uint64_t request_read_calls, request_requested_bytes, request_actual_bytes;
    uint64_t cold_request_ms, warm_request_ms;
};
#ifdef __cplusplus
extern "C" {
#endif
SAP2_API int32_t sap2_runtime_validate(const sap2_media_source* source,
                                       const sap2_aac_fragment_store* store, uint64_t audio_id,
                                       uint32_t mode, struct sap2_runtime_report* report);
/** Automatic preparation with the same resource checks, taking ownership only
 * after valid arguments. Store context releases even if unused; only accepted
 * conversion clears it. Mode 6 requests preservation (conversion must refuse),
 * 7 cancels in the first source read, and 8 expects a quota admission refusal.
 * Modes 0..4 retain their meanings; cache failure mode 2 applies to conversion.
 * Modes 9/10/11 read only the first/middle/last media fragment and a byte-identical
 * warm reread, for bounded performance measurements instead of full-file decode.
 * Quota is explicit. No listener, credentials or receiver/controller is involved.
 */
SAP2_API int32_t sap2_runtime_validate_automatic(const sap2_media_source* source,
                                                 const sap2_aac_fragment_store* store,
                                                 uint64_t audio_id, uint64_t quota_bytes,
                                                 uint32_t mode,
                                                 struct sap2_runtime_automatic_report* report);
#ifdef __cplusplus
}
#endif
#endif
