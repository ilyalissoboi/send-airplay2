/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SEND_AIRPLAY2_HTTP_RANGE_H
#define SEND_AIRPLAY2_HTTP_RANGE_H
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(SAP2_SHARED)
# if defined(SAP2_BUILDING)
#  define SAP2_API __declspec(dllexport)
# else
#  define SAP2_API __declspec(dllimport)
# endif
#else
# define SAP2_API
#endif
#ifdef __cplusplus
extern "C" {
#endif

/* Fixed-width return values for foreign-function bindings. */
#define SAP2_RANGE_FULL 0
#define SAP2_RANGE_PARTIAL 1
#define SAP2_RANGE_UNSATISFIABLE 2
#define SAP2_RANGE_IGNORED 3
#define SAP2_RANGE_INVALID_ARGUMENT 4

/* Half-open interval [offset, offset + length). */
typedef struct sap2_byte_range {
    uint64_t offset;
    uint64_t length;
} sap2_byte_range;

/* Resolve a single HTTP Range field value for a representation of known size.
 * No allocation, I/O, retained pointers, or exceptions; safe for concurrent calls
 * with distinct output objects. The header is length-delimited, not NUL-terminated.
 * NULL with zero length means no header. Leading/trailing SP and HTAB are allowed.
 * FULL/IGNORED: serve 200 and the full representation. PARTIAL: serve 206.
 * UNSATISFIABLE: serve 416 with an unsatisfied Content-Range (see docs/design.md).
 * Unknown units, malformed syntax, overflow and multiple ranges are IGNORED;
 * this server deliberately does not implement multipart range responses.
 * INVALID_ARGUMENT: nonzero header length with NULL header, or NULL output.
 * With a valid output pointer, all non-partial results initialize the output;
 * UNSATISFIABLE and INVALID_ARGUMENT initialize it to {0, 0}.
 * Caller applies HTTP method, If-Range and conditional request semantics first.
 * This pre-1.0 API is experimental. */
SAP2_API int32_t sap2_resolve_http_range(const char* header, size_t header_length,
                                      uint64_t size, sap2_byte_range* output);
#ifdef __cplusplus
}
#endif
#endif
