/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SEND_AIRPLAY2_EXPORT_H
#define SEND_AIRPLAY2_EXPORT_H

/* Windows shared builds export from the library and import into consumers.
 * Other platforms and static builds use default symbol visibility. */
#if defined(_WIN32) && defined(SAP2_SHARED)
#if defined(SAP2_BUILDING)
#define SAP2_API __declspec(dllexport)
#else
#define SAP2_API __declspec(dllimport)
#endif
#else
#define SAP2_API
#endif

#endif
