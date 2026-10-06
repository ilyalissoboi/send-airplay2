// SPDX-License-Identifier: Apache-2.0
#include "auth_workflow.h"
#include "control_crypto.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#endif

using namespace send_airplay2::detail;
namespace {
void require(bool condition, const std::string& scenario) {
    if (!condition) {
        throw std::runtime_error(scenario);
    }
}
template <class Exception, class Action> void rejects(Action action, const std::string& scenario) {
    try {
        action();
    } catch (const Exception&) {
        return;
    }
    throw std::runtime_error("Expected rejection: " + scenario);
}
// RFC 8032 test vector 1's public key, independently published and prime-order.
constexpr PublicKey receiver_key{0xd7, 0x5a, 0x98, 0x01, 0x82, 0xb1, 0x0a, 0xb7, 0xd5, 0x4b, 0xfe,
                                 0xd3, 0xc9, 0x64, 0x07, 0x3a, 0x0e, 0xe1, 0x72, 0xf3, 0xda, 0xa6,
                                 0x23, 0x25, 0xaf, 0x02, 0x1a, 0x68, 0xf7, 0x07, 0x51, 0x1a};
std::unique_ptr<PairCredentials> fixture_credentials(std::size_t id_size = 1) {
    Secret32 seed;
    for (std::size_t index = 0; index < seed.bytes.size(); ++index) {
        seed.bytes[index] = static_cast<std::uint8_t>(index);
    }
    return std::make_unique<PairCredentials>(Bytes(id_size, 'r'), receiver_key, Bytes(id_size, 'c'),
                                             seed);
}
void same_credentials(const PairCredentials& actual, const PairCredentials& expected,
                      const char* scenario) {
    CredentialBlob left, right;
    encode_credentials(actual, left);
    encode_credentials(expected, right);
    require(left.size == right.size && left.bytes == right.bytes, scenario);
}
void codec_tests() {
    auto credentials = fixture_credentials();
    CredentialBlob encoded;
    encoded.bytes.fill(0xff);
    encode_credentials(*credentials, encoded);
    require(encoded.size == 77, "one-byte IDs yield literal 77-byte record");
    constexpr std::array<std::uint8_t, 11> header{'S', 'A', 'P', '2', 'C', 'R', 'E', 'D', 1, 1, 1};
    require(std::equal(header.begin(), header.end(), encoded.bytes.begin()), "literal v1 header");
    require(std::equal(receiver_key.begin(), receiver_key.end(), encoded.bytes.begin() + 11),
            "literal pinned key offset 11");
    for (std::size_t index = 0; index < 32; ++index) {
        require(encoded.bytes[43 + index] == index, "literal seed offset 43");
    }
    require(encoded.bytes[75] == 'r' && encoded.bytes[76] == 'c',
            "literal receiver/controller ID ordering");
    require(std::all_of(encoded.bytes.begin() + 77, encoded.bytes.end(),
                        [](auto byte) { return byte == 0; }),
            "overwrite erases old tail");
    auto decoded = decode_credentials(encoded);
    same_credentials(*decoded, *credentials, "independent credential ownership roundtrip");
    auto maximum = fixture_credentials(64);
    encode_credentials(*maximum, encoded);
    require(encoded.size == 203, "literal maximum envelope size");
    decoded = decode_credentials(encoded);
    same_credentials(*decoded, *maximum, "maximum ID roundtrip");
    for (std::size_t length = 0; length < 203; ++length) {
        encoded.size = length;
        rejects<CredentialException>([&] { (void)decode_credentials(encoded); },
                                     "truncation " + std::to_string(length));
    }
    encoded.size = 204;
    rejects<CredentialException>([&] { (void)decode_credentials(encoded); },
                                 "oversize before access");
    encode_credentials(*credentials, encoded);
    for (auto offset : {0U, 7U, 8U, 9U, 10U}) {
        const auto original = encoded.bytes[offset];
        encoded.bytes[offset] = 0xff;
        rejects<CredentialException>([&] { (void)decode_credentials(encoded); },
                                     "header mutation " + std::to_string(offset));
        encoded.bytes[offset] = original;
    }
    for (auto offset : {9U, 10U}) {
        encoded.bytes[offset] = 0;
        rejects<CredentialException>([&] { (void)decode_credentials(encoded); },
                                     "empty ID " + std::to_string(offset));
        encoded.bytes[offset] = 1;
    }
    ++encoded.size;
    rejects<CredentialException>([&] { (void)decode_credentials(encoded); }, "trailing byte");
    --encoded.size;
    std::fill_n(encoded.bytes.begin() + 11, 32, 0);
    rejects<CredentialException>([&] { (void)decode_credentials(encoded); },
                                 "weak pinned receiver key");
    Secret32 seed;
    auto invalid = std::make_unique<PairCredentials>(Bytes{'r'}, PublicKey{}, Bytes{'c'}, seed);
    encode_credentials(*credentials, encoded);
    const auto old_bytes = encoded.bytes;
    const auto old_size = encoded.size;
    rejects<CredentialException>([&] { encode_credentials(*invalid, encoded); }, "invalid encode");
    require(encoded.bytes == old_bytes && encoded.size == old_size,
            "failed encode preserves destination");
    for (const auto& profile : {"", "UPPER", "../x", "-x", "a/b", "a b", "a\\b"}) {
        rejects<CredentialException>([&] { validate_credential_profile(profile); },
                                     "invalid profile");
    }
    validate_credential_profile("living-room.4k_1");
    validate_credential_profile(std::string(64, 'a'));
    rejects<CredentialException>([] { validate_credential_profile(std::string(65, 'a')); },
                                 "profile length limit");
}
void argument_and_pin_tests() {
    const char* valid[] = {"pair",        "--address",  "fe80::1", "--profile",
                           "living-room", "--scope-id", "7"};
    const auto options = parse_auth_options(7, valid);
    require(options.endpoint.port == 7000 && options.endpoint.scope_id == 7 &&
                options.command == AuthCommand::pair,
            "explicit IPv6 endpoint");
    const std::vector<std::vector<const char*>> invalid{
        {"pair", "--profile", "p"},
        {"verify", "--address", "receiver.local", "--profile", "p"},
        {"pair", "--address", "127.0.0.1", "--profile", "p", "--pin", "0123"},
        {"forget", "--profile", "p", "--address", "127.0.0.1"},
        {"verify", "--address", "::1", "--profile", "p", "--port", "65536"},
        {"verify", "--address", "::1", "--profile", "p", "--timeout-ms", "0"},
        {"verify", "--address", "::1", "--profile", "p", "--pin-timeout-ms", "100"},
        {"pair", "--address", "127.0.0.1", "--profile", "p", "--scope-id", "7"},
        {"pair", "--address", "::1", "--profile", "p", "--profile", "p"},
        {"pair", "--address", "::1", "--profile", "p", "--port"}};
    for (std::size_t index = 0; index < invalid.size(); ++index) {
        rejects<std::invalid_argument>(
            [&] {
                (void)parse_auth_options(static_cast<int>(invalid[index].size()),
                                         invalid[index].data());
            },
            "argument scenario " + std::to_string(index));
    }
    PinCode pin;
    require(!pin.append('x') && !pin.complete(), "PIN accepts digits only");
    for (char digit : std::string_view("01234567")) {
        require(pin.append(digit), "PIN append");
    }
    require(pin.complete() && pin.view() == "01234567" && !pin.append('8'),
            "PIN preserves leading zero and rejects overflow");
    pin.backspace();
    require(pin.view() == "0123456", "PIN backspace");
    pin.clear();
    require(pin.view().empty() && !pin.complete(), "PIN clear");
}

enum class Failure { none, prepare, begin, finish, save, reload, verify, pin_cancel };
struct Scenario {
    Failure failure = Failure::none;
    unsigned connections = 0, active = 0, loads = 0, saves = 0, prompts = 0, verifications = 0;
    std::atomic_bool cancelled{false};
};
class MemoryStore final : public CredentialStore {
    Scenario& scenario_;
    std::unique_ptr<CredentialBlob> record_;

public:
    explicit MemoryStore(Scenario& scenario) : scenario_(scenario) {}
    bool contains() const {
        return static_cast<bool>(record_);
    }
    std::unique_ptr<PairCredentials> load(std::string_view) override {
        ++scenario_.loads;
        if (scenario_.failure == Failure::reload && scenario_.loads == 2) {
            throw CredentialException(CredentialError::unavailable);
        }
        return record_ ? decode_credentials(*record_) : nullptr;
    }
    void save_new(std::string_view, const PairCredentials& credentials) override {
        if (record_) {
            throw CredentialException(CredentialError::already_exists);
        }
        if (scenario_.failure == Failure::save) {
            throw CredentialException(CredentialError::unavailable);
        }
        auto record = std::make_unique<CredentialBlob>();
        encode_credentials(credentials, *record);
        record_ = std::move(record);
        ++scenario_.saves;
    }
    bool erase(std::string_view) override {
        const bool existed = contains();
        record_.reset();
        return existed;
    }
};
class FakePrompt final : public PinPrompt {
    Scenario& scenario_;

public:
    explicit FakePrompt(Scenario& scenario) : scenario_(scenario) {}
    void prepare() override {
        ++scenario_.prompts;
        if (scenario_.failure == Failure::prepare) {
            throw CredentialException(CredentialError::unavailable);
        }
    }
    void read(PinCode& output, const ReceiverOperation& operation) override {
        if (scenario_.failure == Failure::pin_cancel) {
            scenario_.cancelled.store(true);
            operation.check();
        }
        for (char digit : std::string_view("0123")) {
            require(output.append(digit), "fake PIN append");
        }
    }
};
class FakeSession final : public AuthSession {
    Scenario& scenario_;
    bool enrolled_ = false;

public:
    explicit FakeSession(Scenario& scenario) : scenario_(scenario) {
        ++scenario_.active;
    }
    ~FakeSession() override {
        --scenario_.active;
    }
    void begin(Bytes id, const ReceiverOperation& operation) override {
        operation.check();
        require(id.size() == 36 && id[14] == '4' && id[8] == '-' && id[23] == '-',
                "fresh controller ID shape");
        enrolled_ = true;
        if (scenario_.failure == Failure::begin) {
            throw std::runtime_error("synthetic begin failure");
        }
    }
    std::unique_ptr<PairCredentials> finish(std::string_view pin,
                                            const ReceiverOperation& operation) override {
        operation.check();
        require(enrolled_ && pin == "0123", "enrollment PIN preserves leading zero");
        if (scenario_.failure == Failure::finish) {
            throw std::runtime_error("synthetic authentication failure");
        }
        return fixture_credentials();
    }
    void verify(const PairCredentials& credentials, const ReceiverOperation& operation) override {
        operation.check();
        require(!enrolled_ && scenario_.active == 1,
                "verification uses a fresh connection after closing enrollment");
        same_credentials(credentials, *fixture_credentials(),
                         "verification uses reloaded credentials");
        ++scenario_.verifications;
        if (scenario_.failure == Failure::verify) {
            throw std::runtime_error("synthetic verify failure");
        }
    }
};
class FakeSessions final : public AuthSessionFactory {
    Scenario& scenario_;

public:
    explicit FakeSessions(Scenario& scenario) : scenario_(scenario) {}
    std::unique_ptr<AuthSession> connect(const ReceiverEndpoint& endpoint,
                                         const ReceiverOperation& operation) override {
        operation.check();
        require(endpoint.address == "127.0.0.1" && endpoint.port == 7000,
                "explicit workflow endpoint");
        ++scenario_.connections;
        return std::make_unique<FakeSession>(scenario_);
    }
};
AuthOptions pair_options() {
    AuthOptions options;
    options.command = AuthCommand::pair;
    options.profile = "synthetic";
    options.endpoint = {"127.0.0.1", 7000, 0};
    return options;
}
void workflow_tests() {
    for (auto failure : {Failure::none, Failure::prepare, Failure::begin, Failure::finish,
                         Failure::save, Failure::reload, Failure::verify, Failure::pin_cancel}) {
        Scenario scenario;
        scenario.failure = failure;
        MemoryStore store(scenario);
        FakePrompt prompt(scenario);
        FakeSessions sessions(scenario);
        std::ostringstream progress;
        auto options = pair_options();
        const auto run = [&] {
            run_auth_workflow(options, store, sessions, prompt, progress, &scenario.cancelled);
        };
        if (failure == Failure::none) {
            run();
        } else {
            rejects<std::exception>(run, "workflow failure " +
                                             std::to_string(static_cast<int>(failure)));
        }
        require(scenario.active == 0, "all workflow connections released on success/failure");
        const bool saved =
            failure == Failure::none || failure == Failure::reload || failure == Failure::verify;
        require(store.contains() == saved && scenario.saves == (saved ? 1U : 0U),
                "save only after authenticated enrollment; preserve committed credentials");
        require((progress.str().find("credentials saved") != std::string::npos) == saved,
                "saved checkpoint distinguishes reconnect failure");
        require(progress.str().find("0123") == std::string::npos, "progress contains no PIN");
        if (failure == Failure::prepare) {
            require(scenario.connections == 0, "hidden input refusal precedes receiver contact");
        }
        if (saved) {
            scenario.failure = Failure::none;
            const auto old_connections = scenario.connections;
            rejects<CredentialException>(run, "existing profile refusal");
            require(scenario.connections == old_connections,
                    "existing credential never triggers enrollment/replacement");
            options.command = AuthCommand::verify;
            run();
            require(scenario.connections == old_connections + 1,
                    "explicit retry uses one new verification connection");
            options.command = AuthCommand::forget;
            run();
            run();
            require(!store.contains() && scenario.connections == old_connections + 1,
                    "forget is idempotent and local only");
        }
    }
    Scenario scenario;
    MemoryStore store(scenario);
    FakePrompt prompt(scenario);
    FakeSessions sessions(scenario);
    std::ostringstream progress;
    auto options = pair_options();
    options.command = AuthCommand::verify;
    rejects<CredentialException>(
        [&] { run_auth_workflow(options, store, sessions, prompt, progress); },
        "missing profile cannot silently enroll");
    require(scenario.connections == 0 && scenario.prompts == 0,
            "missing profile fails before receiver/PIN prompt");
    scenario.cancelled.store(true);
    rejects<TransportException>(
        [&] {
            run_auth_workflow(pair_options(), store, sessions, prompt, progress,
                              &scenario.cancelled);
        },
        "cancel before workflow side effects");
    require(scenario.loads == 1, "pre-cancel does not read storage");
}

#ifdef _WIN32
// Tests use only random, synthetic namespace entries, never application credentials.
class TestSlot {
    CredentialStore& store_;

public:
    std::string profile;
    bool owned = false;
    explicit TestSlot(CredentialStore& store) : store_(store) {
        Secret32 random;
        generate_ed25519_seed(random);
        constexpr char hex[] = "0123456789abcdef";
        profile = "test-";
        for (std::size_t index = 0; index < 16; ++index) {
            profile += hex[random.bytes[index] >> 4];
            profile += hex[random.bytes[index] & 15];
        }
        require(!store_.load(profile), "synthetic slot must be absent before ownership");
    }
    ~TestSlot() {
        if (owned) {
            try {
                (void)store_.erase(profile);
            } catch (...) {
            }
        }
    }
    TestSlot(const TestSlot&) = delete;
    TestSlot& operator=(const TestSlot&) = delete;
    TestSlot(TestSlot&&) = delete;
    TestSlot& operator=(TestSlot&&) = delete;
};
void verify_in_child_process(const std::string& profile) {
    std::wstring path(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    require(size && size < path.size(), "child executable path");
    path.resize(size);
    auto command =
        L"\"" + path + L"\" --load-synthetic " + std::wstring(profile.begin(), profile.end());
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    require(CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                           nullptr, nullptr, &startup, &child) != 0,
            "launch independent credential reader");
    struct ChildHandles {
        PROCESS_INFORMATION& value;
        ~ChildHandles() {
            CloseHandle(value.hThread);
            CloseHandle(value.hProcess);
        }
        explicit ChildHandles(PROCESS_INFORMATION& process) : value(process) {}
        ChildHandles(const ChildHandles&) = delete;
        ChildHandles& operator=(const ChildHandles&) = delete;
        ChildHandles(ChildHandles&&) = delete;
        ChildHandles& operator=(ChildHandles&&) = delete;
    } handles(child);
    if (WaitForSingleObject(child.hProcess, 10000) != WAIT_OBJECT_0) {
        TerminateProcess(child.hProcess, 1);
        throw std::runtime_error("independent credential reader timed out");
    }
    DWORD exit_code = 1;
    require(GetExitCodeProcess(child.hProcess, &exit_code) && exit_code == 0,
            "credentials persist across independent process");
}
void native_store_tests() {
    auto store = native_credential_store(CredentialNamespace::synthetic_test);
    TestSlot slot(*store);
    auto credentials = fixture_credentials();
    store->save_new(slot.profile, *credentials);
    slot.owned = true;
    auto reloaded = store->load(slot.profile);
    require(static_cast<bool>(reloaded), "native load exists");
    same_credentials(*reloaded, *credentials, "native credential roundtrip");
    verify_in_child_process(slot.profile);
    rejects<CredentialException>([&] { store->save_new(slot.profile, *credentials); },
                                 "native overwrite refusal");
    require(store->erase(slot.profile) && !store->erase(slot.profile) && !store->load(slot.profile),
            "native idempotent erase");
    slot.owned = false;

    TestSlot race(*store);
    auto write = [&] {
        auto writer = native_credential_store(CredentialNamespace::synthetic_test);
        try {
            writer->save_new(race.profile, *credentials);
            return true;
        } catch (const CredentialException& error) {
            if (error.reason() != CredentialError::already_exists) {
                throw;
            }
            return false;
        }
    };
    // Claim cleanup for this known-absent synthetic slot even if a writer fails.
    race.owned = true;
    auto first = std::async(std::launch::async, write);
    auto second = std::async(std::launch::async, write);
    const bool first_won = first.get(), second_won = second.get();
    require(first_won != second_won, "cooperating writers have exactly one create winner");
    reloaded = store->load(race.profile);
    require(static_cast<bool>(reloaded), "winning native write exists");
    same_credentials(*reloaded, *credentials, "winning native write intact");

    TestSlot malformed(*store);
    auto target = L"send-airplay2/tests/v1/" +
                  std::wstring(malformed.profile.begin(), malformed.profile.end());
    unsigned char invalid[] = {0xff};
    wchar_t marker[] = L"send-airplay2-v1";
    CREDENTIALW record{};
    record.Type = CRED_TYPE_GENERIC;
    record.TargetName = target.data();
    record.UserName = marker;
    record.Persist = CRED_PERSIST_LOCAL_MACHINE;
    record.CredentialBlob = invalid;
    record.CredentialBlobSize = 1;
    require(CredWriteW(&record, 0) != 0, "write owned malformed synthetic slot");
    malformed.owned = true;
    rejects<CredentialException>([&] { (void)store->load(malformed.profile); },
                                 "native malformed record rejected");
    try {
        store->save_new(malformed.profile, *credentials);
        throw std::runtime_error("malformed slot overwritten");
    } catch (const CredentialException& error) {
        require(error.reason() == CredentialError::already_exists,
                "malformed slot remains occupied");
    }
}
#else
void native_store_tests() {
    try {
        (void)native_credential_store();
        throw std::runtime_error("unsupported platform accepted native storage");
    } catch (const CredentialException& error) {
        require(error.reason() == CredentialError::unsupported, "no plaintext fallback");
    }
}
#endif
} // namespace
int main(int argc, char** argv) {
    try {
#ifdef _WIN32
        if (argc == 3 && std::string_view(argv[1]) == "--load-synthetic") {
            auto store = native_credential_store(CredentialNamespace::synthetic_test);
            auto credentials = store->load(argv[2]);
            require(static_cast<bool>(credentials), "child synthetic load");
            same_credentials(*credentials, *fixture_credentials(),
                             "child independent credential roundtrip");
            return 0;
        }
#else
        (void)argc;
        (void)argv;
#endif
        codec_tests();
        argument_and_pin_tests();
        workflow_tests();
        native_store_tests();
        std::cout << "Credential codec, host workflow and native storage tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Credential tests: " << error.what() << '\n';
        return 1;
    }
}
