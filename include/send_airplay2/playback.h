/* SPDX-License-Identifier: Apache-2.0 */
#ifndef SEND_AIRPLAY2_PLAYBACK_H
#define SEND_AIRPLAY2_PLAYBACK_H
#include "send_airplay2/credentials.h"
#include "send_airplay2/export.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Experimental versioned C interface for casting one media representation to an
 * already paired AirPlay receiver. See docs/public-api.md for the design record.
 *
 * Scope: one URL playback session per handle, served by a library-owned HTTP
 * media server from host read callbacks, either progressively or as HLS built
 * by the library's remux (delivery option), with MRP pause/play/seek/stop, status
 * and an end reason. Credentials are loaded by profile name, from a host-provided
 * store (credentials.h) when one is given and otherwise from the library's
 * built-in platform store; built-in records never cross this interface.
 * Pairing and profile removal are in pairing.h.
 *
 * Conventions:
 * - No C++ exceptions or objects cross this boundary. Every function returns a
 *   SAP2_* result unless documented otherwise.
 * - Extensible structures begin with struct_size. Initialize options with
 *   sap2_cast_options_init_sized(&options, sizeof options) and set status/source
 *   struct_size to sizeof. Initializers never write beyond the size the caller
 *   passes, so a host built against an older header stays within its struct.
 * - Strings are NUL-terminated UTF-8/ASCII, borrowed for the call and copied.
 * - Receiver-provided text, addresses, identifiers, URLs and credentials are
 *   never returned through this interface.
 * - Receiver interoperability is recorded only for the configurations listed in
 *   docs/receiver-validation.md. A successful result is not a compatibility claim.
 */

/* Incremented whenever the interface changes while it is experimental.
 * 1: playback. 2: host credential stores, pairing, profile removal, and the
 *    PROFILE_EXISTS and PIN_TIMEOUT results. 3: the delivery option (HLS
 *    remux) and the MEDIA_UNSUPPORTED and MEDIA_MALFORMED results. Option
 *    structs of earlier versions (shorter struct_size) are still accepted.
 * 4: optional stereo AAC delivery, track probe, host fragment storage and
 *    conversion failure/status extensions. */
#define SAP2_PLAYBACK_API_VERSION 4u

/* Results. Fixed-width values for foreign-function bindings. */
#define SAP2_OK 0
#define SAP2_ERROR_INVALID_ARGUMENT 1  /* Null pointer, bad size, out-of-range option. */
#define SAP2_ERROR_INVALID_STATE 2     /* Call is not valid in the handle's current phase. */
#define SAP2_ERROR_CANCELLED 3         /* sap2_cast_stop() interrupted the operation. */
#define SAP2_ERROR_PROFILE_NOT_FOUND 4 /* No stored credentials under that profile; pair first. */
#define SAP2_ERROR_CREDENTIAL_STORE 5  /* Store unavailable or its record is malformed. */
#define SAP2_ERROR_UNSUPPORTED 6       /* No trusted credential store on this platform yet. */
#define SAP2_ERROR_AUTHENTICATION 7    /* Peer verification or record authentication failed. */
#define SAP2_ERROR_RECEIVER_REJECTED 8 /* Non-2xx response; see status.rejected_status. */
#define SAP2_ERROR_START_TIMEOUT 9     /* No confirmed playing state before the start deadline. */
#define SAP2_ERROR_CONNECTION 10       /* Network failure, timeout or peer disconnection. */
#define SAP2_ERROR_PROTOCOL 11         /* Malformed or unexpected receiver message. */
#define SAP2_ERROR_MEDIA_SERVER 12     /* Local media server setup failed. */
#define SAP2_ERROR_NOT_OWNED 13        /* The receiver is no longer playing our item. */
#define SAP2_ERROR_COMMAND_FAILED 14   /* The receiver rejected or did not answer a command. */
#define SAP2_ERROR_ENDED 15            /* The session already ended or is being stopped. */
#define SAP2_ERROR_OUT_OF_MEMORY 16
#define SAP2_ERROR_INTERNAL 17       /* Unexpected backend failure; details are not exposed. */
#define SAP2_ERROR_PROFILE_EXISTS 18 /* Pairing refused: the profile already has credentials. */
#define SAP2_ERROR_PIN_TIMEOUT 19    /* read_pin returned after pin_timeout_ms. */
/* Version 3, SAP2_DELIVERY_HLS_REMUX only: the media cannot be remuxed. */
#define SAP2_ERROR_MEDIA_UNSUPPORTED 20 /* Container, codec or size the remux does not take. */
#define SAP2_ERROR_MEDIA_MALFORMED 21   /* The container's structure is invalid. */
#define SAP2_ERROR_AUDIO_SELECTION_REQUIRED 22
#define SAP2_ERROR_AUDIO_TRACK_INVALID 23
#define SAP2_ERROR_MEDIA_CODEC 24
#define SAP2_ERROR_MEDIA_CACHE 25
#define SAP2_ERROR_MEDIA_SOURCE 26
#define SAP2_ERROR_MEDIA_LIMIT 27
#define SAP2_ERROR_MEDIA_GENERATION_TIMEOUT 28

/* Handle phases. ENDED means the session cleaned itself up after an end reason;
 * legacy delivery keeps HTTP until Stop/destroy. Stereo delivery also drains
 * HTTP/conversion/cache automatically; check media_cleaned_up separately. */
#define SAP2_PHASE_CREATED 0
#define SAP2_PHASE_STARTING 1
#define SAP2_PHASE_ACTIVE 2
#define SAP2_PHASE_ENDED 3
#define SAP2_PHASE_START_FAILED 4
#define SAP2_PHASE_STOPPED 5

/* Last URL playback state reported by the receiver's event channel. */
#define SAP2_STATE_NONE 0
#define SAP2_STATE_LOADING 1
#define SAP2_STATE_PLAYING 2
#define SAP2_STATE_PAUSED 3
#define SAP2_STATE_IDLE 4
#define SAP2_STATE_STOPPED 5
#define SAP2_STATE_ENDED 6
#define SAP2_STATE_OTHER 7

/* First terminal reason. A receiver-side Stop/Home has been observed to arrive
 * as CONNECTION_LOST; the library does not infer receiver intent from closure. */
#define SAP2_END_NONE 0
#define SAP2_END_SENDER_STOP 1
#define SAP2_END_MEDIA_END 2
#define SAP2_END_RECEIVER_STOP 3
#define SAP2_END_OWNERSHIP_LOST 4
#define SAP2_END_CONNECTION_LOST 5
#define SAP2_END_MEDIA_FAILURE                                                                     \
    6 /* Conversion failed before another terminal reason was observed. */

/* Local conversion categories, independent of receiver channel failures. */
#define SAP2_MEDIA_FAILURE_NONE 0u
#define SAP2_MEDIA_FAILURE_UNSUPPORTED 1u
#define SAP2_MEDIA_FAILURE_MALFORMED 2u
#define SAP2_MEDIA_FAILURE_SOURCE 3u
#define SAP2_MEDIA_FAILURE_CODEC 4u
#define SAP2_MEDIA_FAILURE_LIMIT 5u
#define SAP2_MEDIA_FAILURE_CACHE 6u
#define SAP2_MEDIA_FAILURE_TIMEOUT 7u
#define SAP2_AUTHORING_AUDIO_ESTIMATED 1u
#define SAP2_AUTHORING_COPIED_SIZES_ESTIMATED 2u
#define SAP2_AUTHORING_SUBTITLE_SIZES_UNMEASURED 4u

/* First observed failure: local channel and fixed category, no peer text. */
#define SAP2_FAILURE_CHANNEL_NONE 0
#define SAP2_FAILURE_CHANNEL_URL_EVENTS 1
#define SAP2_FAILURE_CHANNEL_REMOTE_EVENTS 2
#define SAP2_FAILURE_CHANNEL_URL_FEEDBACK 3
#define SAP2_FAILURE_CHANNEL_REMOTE_FEEDBACK 4
#define SAP2_FAILURE_CHANNEL_TIMING 5
#define SAP2_FAILURE_CHANNEL_MRP 6
#define SAP2_FAILURE_CHANNEL_SUPERVISOR 7

#define SAP2_FAILURE_REASON_NONE 0
#define SAP2_FAILURE_REASON_TIMEOUT 1
#define SAP2_FAILURE_REASON_DISCONNECTED 2
#define SAP2_FAILURE_REASON_NETWORK 3
#define SAP2_FAILURE_REASON_INVALID_MESSAGE 4
#define SAP2_FAILURE_REASON_AUTHENTICATION 5
#define SAP2_FAILURE_REASON_CANCELLED 6
#define SAP2_FAILURE_REASON_REJECTED 7
#define SAP2_FAILURE_REASON_OTHER 8

/* Receiver transport commands (MRP). STOP asks the receiver to stop; local
 * teardown is sap2_cast_stop(). SEEK takes an absolute position in seconds. */
#define SAP2_COMMAND_PLAY 1
#define SAP2_COMMAND_PAUSE 2
#define SAP2_COMMAND_STOP 3
#define SAP2_COMMAND_SEEK 4

/* Delivery (version 3). PROGRESSIVE serves the source as one representation
 * of content_type, for containers the receiver plays directly (MP4/MOV).
 * HLS_REMUX turns an MP4/MOV or MKV with H.264/HEVC video and AAC, AC-3 or
 * E-AC-3 audio into fragmented-MP4 HLS without transcoding; content_type is
 * then ignored. See docs/hls.md. */
#define SAP2_DELIVERY_PROGRESSIVE 0u
#define SAP2_DELIVERY_HLS_REMUX 1u
/* Version 4, opt-in build: copy video and convert selected audio to 48 kHz,
 * stereo AAC-LC. Requires a host-owned per-cast fragment store and quota.
 * Selection uses original source IDs before codec filtering; no fallback. */
#define SAP2_DELIVERY_STEREO_AAC_HLS 2u

/* Option bounds. */
#define SAP2_DEFAULT_RECEIVER_PORT 7000u
#define SAP2_DEFAULT_START_TIMEOUT_MS 30000u
#define SAP2_MAX_START_TIMEOUT_MS 120000u
/* Normal casting default validated on the recorded receiver (D36). */
#define SAP2_DEFAULT_MEDIA_CONNECTIONS 16u
#define SAP2_MAX_MEDIA_CONNECTIONS 16u

/** Runtime interface version; compare with SAP2_PLAYBACK_API_VERSION. */
SAP2_API uint32_t sap2_playback_api_version(void);
/** Nonzero only when this binary was built with SAP2_STEREO_AAC=ON. */
SAP2_API int sap2_stereo_aac_available(void);

/** Static fixed name for a result code ("ok", "invalid_argument", ...).
 * Unknown values return "unknown". Never NULL; never free it. */
SAP2_API const char* sap2_result_name(int32_t result);

/* Opaque per-read control, borrowed only for one read_at call. */
typedef struct sap2_read_control sap2_read_control;

/** Nonzero when the read should end now: the server is stopping, the request was
 * cancelled, or its absolute deadline passed. Poll during slow work. Returns
 * nonzero for NULL. Callable only inside the read_at/seal that received it. */
SAP2_API int sap2_read_should_stop(const sap2_read_control* control);

/** Host-provided immutable media representation, such as a brokered file.
 *
 * read_at copies up to `capacity` (at most 64 KiB) bytes starting at `offset`
 * into `buffer` and returns the count. Short reads are fine. Returning 0 before
 * the declared end fails that one HTTP request; the receiver may retry with a
 * new range in legacy delivery; stereo conversion source errors are terminal.
 * Remux/probe/preparation read on their caller, and conversion reads on its
 * worker. HTTP reads run on up to media_connections threads. All callbacks
 * must be thread-safe, must not retain `buffer` or `control`,
 * and must not call any sap2_cast function for the owning handle.
 *
 * Ownership: after sap2_cast_create() returns SAP2_OK the handle owns
 * `context`. `release` (optional) is called exactly once, after the last
 * read_at has returned, from sap2_cast_destroy(). On any other create result
 * ownership stays with the caller and release is not called. */
typedef struct sap2_media_source {
    uint32_t struct_size; /* sizeof(sap2_media_source) */
    void* context;
    uint64_t size; /* Representation size in bytes; files above 4 GiB are allowed. */
    size_t (*read_at)(void* context, uint64_t offset, uint8_t* buffer, size_t capacity,
                      const sap2_read_control* control);
    void (*release)(void* context);
} sap2_media_source;

/** Per-cast immutable fragment storage, with host-selected backing storage.
 * All callbacks must contain exceptions and must not call cast functions.
 * seal publishes the complete fragment atomically, returning SAP2_OK on success;
 * bytes (at most 1 MiB) and control are borrowed only during that call.
 * read_at supports concurrent positional short reads (at most 64 KiB); zero
 * before the fragment's end or a count above capacity is a terminal cache error.
 * Poll control during slow seal/read work. Retain neither buffers nor control.
 * clear is called exactly once after conversion workers and HTTP reads drain,
 * including failed start and destroy without start; remove only this cast's data.
 * Ownership transfers only on successful create. release, if provided, runs once
 * at destroy after clear and all callbacks. No callback runs on failed create.
 */
typedef struct sap2_aac_fragment_store {
    uint32_t struct_size;
    void* context;
    int32_t (*seal)(void* context, uint64_t index, const uint8_t* bytes, size_t length,
                    const sap2_read_control* control);
    size_t (*read_at)(void* context, uint64_t index, uint64_t offset, uint8_t* buffer,
                      size_t capacity, const sap2_read_control* control);
    void (*clear)(void* context);
    void (*release)(void* context);
} sap2_aac_fragment_store;

#define SAP2_TRACK_OTHER 0u
#define SAP2_TRACK_VIDEO 1u
#define SAP2_TRACK_AUDIO 2u
#define SAP2_CONTAINER_MP4 1u
#define SAP2_CONTAINER_MATROSKA 2u
/** Metadata only: IDs are original source track IDs, never filtered indexes.
 * Text is bounded file data, possibly truncated UTF-8; treat it as untrusted.
 * Probe support does not imply codec or receiver support. */
typedef struct sap2_media_track {
    uint32_t struct_size;
    uint32_t kind;
    uint64_t id;
    uint32_t enabled;
    uint32_t default_track;
    uint32_t encoded;
    uint32_t standard_timeline;
    char codec[64];
    char language[64];
    char name[256];
} sap2_media_track;
typedef struct sap2_track_list sap2_track_list;
/** Synchronous metadata probe, available without the codec backend. Borrows the
 * source, never calls release. Deadline is 1..120000 ms. Optional should_cancel
 * must be thread-safe, nonthrowing, and valid for this call; controls poll it too.
 * *tracks is NULL on failure. Free the successful list with sap2_track_list_free.
 */
SAP2_API int32_t sap2_media_probe(const sap2_media_source* source, uint32_t timeout_ms,
                                  void* context, int (*should_cancel)(void*),
                                  sap2_track_list** tracks);
SAP2_API size_t sap2_track_list_count(const sap2_track_list* tracks);
SAP2_API uint32_t sap2_track_list_container(const sap2_track_list* tracks);
SAP2_API int32_t sap2_track_list_get(const sap2_track_list* tracks, size_t index,
                                     sap2_media_track* track);
SAP2_API void sap2_track_list_free(sap2_track_list* tracks);

typedef struct sap2_cast_options {
    uint32_t struct_size;
    /* Numeric IPv4 or global/ULA IPv6 receiver address, no DNS or scope ID. */
    const char* receiver_address;
    uint16_t receiver_port; /* Default SAP2_DEFAULT_RECEIVER_PORT. */
    /* Paired credential profile: 1..64 lower-case letters/digits/._-, starting
     * with a letter or digit. It selects a local trust slot, not a receiver. */
    const char* profile;
    const char* content_type;   /* Plain type/subtype; NULL means "video/mp4". */
    uint32_t start_timeout_ms;  /* 1..SAP2_MAX_START_TIMEOUT_MS. */
    uint32_t media_connections; /* 1..SAP2_MAX_MEDIA_CONNECTIONS concurrent HTTP requests. */
    /* Finite and >= 0. Above 0, start seeks there once the receiver plays
     * (tvOS 26 ignores the queue item's start, D61), so the first second or so
     * may show the beginning; no MRP ownership within 5 s fails the start with
     * SAP2_ERROR_NOT_OWNED. */
    double start_position_seconds;
    /* Version 2. NULL selects the built-in platform store. The table is copied by
     * sap2_cast_create(); its callbacks and context must stay valid until
     * sap2_cast_destroy() returns (see credentials.h). */
    const sap2_credential_store* credential_store;
    /* Version 3. reserved fills what is tail padding of a version 2 struct on
     * 32-bit targets (8-byte alignment), so delivery starts past the full
     * version 2 size on every ABI; leave it zero, as the initializer does.
     * delivery is SAP2_DELIVERY_*; the initializer sets PROGRESSIVE, as do
     * structs of earlier versions. */
    uint32_t reserved;
    uint32_t delivery;
    /* Version 4: keep ID beyond the full v3 size, including x86 tail padding. */
    uint32_t reserved_v4;
    uint64_t audio_track_id;    /* 0 auto; otherwise an original enabled audio ID. */
    uint64_t cache_quota_bytes; /* Explicit finite quota, required for stereo delivery. */
    const sap2_aac_fragment_store* fragment_store;
} sap2_cast_options;

/** Fill defaults for a struct of `struct_size` bytes, the caller's sizeof.
 * Clears and fills at most min(struct_size, this library's sizeof) bytes and
 * stores that size in struct_size, so a host can read back which fields the
 * library knows; fields beyond it are left untouched and ignored. Strings and
 * the credential store are left NULL. No-op for NULL or a size smaller than the
 * version 1 struct. */
SAP2_API void sap2_cast_options_init_sized(sap2_cast_options* options, size_t struct_size);

/** Version 1 initializer, kept for hosts built against version 1. It writes only
 * the version 1 fields and sets struct_size to their size, so the library then
 * ignores credential_store; hosts that use a credential store must call
 * sap2_cast_options_init_sized(). No-op for NULL. */
SAP2_API void sap2_cast_options_init(sap2_cast_options* options);

/** Snapshot of one handle. Phase and start fields are read together; session
 * and MRP fields are each copied under their own lock, so they can differ by one
 * update. Receiver scalars are optional: check the has_* field before reading
 * the value. Position is estimated between receiver updates and is not visual
 * proof of playback. */
typedef struct sap2_cast_status {
    uint32_t struct_size;     /* Caller sets sizeof(sap2_cast_status). */
    uint32_t phase;           /* SAP2_PHASE_* */
    int32_t start_result;     /* SAP2_OK until a start finishes; then its result. */
    uint32_t rejected_status; /* HTTP status for SAP2_ERROR_RECEIVER_REJECTED, else 0. */
    uint32_t playback_state;  /* SAP2_STATE_* */
    uint32_t end_reason;      /* SAP2_END_* */
    uint32_t failure_channel; /* SAP2_FAILURE_CHANNEL_* */
    uint32_t failure_reason;  /* SAP2_FAILURE_REASON_* */
    uint32_t owned;           /* Nonzero while MRP reports our item as the active player. */
    uint32_t at_end;          /* Receiver-reported paused/stopped position at duration. */
    uint32_t cleaned_up;      /* Session workers joined and channel secrets erased. */
    uint32_t has_position;
    uint32_t has_duration;
    uint32_t has_playback_rate;
    double position_seconds;
    double duration_seconds;
    double playback_rate;
    /* Version 4 extension; written only if the complete extension fits. */
    uint32_t media_failure;    /* SAP2_MEDIA_FAILURE_* */
    uint32_t media_cleaned_up; /* Session, HTTP reads, conversion and cache drained. */
    uint32_t authoring_flags;  /* SAP2_AUTHORING_* estimates; not an accuracy gate. */
    uint32_t reserved_v4;
    uint64_t selected_audio_id; /* Original ID; 0 for no prepared audio. */
} sap2_cast_status;

typedef struct sap2_cast sap2_cast;

/** Validate options, copy them, and take ownership of `source` on success.
 * Performs no network, credential or file access. `*cast` is set to the new
 * handle on SAP2_OK and to NULL otherwise. */
SAP2_API int32_t sap2_cast_create(const sap2_cast_options* options, const sap2_media_source* source,
                                  sap2_cast** cast);

/** Load the profile's credentials (a host store's load runs once, on this
 * thread, before any network work), with SAP2_DELIVERY_HLS_REMUX build the HLS
 * presentation from the source (reading its index or block headers through
 * read_at on this thread; SAP2_ERROR_MEDIA_UNSUPPORTED or _MALFORMED if it
 * cannot be remuxed), start the media server, authenticate both receiver
 * sessions and start playback. The remux reads precede start_timeout_ms and
 * can be cancelled by sap2_cast_stop() like the rest of the start. Blocks until playback is
 * confirmed (one continuous second of forward playing within start_timeout_ms), failure, or
 * cancellation by sap2_cast_stop() from another thread. If the receiver
 * reports itself asleep, start first wakes it and waits up to 10 s for it to
 * settle; that wait is in addition to start_timeout_ms. There is no automatic
 * Play or seek retry. Valid once per handle, in SAP2_PHASE_CREATED; afterwards
 * the phase is ACTIVE or START_FAILED. Every failure has already torn down what
 * was started, including the media server. */
SAP2_API int32_t sap2_cast_start(sap2_cast* cast);

/** Copy the current snapshot. Valid in every phase and from any thread. */
SAP2_API int32_t sap2_cast_get_status(sap2_cast* cast, sap2_cast_status* status);

/** Block until playback_state differs from `previous_state`, the phase changes
 * from STARTING, the session ends or fails, or `timeout_ms` passes; then copy
 * the snapshot. A timeout is not an error. Returns immediately when nothing can
 * change (CREATED, START_FAILED or STOPPED). */
SAP2_API int32_t sap2_cast_wait_for_change(sap2_cast* cast, uint32_t previous_state,
                                           uint32_t timeout_ms, sap2_cast_status* status);

/** Send one correlated MRP command and wait for the receiver's answer. Valid in
 * SAP2_PHASE_ACTIVE; SAP2_ERROR_ENDED once the session ends or stop begins.
 * `position_seconds` is used only by SAP2_COMMAND_SEEK and must be finite and
 * >= 0. A rejection does not change local state. Concurrent sap2_cast_stop()
 * cancels a pending command. */
SAP2_API int32_t sap2_cast_command(sap2_cast* cast, uint32_t command, double position_seconds);

/** Local teardown: cancel a pending start or command, stop the session in its
 * fixed order, then stop the media server and join all its read callbacks.
 * Idempotent and thread-safe with other calls on the same handle; the first end
 * reason is kept (SAP2_END_SENDER_STOP if none). Must not be called from
 * read_at. After it returns no read_at runs. Returns SAP2_OK. */
SAP2_API int32_t sap2_cast_stop(sap2_cast* cast);

/** Stop if needed, release the media source and free the handle. NULL is a
 * no-op. Every other call on this handle must have returned, and none may follow. */
SAP2_API void sap2_cast_destroy(sap2_cast* cast);

#ifdef __cplusplus
}
#endif
#endif
