// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_RECEIVER_HTTP_H
#define SEND_AIRPLAY2_RECEIVER_HTTP_H
#include "pairing_tlv.h"
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace send_airplay2::detail {
namespace receiver_http {
constexpr std::size_t max_headers = 8192;
constexpr std::size_t max_line = 1024;
constexpr std::size_t max_fields = 32;
constexpr std::size_t max_body = 32768;
constexpr std::size_t read_chunk = 4096;
constexpr std::size_t max_feed = 65536;
} // namespace receiver_http

enum class TransportError {
    invalid_message,
    correlation,
    timeout,
    cancelled,
    disconnected,
    network,
    closed,
    invalid_argument
};
/// Error categories only; never include peer bytes, identities, PINs or native error queues.
class TransportException : public std::runtime_error {
public:
    explicit TransportException(TransportError reason);
    [[nodiscard]] TransportError reason() const noexcept {
        return reason_;
    }

private:
    TransportError reason_;
};

enum class ReceiverProtocol { http, rtsp };
using ReceiverHeaders = std::vector<std::pair<std::string, std::string>>;
struct ReceiverRequest {
    std::string method = "POST";
    std::string target;
    ReceiverProtocol protocol = ReceiverProtocol::http;
    ReceiverHeaders headers;
    Bytes body;
};
struct ReceiverResponse {
    unsigned status = 0;
    ReceiverHeaders headers; // Lower-case names; duplicate names rejected.
    Bytes body;              // Caller owns/erases returned bytes; do not log them.
};

/** Encode the narrow receiver request profile, with Content-Length and CSeq.
 * Extra headers cannot override framing, Host, Connection or CSeq. No HEAD,
 * CONNECT, chunked transfer, upgrades, pipelining or close-delimited bodies.
 * Host is a validated numeric endpoint authority from the connection adapter.
 * Bounds: 8 KiB headers, 1 KiB lines, 32 fields, 32 KiB body. Throws on injection,
 * unsupported framing or bounds; caller must erase the returned plaintext copy.
 */
[[nodiscard]] Bytes encode_receiver_request(const ReceiverRequest& request,
                                            const std::string& authority, std::uint32_t sequence);

/** Incremental parser for one correlated final response; serial, noncopyable.
 * HTTP uses the single outstanding request's ordering; CSeq, when present, must
 * match. RTSP requires matching CSeq. Reject informational responses, duplicate
 * fields, Transfer-Encoding and unsupported body framing. HTTP 204 has no body;
 * all other supported statuses require one Content-Length (including zero).
 * feed accepts <=64 KiB, buffering only bounded headers + the declared body.
 * Extra bytes after a response are unsolicited and terminal, even when coalesced.
 * EOF before completion or any method exception closes/wipes the parser.
 * take releases the response once; callers own its bytes. Destruction wipes input.
 */
class ReceiverResponseParser {
public:
    ReceiverResponseParser(ReceiverProtocol protocol, std::uint32_t sequence,
                           std::size_t body_limit);
    ~ReceiverResponseParser();
    ReceiverResponseParser(const ReceiverResponseParser&) = delete;
    ReceiverResponseParser& operator=(const ReceiverResponseParser&) = delete;
    ReceiverResponseParser(ReceiverResponseParser&&) = delete;
    ReceiverResponseParser& operator=(ReceiverResponseParser&&) = delete;
    void feed(const Bytes& bytes);
    void finish();
    [[nodiscard]] bool complete() const noexcept {
        return complete_;
    }
    [[nodiscard]] ReceiverResponse take();
    void close() noexcept;

private:
    void parse_headers();
    ReceiverProtocol protocol_;
    std::uint32_t sequence_;
    std::size_t body_limit_;
    Bytes header_bytes_;
    ReceiverResponse response_;
    std::size_t body_length_ = 0;
    bool headers_done_ = false;
    bool complete_ = false;
    bool closed_ = false;
};
} // namespace send_airplay2::detail
#endif
