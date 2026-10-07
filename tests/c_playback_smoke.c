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
    check(SAP2_PLAYBACK_API_VERSION == 2, "credential stores and pairing are version 2");
    check(strcmp(sap2_result_name(SAP2_OK), "ok") == 0, "ok name");
    check(strcmp(sap2_result_name(SAP2_ERROR_CONNECTION), "connection") == 0, "connection name");
    check(strcmp(sap2_result_name(-1), "unknown") == 0, "unknown name");
    check(sap2_read_should_stop(NULL) != 0, "NULL read control stops");
}

static void option_defaults(void) {
    sap2_cast_options options;
    memset(&options, 0xA5, sizeof(options));
    sap2_cast_options_init(&options);
    check(options.struct_size == sizeof(options), "options struct size");
    check(options.receiver_port == 7000, "default receiver port");
    check(options.start_timeout_ms == 30000, "default start timeout");
    check(options.media_connections == 16, "default media connections");
    check(options.receiver_address == NULL && options.profile == NULL &&
              options.content_type == NULL,
          "strings default to NULL");
    check(options.start_position_seconds == 0, "default start position");
    sap2_cast_options_init(NULL);
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
