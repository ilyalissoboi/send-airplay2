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
 * media server from host read callbacks, with MRP pause/play/seek/stop, status
 * and an end reason. Credentials are loaded by profile name, from a host-provided
 * store (credentials.h) when one is given and otherwise from the library's
 * built-in platform store; built-in records never cross this interface.
 * Pairing and profile removal are in pairing.h.
 *
 * Conventions:
 * - No C++ exceptions or objects cross this boundary. Every function returns a
 *   SAP2_* result unless documented otherwise.
 * - Extensible structures begin with struct_size. Initialize options with
 *   sap2_cast_options_init() and set status/source struct_size to sizeof.
 * - Strings are NUL-terminated UTF-8/ASCII, borrowed for the call and copied.
 * - Receiver-provided text, addresses, identifiers, URLs and credentials are
 *   never returned through this interface.
 * - Receiver interoperability is recorded only for the configurations listed in
 *   docs/receiver-validation.md. A successful result is not a compatibility claim.
 */

/* Incremented whenever the interface changes while it is experimental.
 * 1: playback. 2: host credential stores, pairing, profile removal, and the
 *    PROFILE_EXISTS and PIN_TIMEOUT results. Version 1 option structs (shorter
 *    struct_size) are still accepted. */
#define SAP2_PLAYBACK_API_VERSION 2u

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

/* Handle phases. ENDED means the session cleaned itself up after an end reason;
 * the media server keeps its listener until sap2_cast_stop() or destroy. */
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

/* Option bounds. */
#define SAP2_DEFAULT_RECEIVER_PORT 7000u
#define SAP2_DEFAULT_START_TIMEOUT_MS 30000u
#define SAP2_MAX_START_TIMEOUT_MS 120000u
/* Normal casting default validated on the recorded receiver (D36). */
#define SAP2_DEFAULT_MEDIA_CONNECTIONS 16u
#define SAP2_MAX_MEDIA_CONNECTIONS 16u

/** Runtime interface version; compare with SAP2_PLAYBACK_API_VERSION. */
SAP2_API uint32_t sap2_playback_api_version(void);

/** Static fixed name for a result code ("ok", "invalid_argument", ...).
 * Unknown values return "unknown". Never NULL; never free it. */
SAP2_API const char* sap2_result_name(int32_t result);

/* Opaque per-read control, borrowed only for one read_at call. */
typedef struct sap2_read_control sap2_read_control;

/** Nonzero when the read should end now: the server is stopping, the request was
 * cancelled, or its absolute deadline passed. Poll during slow work. Returns
 * nonzero for NULL. Callable only from inside the read_at that received it. */
SAP2_API int sap2_read_should_stop(const sap2_read_control* control);

/** Host-provided immutable media representation, such as a brokered file.
 *
 * read_at copies up to `capacity` (at most 64 KiB) bytes starting at `offset`
 * into `buffer` and returns the count. Short reads are fine. Returning 0 before
 * the declared end fails that one HTTP request; the receiver may retry with a
 * new range. read_at runs concurrently on up to media_connections library
 * threads, so it must be thread-safe, must not retain `buffer` or `control`,
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

typedef struct sap2_cast_options {
    uint32_t struct_size;
    /* Numeric IPv4 or global/ULA IPv6 receiver address, no DNS or scope ID. */
    const char* receiver_address;
    uint16_t receiver_port; /* Default SAP2_DEFAULT_RECEIVER_PORT. */
    /* Paired credential profile: 1..64 lower-case letters/digits/._-, starting
     * with a letter or digit. It selects a local trust slot, not a receiver. */
    const char* profile;
    const char* content_type;      /* Plain type/subtype; NULL means "video/mp4". */
    uint32_t start_timeout_ms;     /* 1..SAP2_MAX_START_TIMEOUT_MS. */
    uint32_t media_connections;    /* 1..SAP2_MAX_MEDIA_CONNECTIONS concurrent HTTP requests. */
    double start_position_seconds; /* Finite and >= 0. */
    /* Version 2. NULL selects the built-in platform store. The table is copied by
     * sap2_cast_create(); its callbacks and context must stay valid until
     * sap2_cast_destroy() returns (see credentials.h). */
    const sap2_credential_store* credential_store;
} sap2_cast_options;

/** Fill defaults and struct_size. Addresses and profile are left NULL. No-op for NULL. */
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
} sap2_cast_status;

typedef struct sap2_cast sap2_cast;

/** Validate options, copy them, and take ownership of `source` on success.
 * Performs no network, credential or file access. `*cast` is set to the new
 * handle on SAP2_OK and to NULL otherwise. */
SAP2_API int32_t sap2_cast_create(const sap2_cast_options* options, const sap2_media_source* source,
                                  sap2_cast** cast);

/** Load the profile's credentials (a host store's load runs once, on this
 * thread, before any network work), start the media server, authenticate both
 * receiver sessions and start playback. Blocks until playback is confirmed (one
 * continuous second of forward playing within start_timeout_ms), failure, or
 * cancellation by sap2_cast_stop() from another thread. There is no automatic
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
