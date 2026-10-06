// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_RECEIVER_STREAM_H
#define SEND_AIRPLAY2_RECEIVER_STREAM_H
#include "receiver_http.h"
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace send_airplay2::detail {
/** Borrowed cancellation flag must outlive the operation. Only the flag can be
 * written from another thread; connection/socket methods are serial. Use one
 * absolute steady-clock deadline for connect or an entire request/response,
 * so small progress cannot extend the time budget. Native waits poll <=20 ms.
 */
struct ReceiverOperation {
    std::chrono::steady_clock::time_point deadline;
    const std::atomic_bool* cancelled = nullptr;
    [[nodiscard]] static ReceiverOperation after(std::chrono::milliseconds timeout,
                                                 const std::atomic_bool* cancelled = nullptr);
    void check() const;
};
struct ReceiverEndpoint {
    std::string address; // Numeric IPv4 or IPv6 only: no unbounded DNS resolver.
    std::uint16_t port = 0;
    std::uint32_t scope_id = 0; // IPv6 link-local interface; zero for IPv4.
    [[nodiscard]] std::string authority() const;
};

/** Private synchronous byte stream boundary, also used by fake receivers.
 * Each call checks the shared operation. read returns 0 only for EOF; writes
 * return positive progress or throw. Neither call exceeds supplied capacity.
 * require_idle rejects queued input/EOF before a new request or key transition.
 * close is idempotent. Instances own native resources, cannot be copied/moved,
 * and support serial use only; cancellation is through the borrowed atomic flag.
 */
class ReceiverStream {
public:
    ReceiverStream() = default;
    virtual ~ReceiverStream() = default;
    ReceiverStream(const ReceiverStream&) = delete;
    ReceiverStream& operator=(const ReceiverStream&) = delete;
    ReceiverStream(ReceiverStream&&) = delete;
    ReceiverStream& operator=(ReceiverStream&&) = delete;
    virtual std::size_t write_some(const std::uint8_t* data, std::size_t size,
                                   const ReceiverOperation& operation) = 0;
    virtual std::size_t read_some(std::uint8_t* data, std::size_t capacity,
                                  const ReceiverOperation& operation) = 0;
    virtual void require_idle(const ReceiverOperation& operation) = 0;
    virtual void close() noexcept = 0;
};
/// Connect a numeric TCP endpoint nonblockingly within one deadline; no retries/DNS.
/// Every failed construction closes its socket and balances the Winsock reference.
[[nodiscard]] std::unique_ptr<ReceiverStream> connect_receiver(const ReceiverEndpoint& endpoint,
                                                               const ReceiverOperation& operation);
} // namespace send_airplay2::detail
#endif
