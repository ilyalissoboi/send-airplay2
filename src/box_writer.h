// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_BOX_WRITER_H
#define SEND_AIRPLAY2_BOX_WRITER_H
#include "mp4_demux.h"
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <utility>

namespace send_airplay2::detail {
/// 16.16 fixed-point 1.0 (rates, matrix entries).
inline constexpr std::uint32_t fixed_point_one = 0x00010000;
/// 2.30 fixed-point 1.0 (the matrix's w entry).
inline constexpr std::uint32_t matrix_w_one = 0x40000000;
/// 8.8 fixed-point 1.0 (volume).
inline constexpr std::uint16_t full_volume = 0x0100;

/** Appends ISO BMFF boxes (big-endian) to a buffer. open() reserves the
 * 32-bit size field that close() fills once the box's content is written;
 * boxes nest by closing in reverse order. */
class BoxWriter {
public:
    /// `type` is a four-character box type.
    std::size_t open(const char* type) {
        const auto start = out_.size();
        u32(0);
        for (int index = 0; index < 4; ++index) {
            out_.push_back(static_cast<std::uint8_t>(type[index]));
        }
        return start;
    }
    std::size_t open_full(const char* type, std::uint8_t version, std::uint32_t flags) {
        const auto start = open(type);
        u32((static_cast<std::uint32_t>(version) << 24) | (flags & 0xffffff));
        return start;
    }
    /// @throws std::length_error for a box of 4 GiB or more.
    void close(std::size_t start) {
        const auto size = out_.size() - start;
        if (size > std::numeric_limits<std::uint32_t>::max()) {
            throw std::length_error("box larger than 4 GiB");
        }
        for (int index = 0; index < 4; ++index) {
            out_[start + static_cast<std::size_t>(index)] =
                static_cast<std::uint8_t>(size >> (24 - 8 * index));
        }
    }
    void u8(std::uint8_t value) {
        out_.push_back(value);
    }
    void u16(std::uint16_t value) {
        u8(static_cast<std::uint8_t>(value >> 8));
        u8(static_cast<std::uint8_t>(value));
    }
    void u32(std::uint32_t value) {
        u16(static_cast<std::uint16_t>(value >> 16));
        u16(static_cast<std::uint16_t>(value));
    }
    void u64(std::uint64_t value) {
        u32(static_cast<std::uint32_t>(value >> 32));
        u32(static_cast<std::uint32_t>(value));
    }
    void zeros(std::size_t count) {
        out_.insert(out_.end(), count, 0);
    }
    void bytes(const std::uint8_t* data, std::size_t count) {
        out_.insert(out_.end(), data, data + count);
    }
    void bytes(const Bytes& data) {
        bytes(data.data(), data.size());
    }
    /// A NUL-terminated string, terminator included.
    void text(const char* value) {
        for (; *value; ++value) {
            u8(static_cast<std::uint8_t>(*value));
        }
        u8(0);
    }
    /// The identity transformation matrix of mvhd and tkhd.
    void identity_matrix() {
        for (const auto value :
             {fixed_point_one, 0U, 0U, 0U, fixed_point_one, 0U, 0U, 0U, matrix_w_one}) {
            u32(value);
        }
    }
    [[nodiscard]] Bytes take() {
        return std::move(out_);
    }

private:
    Bytes out_;
};
} // namespace send_airplay2::detail
#endif
