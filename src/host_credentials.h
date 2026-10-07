// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_HOST_CREDENTIALS_H
#define SEND_AIRPLAY2_HOST_CREDENTIALS_H
#include "send_airplay2/credentials.h"
#include "auth_workflow.h"
#include "cast_controller.h"
#include "credential_store.h"
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>

namespace send_airplay2::detail {
/**
 * CredentialStore over a host's sap2_credential_store callback table (D49).
 *
 * The table is copied at construction; its callbacks and context must outlive
 * this object (see credentials.h). Records cross the boundary only through the
 * erasing CredentialBlob owner, so the library's copy is wiped after every call,
 * including absent and failed loads. Host results map to CredentialException:
 * ABSENT is an empty load (or false from erase), EXISTS is already_exists, a
 * zero or oversized length is invalid_record, and anything else is unavailable.
 * Serial use, like every CredentialStore; callbacks run on the calling thread.
 */
class HostCredentialStore final : public CredentialStore {
public:
    explicit HostCredentialStore(const sap2_credential_store& callbacks) noexcept;
    std::unique_ptr<PairCredentials> load(std::string_view profile) override;
    void save_new(std::string_view profile, const PairCredentials& credentials) override;
    bool erase(std::string_view profile) override;

private:
    sap2_credential_store callbacks_;
};

/// True when the table is large enough and has all three callbacks.
[[nodiscard]] bool valid_credential_store(const sap2_credential_store& callbacks) noexcept;

/// The host's store when one is given, otherwise the built-in platform store
/// (which throws CredentialException(unsupported) where none exists).
[[nodiscard]] std::unique_ptr<CredentialStore>
select_credential_store(const sap2_credential_store* host);

/// Host PIN entry: sap2_pair_options::read_pin and its context.
using PinReader = int32_t (*)(void* context, char* digits, std::size_t capacity,
                              std::size_t* length);

/**
 * PinPrompt over a host's read_pin callback. prepare() has nothing to set up:
 * the host owns its UI. read() calls the callback once into a fixed buffer that
 * is erased on every path, then copies 4..8 digits into the erasing PinCode.
 * A cancelled callback, a late return (after the PIN deadline) or invalid digits
 * end pairing before the receiver sees any PIN-derived proof.
 */
class CallbackPinPrompt final : public PinPrompt {
public:
    CallbackPinPrompt(PinReader read_pin, void* context) noexcept;
    void prepare() override {}
    void read(PinCode& output, const ReceiverOperation& operation) override;

private:
    PinReader read_pin_;
    void* context_;
};

struct PairSettings {
    ReceiverEndpoint endpoint;
    std::string profile;
    std::chrono::milliseconds timeout{10000};     // Each network phase.
    std::chrono::milliseconds pin_timeout{60000}; // Time allowed in the PIN callback.
};

/// Pair and save through run_auth_workflow (refuse an existing profile, enroll,
/// save, reload, verify a fresh connection). Never throws; failures are mapped.
[[nodiscard]] CastResult pair_profile(const PairSettings& settings, CredentialStore& store,
                                      AuthSessionFactory& sessions, PinPrompt& prompt) noexcept;

/// ok when a record was deleted, profile_not_found when there was none.
[[nodiscard]] CastResult forget_profile(std::string_view profile, CredentialStore& store) noexcept;
} // namespace send_airplay2::detail
#endif
