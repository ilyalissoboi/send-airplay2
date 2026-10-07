// SPDX-License-Identifier: Apache-2.0
// Host credential store and pairing (D49) behind the C interface: the C table
// adapter, PIN callback handling, the pairing workflow with scripted sessions,
// profile removal, and a cast whose credentials come from a host store. Loopback
// and synthetic identities only; no receiver, platform store or real PIN.
#include "send_airplay2/pairing.h"
#include "cast_controller.h"
#include "credential_store.h"
#include "fake_receiver.h"
#include "host_credentials.h"
#include "pair_setup_crypto.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
using namespace send_airplay2;
using namespace send_airplay2::detail;
using namespace send_airplay2::detail::testing;
using namespace std::chrono_literals;

int failures = 0;
const char* group = "setup";
void check(bool passed, const std::string& scenario) {
    if (!passed) {
        std::cerr << "FAIL [" << group << "]: " << scenario << '\n';
        ++failures;
    }
}

constexpr const char* profile_name = "host-store-test";

/// One-slot in-memory host store behind a real sap2_credential_store table,
/// with scripted results. Synthetic test records only.
struct MemoryStore {
    std::optional<std::string> profile;
    std::vector<std::uint8_t> record;
    bool load_unavailable = false;
    bool save_unavailable = false;
    bool save_reports_exists = false; // Another writer won the race.
    int loads = 0, saves = 0, erases = 0;

    static int32_t load(void* context, const char* profile, std::uint8_t* buffer,
                        std::size_t capacity, std::size_t* length) {
        auto& store = *static_cast<MemoryStore*>(context);
        ++store.loads;
        if (store.load_unavailable) {
            return SAP2_STORE_UNAVAILABLE;
        }
        if (!store.profile || *store.profile != profile) {
            return SAP2_STORE_ABSENT;
        }
        if (store.record.size() > capacity) {
            return SAP2_STORE_UNAVAILABLE;
        }
        std::memcpy(buffer, store.record.data(), store.record.size());
        *length = store.record.size();
        return SAP2_STORE_OK;
    }
    static int32_t save_new(void* context, const char* profile, const std::uint8_t* record,
                            std::size_t length) {
        auto& store = *static_cast<MemoryStore*>(context);
        ++store.saves;
        if (store.save_unavailable) {
            return SAP2_STORE_UNAVAILABLE;
        }
        if (store.profile || store.save_reports_exists) {
            return SAP2_STORE_EXISTS;
        }
        store.profile = profile;
        store.record.assign(record, record + length);
        return SAP2_STORE_OK;
    }
    static int32_t erase(void* context, const char* profile) {
        auto& store = *static_cast<MemoryStore*>(context);
        ++store.erases;
        if (!store.profile || *store.profile != profile) {
            return SAP2_STORE_ABSENT;
        }
        store.profile.reset();
        store.record.clear();
        return SAP2_STORE_OK;
    }
    [[nodiscard]] sap2_credential_store table() {
        sap2_credential_store callbacks{};
        callbacks.struct_size = sizeof(callbacks);
        callbacks.context = this;
        callbacks.load = &MemoryStore::load;
        callbacks.save_new = &MemoryStore::save_new;
        callbacks.erase = &MemoryStore::erase;
        return callbacks;
    }
};

/// Synthetic credentials with a valid, canonical receiver key (the fake
/// receiver's), so the version-1 codec accepts them.
std::unique_ptr<PairCredentials> synthetic_credentials(const FakeReceiver& receiver) {
    return std::unique_ptr<PairCredentials>(new PairCredentials(receiver.credentials()));
}

bool same_record(const PairCredentials& left, const PairCredentials& right) {
    CredentialBlob a, b;
    encode_credentials(left, a);
    encode_credentials(right, b);
    return a.size == b.size && std::memcmp(a.bytes.data(), b.bytes.data(), a.size) == 0;
}

void adapter_tests() {
    group = "host store adapter";
    FakeReceiver receiver({});
    MemoryStore memory;
    const auto callbacks = memory.table();
    HostCredentialStore store(callbacks);
    check(store.load(profile_name) == nullptr, "absent profile loads as empty");
    const auto credentials = synthetic_credentials(receiver);
    store.save_new(profile_name, *credentials);
    check(memory.record.size() > 0 && memory.record.size() <= SAP2_CREDENTIAL_RECORD_MAX,
          "saved record fits the published bound (saved " + std::to_string(memory.record.size()) +
              " bytes)");
    const auto loaded = store.load(profile_name);
    check(loaded && same_record(*loaded, *credentials), "record round-trips through the host");
    try {
        store.save_new(profile_name, *credentials);
        check(false, "second save_new overwrote the record");
    } catch (const CredentialException& error) {
        check(error.reason() == CredentialError::already_exists, "EXISTS maps to already_exists");
    }
    check(store.erase(profile_name) && !store.erase(profile_name), "erase, then absent");

    memory.profile = profile_name;
    memory.record.assign(16, 0x5a);
    try {
        (void)store.load(profile_name);
        check(false, "malformed record decoded");
    } catch (const CredentialException& error) {
        check(error.reason() == CredentialError::invalid_record, "malformed record is rejected");
    }
    memory.load_unavailable = true;
    try {
        (void)store.load(profile_name);
        check(false, "unavailable store loaded");
    } catch (const CredentialException& error) {
        check(error.reason() == CredentialError::unavailable, "UNAVAILABLE maps to unavailable");
    }
}

/// Scripted pairing: begin, then finish succeeds only for PIN "0123".
struct PairingScript {
    const FakeReceiver* receiver = nullptr;
    int connections = 0, verifications = 0;
    std::string last_pin;
};
class ScriptedSession final : public AuthSession {
public:
    explicit ScriptedSession(PairingScript& script) : script_(script) {}
    void begin(Bytes, const ReceiverOperation& operation) override {
        operation.check();
    }
    std::unique_ptr<PairCredentials> finish(std::string_view pin,
                                            const ReceiverOperation& operation) override {
        operation.check();
        script_.last_pin = std::string(pin);
        if (pin != "0123") {
            // A wrong PIN fails the SRP proof at M4.
            throw PairSetupException(PairSetupError::authentication);
        }
        return synthetic_credentials(*script_.receiver);
    }
    void verify(const PairCredentials& credentials, const ReceiverOperation& operation) override {
        operation.check();
        check(same_record(credentials, *synthetic_credentials(*script_.receiver)),
              "verification uses the reloaded host record");
        ++script_.verifications;
    }

private:
    PairingScript& script_;
};
class ScriptedSessions final : public AuthSessionFactory {
public:
    explicit ScriptedSessions(PairingScript& script) : script_(script) {}
    std::unique_ptr<AuthSession> connect(const ReceiverEndpoint&,
                                         const ReceiverOperation& operation) override {
        operation.check();
        ++script_.connections;
        return std::make_unique<ScriptedSession>(script_);
    }

private:
    PairingScript& script_;
};

/// A read_pin callback with a scripted answer.
struct PinScript {
    std::string digits = "0123";
    int32_t result = SAP2_OK;
    std::chrono::milliseconds delay{0};
    int calls = 0;
    static int32_t read(void* context, char* digits, std::size_t capacity, std::size_t* length) {
        auto& script = *static_cast<PinScript*>(context);
        ++script.calls;
        std::this_thread::sleep_for(script.delay);
        const auto count = std::min(capacity, script.digits.size());
        std::memcpy(digits, script.digits.data(), count);
        *length = script.digits.size(); // May exceed capacity on purpose.
        return script.result;
    }
};

PairSettings pair_settings() {
    PairSettings settings;
    settings.endpoint = {"127.0.0.1", 7000, 0};
    settings.profile = profile_name;
    settings.timeout = 2s;
    settings.pin_timeout = 2s;
    return settings;
}

struct PairRun {
    CastResult result = CastResult::internal;
    PairingScript script;
};
PairRun pair_with(MemoryStore& memory, PinScript& pin, const FakeReceiver& receiver,
                  const PairSettings& settings = pair_settings()) {
    PairRun run;
    run.script.receiver = &receiver;
    const auto callbacks = memory.table();
    HostCredentialStore store(callbacks);
    ScriptedSessions sessions(run.script);
    CallbackPinPrompt prompt(&PinScript::read, &pin);
    run.result = pair_profile(settings, store, sessions, prompt);
    return run;
}

void pairing_tests() {
    group = "pairing";
    FakeReceiver receiver({});
    {
        MemoryStore memory;
        PinScript pin;
        const auto run = pair_with(memory, pin, receiver);
        check(run.result == CastResult::ok, "pairing succeeds with the right PIN");
        check(pin.calls == 1 && run.script.last_pin == "0123",
              "PIN callback runs once and leading zeros are kept");
        check(memory.saves == 1 && memory.profile == std::string(profile_name),
              "authenticated credentials are saved once in the host store");
        check(run.script.connections == 2 && run.script.verifications == 1,
              "a fresh connection verifies the reloaded record");
    }
    {
        MemoryStore memory;
        PinScript pin;
        pin.digits = "9999";
        const auto run = pair_with(memory, pin, receiver);
        check(run.result == CastResult::authentication && memory.saves == 0,
              "wrong PIN: authentication and nothing saved");
    }
    {
        MemoryStore memory;
        PinScript pin;
        pin.result = SAP2_ERROR_CANCELLED;
        const auto run = pair_with(memory, pin, receiver);
        check(run.result == CastResult::cancelled && memory.saves == 0,
              "cancelled PIN entry: cancelled and nothing saved");
    }
    for (const auto* digits : {"012", "012345678", "01a3"}) {
        MemoryStore memory;
        PinScript pin;
        pin.digits = digits;
        const auto run = pair_with(memory, pin, receiver);
        check(run.result == CastResult::invalid_argument && memory.saves == 0,
              std::string("invalid PIN '") + digits + "': invalid_argument and nothing saved");
    }
    {
        MemoryStore memory;
        PinScript pin;
        pin.delay = 200ms;
        auto settings = pair_settings();
        settings.pin_timeout = 50ms;
        const auto run = pair_with(memory, pin, receiver, settings);
        check(run.result == CastResult::pin_timeout && memory.saves == 0,
              "late PIN: pin_timeout and nothing saved");
    }
    {
        MemoryStore memory;
        const auto callbacks = memory.table();
        HostCredentialStore(callbacks).save_new(profile_name, *synthetic_credentials(receiver));
        PinScript pin;
        const auto run = pair_with(memory, pin, receiver);
        check(run.result == CastResult::profile_exists && pin.calls == 0 &&
                  run.script.connections == 0 && memory.saves == 1,
              "existing profile: profile_exists before any PIN or connection");
    }
    {
        MemoryStore memory;
        memory.save_reports_exists = true;
        PinScript pin;
        const auto run = pair_with(memory, pin, receiver);
        check(run.result == CastResult::profile_exists,
              "save_new losing a race to another writer: profile_exists");
    }
    {
        MemoryStore memory;
        memory.save_unavailable = true;
        PinScript pin;
        const auto run = pair_with(memory, pin, receiver);
        check(run.result == CastResult::credential_store && run.script.verifications == 0,
              "save failure: credential_store and no verification");
    }
}

void removal_tests() {
    group = "profile removal";
    FakeReceiver receiver({});
    MemoryStore memory;
    const auto callbacks = memory.table();
    HostCredentialStore store(callbacks);
    check(forget_profile(profile_name, store) == CastResult::profile_not_found, "absent profile");
    store.save_new(profile_name, *synthetic_credentials(receiver));
    check(forget_profile(profile_name, store) == CastResult::ok && !memory.profile,
          "stored profile is deleted");
    check(forget_profile("Not-Valid", store) == CastResult::invalid_argument && memory.erases == 2,
          "invalid profile is refused before calling the store");
}

MediaSource unused_source() {
    MediaSource source;
    source.size = [] { return std::uint64_t{10}; };
    source.read_at = [](std::uint64_t, std::uint8_t*, std::size_t, const MediaReadContext&) {
        return std::size_t{0};
    };
    return source;
}

void cast_tests() {
    group = "cast through a host store";
    FakeReceiver receiver({});
    MemoryStore memory;
    const auto callbacks = memory.table();
    auto store = std::make_shared<HostCredentialStore>(callbacks);
    store->save_new(profile_name, *synthetic_credentials(receiver));
    const int loads_before = memory.loads;

    CastSettings settings;
    settings.receiver_address = "127.0.0.1";
    settings.profile = profile_name;
    settings.start_timeout = 2000ms;
    settings.media_connections = 4;
    CastDependencies dependencies;
    dependencies.load_credentials = [store](std::string_view profile) {
        return store->load(profile);
    };
    dependencies.connect = receiver.connector();
    dependencies.adjust_session = [](UrlPlaybackOptions& options) {
        options.enable_mrp = false; // The default fake has no MRP data stream.
        options.start_confirmation_interval = 20ms;
        options.feedback_interval = 30ms;
    };
    CastController controller(settings, unused_source(), dependencies);
    check(memory.loads == loads_before, "creating a cast does not touch the store");
    check(controller.start() == CastResult::ok, "the host store's record authenticates the cast");
    check(memory.loads == loads_before + 1, "start loads the record exactly once");
    check(controller.snapshot().phase == CastPhase::active, "cast is active");
    controller.stop();
    check(controller.snapshot().session.end_reason == SessionEnd::sender_stop, "clean stop");
}
} // namespace

int main() {
    try {
        adapter_tests();
        pairing_tests();
        removal_tests();
        cast_tests();
    } catch (const std::exception& error) {
        std::cerr << "Unexpected test exception [" << group << "]: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " failure(s)\n";
        return 1;
    }
    std::cout << "host_credential_tests passed\n";
    return 0;
}
