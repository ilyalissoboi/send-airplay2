// SPDX-License-Identifier: Apache-2.0
#include "protobuf_wire.h"
#include <cstring>
#include <limits>
#include <stdexcept>

namespace send_airplay2::detail::protobuf_wire {
namespace {
[[noreturn]] void invalid() {
    throw std::invalid_argument("Invalid bounded protobuf message");
}
void bound(const Bytes& output) {
    if (output.size() > max_message_size) {
        invalid();
    }
}
void tag(Bytes& output, std::uint32_t field, unsigned wire) {
    if (field == 0 || field > 0x1fffffff) {
        invalid();
    }
    append_varint(output, (static_cast<std::uint64_t>(field) << 3) | wire);
}
} // namespace
std::string_view view(const Bytes& bytes) noexcept {
    return {reinterpret_cast<const char*>(bytes.data()), bytes.size()};
}
std::uint64_t varint(std::string_view input, std::size_t& offset) {
    auto cursor = offset;
    std::uint64_t value = 0;
    for (unsigned index = 0; index < 10; ++index) {
        if (cursor >= input.size()) {
            invalid();
        }
        const auto byte = static_cast<std::uint8_t>(input[cursor++]);
        if (index == 9 && byte > 1) {
            invalid();
        }
        value |= static_cast<std::uint64_t>(byte & 0x7f) << (7 * index);
        if ((byte & 0x80) == 0) {
            offset = cursor;
            return value;
        }
    }
    invalid();
}
Fields decode(std::string_view input) {
    if (input.size() > max_message_size) {
        invalid();
    }
    Fields fields;
    for (std::size_t offset = 0; offset < input.size();) {
        if (fields.size() == max_fields) {
            invalid();
        }
        const auto key = varint(input, offset);
        if ((key >> 3) == 0 || (key >> 3) > 0x1fffffff) {
            invalid();
        }
        Field field;
        field.number = static_cast<std::uint32_t>(key >> 3);
        field.wire = static_cast<unsigned>(key & 7);
        if (field.wire == 0) {
            field.integer = varint(input, offset);
        } else {
            std::uint64_t length = 0;
            if (field.wire == 1) {
                length = 8;
            } else if (field.wire == 5) {
                length = 4;
            } else if (field.wire == 2) {
                length = varint(input, offset);
            } else {
                invalid();
            }
            if (length > input.size() - offset) {
                invalid();
            }
            field.data = input.substr(offset, static_cast<std::size_t>(length));
            offset += static_cast<std::size_t>(length);
        }
        fields.push_back(field);
    }
    return fields;
}
const Field* find(const Fields& fields, std::uint32_t number, unsigned wire) {
    const Field* result = nullptr;
    for (const auto& field : fields) {
        if (field.number != number) {
            continue;
        }
        if (result != nullptr || field.wire != wire) {
            invalid();
        }
        result = &field;
    }
    return result;
}
void append_varint(Bytes& output, std::uint64_t value) {
    do {
        auto byte = static_cast<std::uint8_t>(value & 0x7f);
        value >>= 7;
        if (value != 0) {
            byte |= 0x80;
        }
        output.push_back(byte);
    } while (value != 0);
    bound(output);
}
void integer(Bytes& output, std::uint32_t field, std::uint64_t value) {
    tag(output, field, 0);
    append_varint(output, value);
}
void data(Bytes& output, std::uint32_t field, std::string_view value) {
    if (value.size() > max_message_size || output.size() > max_message_size - value.size()) {
        invalid();
    }
    tag(output, field, 2);
    append_varint(output, value.size());
    output.insert(output.end(), value.begin(), value.end());
    bound(output);
}
void data(Bytes& output, std::uint32_t field, const Bytes& value) {
    data(output, field, view(value));
}
void real64(Bytes& output, std::uint32_t field, double value) {
    std::uint64_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&bits, &value, sizeof(bits));
    tag(output, field, 1);
    for (unsigned index = 0; index < 8; ++index) {
        output.push_back(static_cast<std::uint8_t>(bits >> (8 * index)));
    }
    bound(output);
}
double real64(const Field& field) {
    if (field.wire != 1 || field.data.size() != 8) {
        invalid();
    }
    std::uint64_t bits = 0;
    for (unsigned index = 0; index < 8; ++index) {
        bits |= static_cast<std::uint64_t>(static_cast<std::uint8_t>(field.data[index]))
                << (8 * index);
    }
    double value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
float real32(const Field& field) {
    if (field.wire != 5 || field.data.size() != 4) {
        invalid();
    }
    std::uint32_t bits = 0;
    for (unsigned index = 0; index < 4; ++index) {
        bits |= static_cast<std::uint32_t>(static_cast<std::uint8_t>(field.data[index]))
                << (8 * index);
    }
    float value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
} // namespace send_airplay2::detail::protobuf_wire
