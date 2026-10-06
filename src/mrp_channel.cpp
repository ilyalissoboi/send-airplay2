// SPDX-License-Identifier: Apache-2.0
#include "mrp_channel.h"
#include "binary_plist.h"
#include "control_crypto.h"
#include "protobuf_wire.h"
#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>
namespace send_airplay2::detail {
namespace {
constexpr std::size_t type_offset = 4, command_offset = 16, sequence_offset = 20,
                      padding_offset = 28;
void big_endian(Bytes& bytes, std::size_t offset, std::size_t count, std::uint64_t value) {
    for (std::size_t index = 0; index < count; ++index) {
        bytes[offset + count - 1 - index] = static_cast<std::uint8_t>(value >> (index * 8));
    }
}
std::uint64_t big_endian(const Bytes& bytes, std::size_t offset, std::size_t count) {
    std::uint64_t value = 0;
    for (std::size_t index = 0; index < count; ++index) {
        value = (value << 8) | bytes[offset + index];
    }
    return value;
}
[[noreturn]] void invalid() {
    throw TransportException(TransportError::invalid_message);
}
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
Bytes encode_mrp_frame(bool sync, std::uint64_t sequence, const Bytes& payload) {
    if (payload.size() > mrp_frame::max_size - mrp_frame::header_size ||
        (!sync && !payload.empty())) {
        invalid();
    }
    Bytes output(mrp_frame::header_size, 0);
    big_endian(output, 0, 4, mrp_frame::header_size + payload.size());
    const std::string type = sync ? "sync" : "rply";
    std::copy(type.begin(), type.end(), output.begin() + type_offset);
    if (sync) {
        const std::string command = "comm";
        std::copy(command.begin(), command.end(), output.begin() + command_offset);
    }
    big_endian(output, sequence_offset, 8, sequence);
    output.insert(output.end(), payload.begin(), payload.end());
    return output;
}
std::optional<MrpFrame> take_mrp_frame(Bytes& input) {
    if (input.size() > mrp_frame::max_buffer) {
        invalid();
    }
    if (input.size() < 4) {
        return {};
    }
    const auto size = big_endian(input, 0, 4);
    if (size < mrp_frame::header_size || size > mrp_frame::max_size) {
        invalid();
    }
    if (input.size() < mrp_frame::header_size) {
        return {};
    }
    const std::string type(input.begin() + type_offset, input.begin() + type_offset + 4);
    if (type != "sync" && type != "rply") {
        invalid();
    }
    if (!std::all_of(input.begin() + type_offset + 4, input.begin() + command_offset,
                     [](std::uint8_t byte) { return byte == 0; }) ||
        big_endian(input, padding_offset, 4) != 0) {
        invalid();
    }
    // Both sync and rply can carry plist payloads (the native receiver sends
    // a 74-byte rply). Command bytes are opaque. Only sync needs an answer.
    if (input.size() < size) {
        return {};
    }
    MrpFrame frame;
    frame.sync = type == "sync";
    frame.sequence = big_endian(input, sequence_offset, 8);
    frame.payload.assign(input.begin() + mrp_frame::header_size,
                         input.begin() + static_cast<std::ptrdiff_t>(size));
    cleanse(input.data(), static_cast<std::size_t>(size));
    input.erase(input.begin(), input.begin() + static_cast<std::ptrdiff_t>(size));
    return frame;
}
Bytes mrp_frame_payload(const Bytes& message) {
    Bytes batch;
    protobuf_wire::append_varint(batch, message.size());
    batch.insert(batch.end(), message.begin(), message.end());
    return encode_binary_plist(
        PlistDictionary{{"params", PlistDictionary{{"data", std::move(batch)}}}});
}
Bytes mrp_frame_protobufs(const Bytes& payload) {
    if (payload.empty()) {
        return {};
    }
    const auto root = decode_binary_plist(payload);
    const auto* params = root.find("params");
    const auto* data = params ? params->find("data") : nullptr;
    if (!data) {
        return {};
    } // E.g. ConfigureConnection transport information.
    if (data->kind() != PlistKind::data) {
        invalid();
    }
    return data->as_data();
}
MrpChannel::MrpChannel(std::unique_ptr<ReceiverStream> stream, const Secret32& write_key,
                       const Secret32& read_key)
    : stream_(std::move(stream)) {
    try {
        if (!stream_ || &write_key == &read_key) {
            throw TransportException(TransportError::invalid_argument);
        }
        writer_ = std::make_unique<ControlWriter>(write_key.bytes);
        reader_ =
            std::make_unique<ControlReader>(read_key.bytes, control_records::max_data_plaintext);
        std::array<std::uint8_t, 4> random{};
        public_random_bytes(random.data(), random.size());
        sequence_ = 0x100000000ULL;
        for (unsigned index = 0; index < 4; ++index) {
            sequence_ |= static_cast<std::uint64_t>(random[index]) << (index * 8);
        }
    } catch (...) {
        close();
        throw;
    }
}
MrpChannel::~MrpChannel() {
    close();
}
void MrpChannel::close() noexcept {
    closed_ = true;
    if (stream_) {
        stream_->close();
    }
    writer_.reset();
    reader_.reset();
    cleanse(pending_.data(), pending_.size());
    pending_.clear();
}
void MrpChannel::write_frame(const Bytes& frame, const ReceiverOperation& operation) {
    // The frame bound exceeds the HAP per-call bound; preserve counters across
    // chunks and never re-encrypt a partial socket write.
    for (std::size_t cursor = 0; cursor < frame.size();) {
        const auto count = std::min(control_records::max_call_input, frame.size() - cursor);
        ErasedBytes plain{Bytes(frame.begin() + static_cast<std::ptrdiff_t>(cursor),
                                frame.begin() + static_cast<std::ptrdiff_t>(cursor + count))};
        ErasedBytes wire{writer_->encrypt(plain.bytes)};
        for (std::size_t offset = 0; offset < wire.bytes.size();) {
            operation.check();
            const auto remaining = wire.bytes.size() - offset;
            const auto written =
                stream_->write_some(wire.bytes.data() + offset, remaining, operation);
            if (written == 0 || written > remaining) {
                throw TransportException(TransportError::network);
            }
            offset += written;
        }
        cursor += count;
    }
}
void MrpChannel::send(const Bytes& message, const ReceiverOperation& operation) {
    try {
        if (closed_) {
            throw TransportException(TransportError::closed);
        }
        ErasedBytes payload{mrp_frame_payload(message)};
        // The reference uses one random data-stream sequence for every sender
        // sync. This is distinct from HAP nonce counters, which still advance
        // for every record and must never be reused.
        ErasedBytes frame{encode_mrp_frame(true, sequence_, payload.bytes)};
        write_frame(frame.bytes, operation);
    } catch (...) {
        close();
        throw;
    }
}
std::optional<MrpFrame> MrpChannel::receive(const ReceiverOperation& read_operation,
                                            const ReceiverOperation& reply_operation) {
    try {
        if (closed_) {
            throw TransportException(TransportError::closed);
        }
        for (;;) {
            if (auto frame = take_mrp_frame(pending_)) {
                if (frame->sync) {
                    write_frame(encode_mrp_frame(false, frame->sequence, {}), reply_operation);
                }
                return frame;
            }
            ErasedBytes input{Bytes(receiver_http::read_chunk)};
            if (!stream_->wait_readable(read_operation)) {
                return {};
            }
            // Read/write deadlines are terminal. Only readiness polling can
            // expire harmlessly; a native read failure has already closed it.
            const auto received =
                stream_->read_some(input.bytes.data(), input.bytes.size(), reply_operation);
            if (received > input.bytes.size()) {
                throw TransportException(TransportError::network);
            }
            if (received == 0) {
                reader_->finish();
                throw TransportException(TransportError::disconnected);
            }
            input.bytes.resize(received);
            ErasedBytes plain{reader_->feed(input.bytes)};
            if (plain.bytes.size() > mrp_frame::max_buffer - pending_.size()) {
                invalid();
            }
            pending_.insert(pending_.end(), plain.bytes.begin(), plain.bytes.end());
        }
    } catch (...) {
        close();
        throw;
    }
}
} // namespace send_airplay2::detail
