// SPDX-License-Identifier: Apache-2.0
#ifndef SAP2_AUDIO_CODEC_SPIKE_H
#define SAP2_AUDIO_CODEC_SPIKE_H
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32)
#if defined(SAP2_CODEC_PROBE_BUILD)
#define SAP2_PROBE_API __declspec(dllexport)
#else
#define SAP2_PROBE_API __declspec(dllimport)
#endif
#else
#define SAP2_PROBE_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif

/* Development probe only: not the playback ABI or a production transcoder. */
#define SAP2_PROBE_OK 0
#define SAP2_PROBE_INVALID 1
#define SAP2_PROBE_CANCELLED 2
#define SAP2_PROBE_UNSUPPORTED 3
#define SAP2_PROBE_MALFORMED 4
#define SAP2_PROBE_CODEC_ERROR 5
#define SAP2_PROBE_OUTPUT_ERROR 6
#define SAP2_PROBE_BUILD_ERROR 7

/** Borrowed synchronous callbacks, used only before sap2_codec_probe returns.
 * No ownership transfer or callback after return. Callers serialize calls on
 * one context. Callbacks must not throw or reenter the probe. read_at copies
 * 1..capacity bytes (capacity <= 32768); short reads are supported, zero before
 * the declared end is an error. should_stop must return promptly; NULL means
 * no cancellation. write_packet consumes one raw AAC packet immediately and
 * returns nonzero on success; no borrowed pointer may be retained. A failed
 * call can have written partial output; the host must discard it.
 * Sources are bounded to 8 MiB and decoded audio to 60 seconds in this spike.
 */
typedef struct sap2_probe_source {
    uint32_t struct_size;
    void* context;
    uint64_t size;
    size_t (*read_at)(void* context, uint64_t offset, uint8_t* buffer, size_t capacity);
    int (*should_stop)(void* context);
    int (*write_packet)(void* context, const uint8_t* data, size_t size);
} sap2_probe_source;

/** Fixed non-secret metrics. PCM peaks/means are
 * before lossy encoding. With a correctly sized report it is zeroed on failure;
 * invalid pointer/size arguments are rejected without accessing report fields.
 * encoded_samples includes encoder-frame padding;
 * initial_padding is the AAC encoder delay, not a promise of gap-free HLS.
 * The packet output is a probe artifact, not a playable container.
 */
typedef struct sap2_probe_report {
    uint32_t struct_size;
    uint32_t input_channels;
    uint32_t input_sample_rate;
    uint32_t output_channels;
    uint32_t output_sample_rate;
    uint32_t initial_padding;
    uint64_t decoded_samples;
    uint64_t stereo_samples;
    uint64_t encoded_samples;
    uint64_t packet_count;
    uint64_t packet_bytes;
    uint64_t verified_aac_samples;
    double left_peak;
    double right_peak;
    double left_mean;
    double right_mean;
} sap2_probe_report;

/** Verify the narrow LGPL build, decode the sole audio stream through custom
 * AVIO, downmix/resample to 48 kHz stereo and encode native AAC-LC at 192 kbit/s.
 * Multiple audio streams and unknown channel layouts are refused. Errors and
 * exceptions never cross this C boundary. No files, URLs, network or credentials
 * are opened by the probe. Storage permission and cleanup belong to the host.
 */
SAP2_PROBE_API int32_t sap2_codec_probe(const sap2_probe_source* source, sap2_probe_report* report);
/** Nonzero only for the configured four-library LGPL profile and required codecs. */
SAP2_PROBE_API int sap2_codec_probe_build_ok(void);

#ifdef __cplusplus
}
#endif
#endif
