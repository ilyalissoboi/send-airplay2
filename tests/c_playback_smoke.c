/* SPDX-License-Identifier: Apache-2.0 */
/* Compiles send_airplay2/playback.h as C and exercises the exported boundary:
 * versioning, defaults, argument validation, source ownership and the phases
 * reachable without a receiver. Only a deliberately absent credential profile
 * is read; nothing is written to any credential store and no receiver is used. */
#include "send_airplay2/playback.h"
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int failures = 0;
static void check(int passed, const char* scenario) {
    if (!passed) {
        fprintf(stderr, "FAIL: %s\n", scenario);
        ++failures;
    }
}

struct probe {
    int releases;
};
static size_t never_read(void* context, uint64_t offset, uint8_t* buffer, size_t capacity,
                         const sap2_read_control* control) {
    (void)context;
    (void)offset;
    (void)buffer;
    (void)capacity;
    (void)control;
    return 0;
}
static void count_release(void* context) {
    ((struct probe*)context)->releases++;
}

static sap2_media_source source_for(struct probe* probe) {
    sap2_media_source source;
    memset(&source, 0, sizeof(source));
    source.struct_size = sizeof(source);
    source.context = probe;
    source.size = 5000000000ull; /* Above 4 GiB: the size is carried as 64-bit. */
    source.read_at = never_read;
    source.release = count_release;
    return source;
}

/* sap2_cast_options as published in API version 2, for its sizeof. */
struct options_v2 {
    uint32_t struct_size;
    const char* receiver_address;
    uint16_t receiver_port;
    const char* profile;
    const char* content_type;
    uint32_t start_timeout_ms;
    uint32_t media_connections;
    double start_position_seconds;
    const sap2_credential_store* credential_store;
};

static sap2_cast_options valid_options(void) {
    sap2_cast_options options;
    sap2_cast_options_init(&options);
    options.receiver_address = "127.0.0.1";
    /* Absent by construction; a real profile would never use this name. */
    options.profile = "sap2-c-smoke-absent-profile";
    return options;
}

static int create_result(const sap2_cast_options* options) {
    struct probe probe = {0};
    const sap2_media_source source = source_for(&probe);
    sap2_cast* cast = (sap2_cast*)&probe; /* Must be reset to NULL on failure. */
    const int result = sap2_cast_create(options, &source, &cast);
    if (result != SAP2_OK) {
        check(cast == NULL, "failed create clears the output handle");
        check(probe.releases == 0, "failed create keeps source ownership with the caller");
    }
    sap2_cast_destroy(cast);
    return result;
}

static void version_and_names(void) {
    check(sap2_playback_api_version() == SAP2_PLAYBACK_API_VERSION, "runtime version");
    check(SAP2_PLAYBACK_API_VERSION == 3, "the delivery option is version 3");
    check(strcmp(sap2_result_name(SAP2_ERROR_MEDIA_UNSUPPORTED), "media_unsupported") == 0 &&
              strcmp(sap2_result_name(SAP2_ERROR_MEDIA_MALFORMED), "media_malformed") == 0,
          "remux result names");
    check(strcmp(sap2_result_name(SAP2_OK), "ok") == 0, "ok name");
    check(strcmp(sap2_result_name(SAP2_ERROR_CONNECTION), "connection") == 0, "connection name");
    check(strcmp(sap2_result_name(-1), "unknown") == 0, "unknown name");
    check(sap2_read_should_stop(NULL) != 0, "NULL read control stops");
}

/* Nonzero when every byte of `bytes` still holds the fill pattern. */
static int untouched(const unsigned char* bytes, size_t count) {
    size_t index;
    for (index = 0; index < count; ++index) {
        if (bytes[index] != 0xA5) {
            return 0;
        }
    }
    return 1;
}

static void check_defaults(const sap2_cast_options* options, const char* initializer) {
    char scenario[96];
    snprintf(scenario, sizeof(scenario), "%s: defaults", initializer);
    check(options->receiver_port == 7000 && options->start_timeout_ms == 30000 &&
              options->media_connections == 16 && options->start_position_seconds == 0 &&
              options->receiver_address == NULL && options->profile == NULL &&
              options->content_type == NULL,
          scenario);
}

static void option_defaults(void) {
    /* Version 1 fields end where credential_store begins (API version 2). */
    const size_t v1_size = offsetof(sap2_cast_options, credential_store);
    sap2_cast_options options;

    memset(&options, 0xA5, sizeof(options));
    sap2_cast_options_init_sized(&options, sizeof(options));
    check(options.struct_size == sizeof(options), "sized init: full struct size");
    check(options.credential_store == NULL, "sized init: no credential store");
    check(options.delivery == SAP2_DELIVERY_PROGRESSIVE, "sized init: progressive delivery");
    check_defaults(&options, "sized init");

    /* A host built against version 1 owns only v1_size bytes: the legacy
     * initializer must not write past them. */
    memset(&options, 0xA5, sizeof(options));
    sap2_cast_options_init(&options);
    check(options.struct_size == v1_size, "legacy init: version 1 struct size");
    check(untouched((const unsigned char*)&options + v1_size, sizeof(options) - v1_size),
          "legacy init writes nothing past the version 1 fields");
    check_defaults(&options, "legacy init");

    /* The same holds when a version 1 host's size is passed explicitly. */
    memset(&options, 0xA5, sizeof(options));
    sap2_cast_options_init_sized(&options, v1_size);
    check(options.struct_size == v1_size &&
              untouched((const unsigned char*)&options + v1_size, sizeof(options) - v1_size),
          "sized init with the version 1 size stays within it");

    memset(&options, 0xA5, sizeof(options));
    sap2_cast_options_init_sized(&options, v1_size - 1);
    check(untouched((const unsigned char*)&options, sizeof(options)),
          "sized init ignores a struct smaller than version 1");
    sap2_cast_options_init(NULL);
    sap2_cast_options_init_sized(NULL, sizeof(options));
}

static void argument_validation(void) {
    sap2_cast_options options = valid_options();
    struct probe probe = {0};
    sap2_media_source source = source_for(&probe);
    sap2_cast* cast = NULL;

    check(sap2_cast_create(&options, &source, NULL) == SAP2_ERROR_INVALID_ARGUMENT,
          "NULL output handle");
    check(sap2_cast_create(NULL, &source, &cast) == SAP2_ERROR_INVALID_ARGUMENT, "NULL options");
    check(sap2_cast_create(&options, NULL, &cast) == SAP2_ERROR_INVALID_ARGUMENT, "NULL source");
    source.read_at = NULL;
    check(sap2_cast_create(&options, &source, &cast) == SAP2_ERROR_INVALID_ARGUMENT,
          "NULL read callback");
    source = source_for(&probe);
    source.struct_size = sizeof(source) - 1;
    check(sap2_cast_create(&options, &source, &cast) == SAP2_ERROR_INVALID_ARGUMENT,
          "truncated source struct");
    check(probe.releases == 0, "rejected sources are not released");

    /* Version 1 options end before credential_store and stay accepted (API 2). */
    options.struct_size = (uint32_t)offsetof(sap2_cast_options, credential_store) - 1;
    check(create_result(&options) == SAP2_ERROR_INVALID_ARGUMENT,
          "options struct shorter than version 1");
    options.struct_size = (uint32_t)offsetof(sap2_cast_options, credential_store);
    check(create_result(&options) == SAP2_OK, "version 1 options struct is still accepted");
    /* valid_options() is a version 1 struct; delivery needs the full size. */
    options = valid_options();
    options.struct_size = (uint32_t)sizeof(options);
    options.credential_store = NULL;
    options.delivery = SAP2_DELIVERY_HLS_REMUX;
    check(create_result(&options) == SAP2_OK, "HLS remux delivery is accepted at create");
    options.delivery = 2;
    check(create_result(&options) == SAP2_ERROR_INVALID_ARGUMENT, "unknown delivery");
    /* A version 2 host passes sizeof its struct, tail padding included (4 bytes
     * on 32-bit targets); delivery lies beyond it and is never read. */
    options = valid_options();
    options.struct_size = (uint32_t)sizeof(struct options_v2);
    options.credential_store = NULL;
    options.delivery = 2;
    check(create_result(&options) == SAP2_OK, "version 2 options struct ignores delivery");
    check(offsetof(sap2_cast_options, delivery) >= sizeof(struct options_v2),
          "delivery starts past a whole version 2 struct");
    options = valid_options();
    options.receiver_address = NULL;
    check(create_result(&options) == SAP2_ERROR_INVALID_ARGUMENT, "NULL address");
    options = valid_options();
    options.receiver_address = "";
    check(create_result(&options) == SAP2_ERROR_INVALID_ARGUMENT, "empty address");
    options = valid_options();
    options.receiver_port = 0;
    check(create_result(&options) == SAP2_ERROR_INVALID_ARGUMENT, "zero port");
    options = valid_options();
    options.profile = "Upper-Case";
    check(create_result(&options) == SAP2_ERROR_INVALID_ARGUMENT, "invalid profile characters");
    options = valid_options();
    options.profile = "";
    check(create_result(&options) == SAP2_ERROR_INVALID_ARGUMENT, "empty profile");
    options = valid_options();
    options.start_timeout_ms = 0;
    check(create_result(&options) == SAP2_ERROR_INVALID_ARGUMENT, "zero start timeout");
    options.start_timeout_ms = SAP2_MAX_START_TIMEOUT_MS + 1;
    check(create_result(&options) == SAP2_ERROR_INVALID_ARGUMENT, "start timeout above bound");
    options = valid_options();
    options.media_connections = 0;
    check(create_result(&options) == SAP2_ERROR_INVALID_ARGUMENT, "zero media connections");
    options.media_connections = SAP2_MAX_MEDIA_CONNECTIONS + 1;
    check(create_result(&options) == SAP2_ERROR_INVALID_ARGUMENT, "media connections above bound");
    options = valid_options();
    options.start_position_seconds = -1;
    check(create_result(&options) == SAP2_ERROR_INVALID_ARGUMENT, "negative start position");
    options.start_position_seconds = NAN;
    check(create_result(&options) == SAP2_ERROR_INVALID_ARGUMENT, "NaN start position");
    options = valid_options();
    options.content_type = "";
    check(create_result(&options) == SAP2_ERROR_INVALID_ARGUMENT, "empty content type");
}

static void unstarted_lifecycle(void) {
    const sap2_cast_options options = valid_options();
    struct probe probe = {0};
    const sap2_media_source source = source_for(&probe);
    sap2_cast* cast = NULL;
    sap2_cast_status status;

    check(sap2_cast_create(&options, &source, &cast) == SAP2_OK && cast != NULL, "valid create");
    memset(&status, 0, sizeof(status));
    status.struct_size = sizeof(status);
    check(sap2_cast_get_status(cast, &status) == SAP2_OK, "status");
    check(status.phase == SAP2_PHASE_CREATED && status.start_result == SAP2_OK &&
              status.playback_state == SAP2_STATE_NONE && status.end_reason == SAP2_END_NONE,
          "created snapshot");
    status.struct_size = sizeof(status) - 1;
    check(sap2_cast_get_status(cast, &status) == SAP2_ERROR_INVALID_ARGUMENT,
          "truncated status struct");
    status.struct_size = sizeof(status);
    check(sap2_cast_wait_for_change(cast, 99, 0, &status) == SAP2_ERROR_INVALID_ARGUMENT,
          "unknown previous state");
    check(sap2_cast_wait_for_change(cast, SAP2_STATE_NONE, 5000, &status) == SAP2_OK &&
              status.phase == SAP2_PHASE_CREATED,
          "wait returns immediately before start");
    check(sap2_cast_command(cast, SAP2_COMMAND_PAUSE, 0) == SAP2_ERROR_INVALID_STATE,
          "command before start");
    check(sap2_cast_command(cast, 99, 0) == SAP2_ERROR_INVALID_ARGUMENT, "unknown command");
    check(probe.releases == 0, "handle owns the source");
    sap2_cast_destroy(cast);
    check(probe.releases == 1, "destroy releases the source exactly once");
}

/* The absent profile ends start before any listener or receiver connection. */
static void absent_profile_start(void) {
    const sap2_cast_options options = valid_options();
    struct probe probe = {0};
    const sap2_media_source source = source_for(&probe);
    sap2_cast* cast = NULL;
    sap2_cast_status status;
    int result;

    check(sap2_cast_create(&options, &source, &cast) == SAP2_OK, "create for start");
    result = sap2_cast_start(cast);
    /* Windows has a credential store; other platforms do not have one yet. */
    check(result == SAP2_ERROR_PROFILE_NOT_FOUND || result == SAP2_ERROR_UNSUPPORTED,
          "absent profile or unsupported store");
    memset(&status, 0, sizeof(status));
    status.struct_size = sizeof(status);
    check(sap2_cast_get_status(cast, &status) == SAP2_OK &&
              status.phase == SAP2_PHASE_START_FAILED && status.start_result == result,
          "failed start snapshot");
    check(sap2_cast_start(cast) == SAP2_ERROR_INVALID_STATE, "second start");
    check(sap2_cast_stop(cast) == SAP2_OK, "stop after failed start");
    check(sap2_cast_stop(cast) == SAP2_OK, "repeated stop");
    sap2_cast_destroy(cast);
    check(probe.releases == 1, "failed start releases the source once");
    sap2_cast_destroy(NULL);
    check(sap2_cast_stop(NULL) == SAP2_ERROR_INVALID_ARGUMENT, "NULL stop");
}

int main(void) {
    version_and_names();
    option_defaults();
    argument_validation();
    unstarted_lifecycle();
    absent_profile_start();
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("c_playback_smoke passed");
    return 0;
}
