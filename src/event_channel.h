// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_EVENT_CHANNEL_H
#define SEND_AIRPLAY2_EVENT_CHANNEL_H

#include "control_records.h"
#include "identity_crypto.h"
#include "receiver_http.h"
#include "receiver_stream.h"
#include <memory>

namespace send_airplay2::detail {
/**
 * The sender's side of a receiver session's event channel: a TCP connection to
 * the `eventPort` from the base SETUP, carrying HAP records keyed with
 * event_channel_labels(). The receiver sends requests on it (playback state,
 * notifications) and expects an answer to each one.
 *
 * receive() answers every well-formed request with encode_event_response()
 * before returning it, so the receiver is never left waiting while the caller
 * interprets the body. Bodies are returned undecoded.
 *
 * Owns the stream, both record directions and buffered input. Noncopyable,
 * nonmovable, serial use by one owning thread. To stop a blocked receive() from
 * another thread, set the operation's cancellation flag; the stream polls it.
 * Call close() only from the owning thread. Every exception is terminal: it
 * closes the stream and erases keys and buffered plaintext.
 */
class EventChannel {
public:
    /// Ownership of `stream` transfers even if construction throws. The keys are
    /// copied into the record owners; callers keep and erase their own copies.
    EventChannel(std::unique_ptr<ReceiverStream> stream, const Secret32& sender_write,
                 const Secret32& sender_read);
    ~EventChannel();
    EventChannel(const EventChannel&) = delete;
    EventChannel& operator=(const EventChannel&) = delete;
    EventChannel(EventChannel&&) = delete;
    EventChannel& operator=(EventChannel&&) = delete;

    /**
     * Block until the receiver's next complete request, answer it, and return
     * it. The caller owns and erases the body after return. A failed reply erases
     * the decoded body before unwinding. A clean or partial end of input
     * throws TransportException(disconnected); record authentication failures
     * throw ControlException; deadline and cancellation throw
     * TransportException(timeout or cancelled).
     */
    [[nodiscard]] EventRequest receive(const ReceiverOperation& operation);

    void close() noexcept;
    [[nodiscard]] bool closed() const noexcept {
        return closed_;
    }

private:
    void send_reply(const EventRequest& request, const ReceiverOperation& operation);
    /// Read once and feed decrypted bytes to the parser; end of input throws.
    void read_more(const ReceiverOperation& operation);

    std::unique_ptr<ReceiverStream> stream_;
    std::unique_ptr<ControlWriter> writer_;
    std::unique_ptr<ControlReader> reader_;
    EventRequestParser parser_;
    bool closed_ = false;
};
} // namespace send_airplay2::detail
#endif
