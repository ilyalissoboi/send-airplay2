// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_MRP_CHANNEL_H
#define SEND_AIRPLAY2_MRP_CHANNEL_H
#include "control_records.h"
#include "identity_crypto.h"
#include "receiver_stream.h"
#include <memory>
#include <optional>
namespace send_airplay2::detail {
namespace mrp_frame {
constexpr std::size_t header_size = 32;
constexpr std::size_t max_size = 262144;
constexpr std::size_t max_buffer = max_size + control_records::max_call_input;
} // namespace mrp_frame
struct MrpFrame {
    bool sync = false;
    std::uint64_t sequence = 0;
    Bytes payload;
};
/// Network-byte-order 32-byte header: size, 12-byte type, 4-byte command,
/// 64-bit sequence and 4-byte padding. Sender replies are empty; receiver
/// replies may contain plist bodies and are never acknowledged again.
[[nodiscard]] Bytes encode_mrp_frame(bool sync, std::uint64_t sequence, const Bytes& payload);
/// Partial input is retained; complete frames are removed once. Bounds are
/// checked as soon as the 4-byte advertised size arrives, before allocation.
[[nodiscard]] std::optional<MrpFrame> take_mrp_frame(Bytes& input);
[[nodiscard]] Bytes mrp_frame_payload(const Bytes& protobuf_message);
[[nodiscard]] Bytes mrp_frame_protobufs(const Bytes& payload);
/** Single-thread-owned encrypted stream, including both counters and buffers.
 * receive() returns null on its readiness deadline without discarding partial input,
 * so a worker can service outgoing commands while the receiver is idle.
 * Every other I/O/codec/authentication exception closes and erases the channel.
 * A sync is acknowledged before return using a separate bounded write deadline.
 * Never share this stream between a reader and writer thread.
 */
class MrpChannel {
public:
    MrpChannel(std::unique_ptr<ReceiverStream> stream, const Secret32& write_key,
               const Secret32& read_key);
    ~MrpChannel();
    MrpChannel(const MrpChannel&) = delete;
    MrpChannel& operator=(const MrpChannel&) = delete;
    MrpChannel(MrpChannel&&) = delete;
    MrpChannel& operator=(MrpChannel&&) = delete;
    void send(const Bytes& message, const ReceiverOperation& operation);
    [[nodiscard]] std::optional<MrpFrame> receive(const ReceiverOperation& read_operation,
                                                  const ReceiverOperation& reply_operation);
    void close() noexcept;

private:
    void write_frame(const Bytes& frame, const ReceiverOperation& operation);
    std::unique_ptr<ReceiverStream> stream_;
    std::unique_ptr<ControlWriter> writer_;
    std::unique_ptr<ControlReader> reader_;
    Bytes pending_;
    std::uint64_t sequence_ = 0;
    bool closed_ = false;
};
} // namespace send_airplay2::detail
#endif
