/* SPDX-License-Identifier: Apache-2.0 */
/* Compiles send_airplay2/credentials.h and pairing.h as C and exercises the
 * host credential store and pairing boundary (D49) without a receiver: table
 * validation, load failures during cast start, profile removal, and pairing
 * refusals before any PIN is requested. The only built-in store access is one
 * removal of a deliberately absent profile; nothing is written to it. */
#include "send_airplay2/pairing.h"
#include "send_airplay2/playback.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;
static void check(int passed, const char* scenario) {
    if (!passed) {
        fprintf(stderr, "FAIL: %s\n", scenario);
        ++failures;
    }
}

/* One-slot in-memory store with scripted load results. Synthetic test data
 * only; a real host keeps records in its platform's secure storage. */
enum load_mode { load_normal, load_unavailable, load_zero_length, load_oversized };
struct memory_store {
    int has_record;
    char profile[65];
    uint8_t record[SAP2_CREDENTIAL_RECORD_MAX];
    size_t length;
    enum load_mode mode;
    int loads, saves, erases;
};

static int32_t store_load(void* context, const char* profile, uint8_t* buffer, size_t capacity,
                          size_t* length) {
    struct memory_store* store = (struct memory_store*)context;
    ++store->loads;
    if (store->mode == load_unavailable) {
        return SAP2_STORE_UNAVAILABLE;
    }
    if (store->mode == load_zero_length) {
        *length = 0;
        return SAP2_STORE_OK;
    }
    if (store->mode == load_oversized) {
        *length = capacity + 1;
        return SAP2_STORE_OK;
    }
    if (!store->has_record || strcmp(store->profile, profile) != 0) {
        return SAP2_STORE_ABSENT;
    }
    if (store->length > capacity) {
        return SAP2_STORE_UNAVAILABLE;
    }
    memcpy(buffer, store->record, store->length);
    *length = store->length;
    return SAP2_STORE_OK;
}
static int32_t store_save_new(void* context, const char* profile, const uint8_t* record,
                              size_t length) {
    struct memory_store* store = (struct memory_store*)context;
    ++store->saves;
    if (store->has_record) {
        return SAP2_STORE_EXISTS;
    }
    if (length > sizeof(store->record) || strlen(profile) >= sizeof(store->profile)) {
        return SAP2_STORE_UNAVAILABLE;
    }
    store->has_record = 1;
    strcpy(store->profile, profile);
    memcpy(store->record, record, length);
    store->length = length;
    return SAP2_STORE_OK;
}
static int32_t store_erase(void* context, const char* profile) {
    struct memory_store* store = (struct memory_store*)context;
    ++store->erases;
    if (!store->has_record || strcmp(store->profile, profile) != 0) {
        return SAP2_STORE_ABSENT;
    }
    memset(store, 0, sizeof(*store));
    store->erases = 1; /* Keep the count across the reset. */
    return SAP2_STORE_OK;
}

static sap2_credential_store table_for(struct memory_store* store) {
    sap2_credential_store table;
    memset(&table, 0, sizeof(table));
    table.struct_size = sizeof(table);
    table.context = store;
    table.load = store_load;
    table.save_new = store_save_new;
    table.erase = store_erase;
    return table;
}

/* Puts a non-empty placeholder record under `profile` (not a valid record). */
static void fill_placeholder(struct memory_store* store, const char* profile, size_t length) {
    store->has_record = 1;
    strcpy(store->profile, profile);
    memset(store->record, 0x5a, sizeof(store->record));
    store->length = length;
}

static size_t never_read(void* context, uint64_t offset, uint8_t* buffer, size_t capacity,
                         const sap2_read_control* control) {
    (void)context;
    (void)offset;
    (void)buffer;
    (void)capacity;
    (void)control;
    return 0;
}

static const char* profile_name = "c-store-smoke";

/* Creates a cast for the store and returns its start result. */
static int start_with_store(const sap2_credential_store* table) {
    sap2_cast_options options;
    sap2_media_source source;
    sap2_cast* cast = NULL;
    int result;
    sap2_cast_options_init(&options);
    options.receiver_address = "127.0.0.1";
    options.profile = profile_name;
    options.credential_store = table;
    memset(&source, 0, sizeof(source));
    source.struct_size = sizeof(source);
    source.size = 10;
    source.read_at = never_read;
    result = sap2_cast_create(&options, &source, &cast);
    if (result != SAP2_OK) {
        return 1000 + result; /* Distinguishes create failures from start results. */
    }
    result = sap2_cast_start(cast);
    sap2_cast_destroy(cast);
    return result;
}

static void table_validation(void) {
    struct memory_store store;
    sap2_credential_store table;
    memset(&store, 0, sizeof(store));
    table = table_for(&store);
    table.erase = NULL;
    check(start_with_store(&table) == 1000 + SAP2_ERROR_INVALID_ARGUMENT,
          "cast refuses a store table without erase");
    table = table_for(&store);
    table.struct_size = sizeof(table) - 1;
    check(start_with_store(&table) == 1000 + SAP2_ERROR_INVALID_ARGUMENT,
          "cast refuses a truncated store table");
    check(store.loads == 0, "rejected tables are never called");
}

static void cast_load_results(void) {
    struct memory_store store;
    sap2_credential_store table;
    memset(&store, 0, sizeof(store));
    table = table_for(&store);
    /* Same result on every platform: the host store replaces the built-in one. */
    check(start_with_store(&table) == SAP2_ERROR_PROFILE_NOT_FOUND,
          "empty host store reports profile_not_found");
    check(store.loads == 1 && store.saves == 0, "start loads once and never saves");

    memset(&store, 0, sizeof(store));
    store.mode = load_unavailable;
    check(start_with_store(&table) == SAP2_ERROR_CREDENTIAL_STORE,
          "unavailable host store reports credential_store");

    memset(&store, 0, sizeof(store));
    fill_placeholder(&store, profile_name, 16);
    check(start_with_store(&table) == SAP2_ERROR_CREDENTIAL_STORE,
          "malformed record reports credential_store");

    memset(&store, 0, sizeof(store));
    store.mode = load_zero_length;
    check(start_with_store(&table) == SAP2_ERROR_CREDENTIAL_STORE,
          "zero-length record reports credential_store");

    memset(&store, 0, sizeof(store));
    store.mode = load_oversized;
    check(start_with_store(&table) == SAP2_ERROR_CREDENTIAL_STORE,
          "length above capacity reports credential_store");
}

static void profile_removal(void) {
    struct memory_store store;
    sap2_credential_store table;
    int result;
    memset(&store, 0, sizeof(store));
    table = table_for(&store);
    check(sap2_forget_profile(profile_name, &table) == SAP2_ERROR_PROFILE_NOT_FOUND,
          "removing an absent profile");
    fill_placeholder(&store, profile_name, 16);
    check(sap2_forget_profile(profile_name, &table) == SAP2_OK && !store.has_record,
          "removing a stored profile deletes it");
    store.erases = 0;
    check(sap2_forget_profile("Not-Valid", &table) == SAP2_ERROR_INVALID_ARGUMENT &&
              store.erases == 0,
          "invalid profile is refused before calling the store");
    check(sap2_forget_profile(NULL, &table) == SAP2_ERROR_INVALID_ARGUMENT, "NULL profile");
    /* Built-in store: absent on Windows, unsupported elsewhere. */
    result = sap2_forget_profile("sap2-c-credentials-absent-profile", NULL);
    check(result == SAP2_ERROR_PROFILE_NOT_FOUND || result == SAP2_ERROR_UNSUPPORTED,
          "built-in store removal of an absent profile");
}

struct pin_probe {
    int calls;
};
static int32_t record_pin_call(void* context, char* digits, size_t capacity, size_t* length) {
    (void)capacity;
    ((struct pin_probe*)context)->calls++;
    memcpy(digits, "0123", 4);
    *length = 4;
    return SAP2_OK;
}

static sap2_pair_options pair_options_for(const sap2_credential_store* table,
                                          struct pin_probe* pin) {
    sap2_pair_options options;
    sap2_pair_options_init(&options);
    options.receiver_address = "127.0.0.1";
    options.receiver_port = 9; /* Discard port: nothing listens on loopback. */
    options.profile = profile_name;
    options.timeout_ms = 2000;
    options.credential_store = table;
    options.pin_context = pin;
    options.read_pin = record_pin_call;
    return options;
}

static void pair_defaults(void) {
    sap2_pair_options options;
    memset(&options, 0xA5, sizeof(options));
    sap2_pair_options_init(&options);
    check(options.struct_size == sizeof(options), "pair options struct size");
    check(options.receiver_port == 7000, "default pair port");
    check(options.timeout_ms == 10000 && options.pin_timeout_ms == 60000, "default pair timeouts");
    check(!options.receiver_address && !options.profile && !options.credential_store &&
              !options.read_pin,
          "pair pointers default to NULL");
    sap2_pair_options_init(NULL);
}

static void pair_refusals(void) {
    struct memory_store store;
    struct pin_probe pin = {0};
    sap2_credential_store table;
    sap2_pair_options options;
    memset(&store, 0, sizeof(store));
    table = table_for(&store);

    check(sap2_pair(NULL) == SAP2_ERROR_INVALID_ARGUMENT, "NULL pair options");
    options = pair_options_for(&table, &pin);
    options.read_pin = NULL;
    check(sap2_pair(&options) == SAP2_ERROR_INVALID_ARGUMENT, "pair requires read_pin");
    options = pair_options_for(&table, &pin);
    options.timeout_ms = 0;
    check(sap2_pair(&options) == SAP2_ERROR_INVALID_ARGUMENT, "zero pair timeout");
    options = pair_options_for(&table, &pin);
    options.pin_timeout_ms = SAP2_MAX_PIN_TIMEOUT_MS + 1;
    check(sap2_pair(&options) == SAP2_ERROR_INVALID_ARGUMENT, "PIN timeout above bound");
    options = pair_options_for(&table, &pin);
    options.receiver_address = "";
    check(sap2_pair(&options) == SAP2_ERROR_INVALID_ARGUMENT, "empty pair address");
    options = pair_options_for(&table, &pin);
    options.profile = "Not-Valid";
    check(sap2_pair(&options) == SAP2_ERROR_INVALID_ARGUMENT, "invalid pair profile");

    /* A malformed existing record fails the initial load. A valid one gives
     * SAP2_ERROR_PROFILE_EXISTS; that needs an encoded record and is covered by
     * host_credential_tests. Either way nothing reaches the receiver. */
    fill_placeholder(&store, profile_name, 16);
    options = pair_options_for(&table, &pin);
    check(sap2_pair(&options) == SAP2_ERROR_CREDENTIAL_STORE,
          "pairing stops on a malformed existing record");
    check(pin.calls == 0 && store.saves == 0, "existing record: no PIN request and no save");

    memset(&store, 0, sizeof(store));
    options = pair_options_for(&table, &pin);
    check(sap2_pair(&options) == SAP2_ERROR_CONNECTION, "unreachable receiver");
    check(pin.calls == 0 && store.saves == 0, "unreachable receiver: no PIN request and no save");
}

int main(void) {
    table_validation();
    cast_load_results();
    profile_removal();
    pair_defaults();
    pair_refusals();
    if (failures != 0) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("c_credentials_smoke passed");
    return 0;
}
