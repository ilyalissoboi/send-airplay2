// SPDX-License-Identifier: Apache-2.0
#include "event_channel.h"
#include "control_crypto.h"
#include <cstddef>
#include <memory>
#include <utility>

namespace send_airplay2::detail {
namespace {
/// Erases transient plaintext or wire bytes on every exit.
struct ErasedBytes {
    Bytes bytes;
    explicit ErasedBytes(Bytes input) : bytes(std::move(input)) {}
    ~ErasedBytes() {
        cleanse(bytes.data(), bytes.size());
    }
    ErasedBytes(const ErasedBytes&) = delete;
    ErasedBytes& operator=(const ErasedBytes&) = delete;
    ErasedBytes(ErasedBytes&&) = delete;
    ErasedBytes& operator=(ErasedBytes&&) = delete;
};
} // namespace

EventChannel::EventChannel(std::unique_ptr<ReceiverStream> stream, const Secret32& sender_write,
                           const Secret32& sender_read)
    : stream_(std::move(stream)) {
    try {
        if (!stream_ || &sender_write == &sender_read) {
            throw TransportException(TransportError::invalid_argument);
        }
        writer_ = std::make_unique<ControlWriter>(sender_write.bytes);
        reader_ = std::make_unique<ControlReader>(sender_read.bytes);
    } catch (...) {
        close();
        throw;
    }
}

EventChannel::~EventChannel() {
    close();
}

void EventChannel::close() noexcept {
    closed_ = true;
    if (stream_) {
        stream_->close();
    }
    writer_.reset();
    reader_.reset();
    parser_.close();
}

EventRequest EventChannel::receive(const ReceiverOperation& operation) {
    try {
        if (closed_) {
            throw TransportException(TransportError::closed);
        }
        while (true) {
            operation.check();
            if (auto request = parser_.next()) {
                send_reply(*request, operation);
                return std::move(*request);
            }
            read_more(operation);
        }
    } catch (...) {
        close();
        throw;
    }
}

void EventChannel::read_more(const ReceiverOperation& operation) {
    ErasedBytes input{Bytes(receiver_http::read_chunk)};
    const auto received = stream_->read_some(input.bytes.data(), input.bytes.size(), operation);
    if (received > input.bytes.size()) {
        throw TransportException(TransportError::network);
    }
    if (received == 0) {
        reader_->finish(); // A truncated record throws here.
        parser_.finish();  // A partial request throws `disconnected` here.
        throw TransportException(TransportError::disconnected);
    }
    input.bytes.resize(received);
    ErasedBytes plaintext{reader_->feed(input.bytes)};
    parser_.append(plaintext.bytes);
}

void EventChannel::send_reply(const EventRequest& request, const ReceiverOperation& operation) {
    ErasedBytes wire{writer_->encrypt(encode_event_response(request))};
    // Encrypted bytes are produced once; partial writes never re-encrypt.
    for (std::size_t offset = 0; offset < wire.bytes.size();) {
        operation.check();
        const auto remaining = wire.bytes.size() - offset;
        const auto written = stream_->write_some(wire.bytes.data() + offset, remaining, operation);
        if (written == 0 || written > remaining) {
            throw TransportException(TransportError::network);
        }
        offset += written;
    }
}
} // namespace send_airplay2::detail
