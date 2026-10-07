// SPDX-License-Identifier: Apache-2.0
#include "receiver_http.h"
#include "control_crypto.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace send_airplay2::detail {
namespace {
[[noreturn]] void invalid() {
    throw TransportException(TransportError::invalid_message);
}
bool token(std::string_view value) {
    if (value.empty()) {
        return false;
    }
    for (const unsigned char ch : value) {
        if (!((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') ||
              std::string_view("!#$%&'*+-.^_`|~").find(static_cast<char>(ch)) !=
                  std::string_view::npos)) {
            return false;
        }
    }
    return true;
}
bool field_value(std::string_view value) {
    return std::all_of(value.begin(), value.end(),
                       [](unsigned char ch) { return ch == '\t' || (ch >= 32 && ch <= 126); });
}
std::string lower(std::string_view value) {
    std::string output(value);
    for (auto& ch : output) {
        if (ch >= 'A' && ch <= 'Z') {
            ch += 'a' - 'A';
        }
    }
    return output;
}
std::string_view trim(std::string_view value) {
    while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
    }
    while (!value.empty() && (value.back() == ' ' || value.back() == '\t')) {
        value.remove_suffix(1);
    }
    return value;
}
std::uint32_t decimal(std::string_view value) {
    if (value.empty()) {
        invalid();
    }
    std::uint32_t result = 0;
    for (const char ch : value) {
        if (ch < '0' || ch > '9' ||
            result > (std::numeric_limits<std::uint32_t>::max() - static_cast<unsigned>(ch - '0')) /
                         10) {
            invalid();
        }
        result = result * 10 + static_cast<unsigned>(ch - '0');
    }
    return result;
}
const char* protocol_name(ReceiverProtocol protocol) {
    switch (protocol) {
    case ReceiverProtocol::http:
        return "HTTP/1.1";
    case ReceiverProtocol::rtsp:
        return "RTSP/1.0";
    }
    throw TransportException(TransportError::invalid_argument);
}
/// One "Name: value" field line: token name, visible value. Returns the
/// lower-cased name and the value without surrounding whitespace.
std::pair<std::string, std::string_view> parse_field_line(std::string_view line) {
    const auto colon = line.find(':');
    if (colon == std::string_view::npos || !token(line.substr(0, colon)) ||
        !field_value(line.substr(colon + 1))) {
        invalid();
    }
    return {lower(line.substr(0, colon)), trim(line.substr(colon + 1))};
}
void append_line(std::string& output, const std::string& line) {
    if (line.size() > receiver_http::max_line ||
        line.size() + 2 > receiver_http::max_headers - output.size()) {
        invalid();
    }
    output += line;
    output += "\r\n";
}
} // namespace

TransportException::TransportException(TransportError reason)
    : std::runtime_error("Receiver transport failed (category " +
                         std::to_string(static_cast<int>(reason)) + ")"),
      reason_(reason) {}

Bytes encode_receiver_request(const ReceiverRequest& request, const std::string& authority,
                              std::uint32_t sequence) {
    if (request.method.size() > 32 || !token(request.method) || request.method == "HEAD" ||
        request.method == "CONNECT" || request.target.empty() ||
        request.target.size() > receiver_http::max_line ||
        !std::all_of(request.target.begin(), request.target.end(),
                     [](unsigned char ch) { return ch > 32 && ch <= 126; }) ||
        authority.empty() || authority.size() > 256 ||
        !std::all_of(authority.begin(), authority.end(),
                     [](unsigned char ch) {
                         return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') ||
                                (ch >= 'A' && ch <= 'F') || ch == '.' || ch == ':' || ch == '[' ||
                                ch == ']';
                     }) ||
        !sequence || request.body.size() > receiver_http::max_body ||
        request.headers.size() > receiver_http::max_fields - 4) {
        invalid();
    }
    std::string header;
    header.reserve(receiver_http::max_headers);
    append_line(header,
                request.method + " " + request.target + " " + protocol_name(request.protocol));
    append_line(header, "Host: " + authority);
    append_line(header, "CSeq: " + std::to_string(sequence));
    append_line(header, "Content-Length: " + std::to_string(request.body.size()));
    append_line(header, "Connection: keep-alive");
    std::vector<std::string> names;
    for (const auto& field : request.headers) {
        if (field.first.size() > receiver_http::max_line ||
            field.second.size() > receiver_http::max_line ||
            field.first.size() + field.second.size() + 2 > receiver_http::max_line) {
            invalid();
        }
        const auto name = lower(field.first);
        if (!token(field.first) || !field_value(field.second) || name == "content-length" ||
            name == "transfer-encoding" || name == "cseq" || name == "host" ||
            name == "connection" || name == "upgrade" || name == "trailer" ||
            std::find(names.begin(), names.end(), name) != names.end()) {
            invalid();
        }
        names.push_back(name);
        append_line(header, field.first + ": " + field.second);
    }
    append_line(header, "");
    Bytes output;
    output.reserve(header.size() + request.body.size());
    output.insert(output.end(), header.begin(), header.end());
    output.insert(output.end(), request.body.begin(), request.body.end());
    return output;
}

ReceiverResponseParser::ReceiverResponseParser(ReceiverProtocol protocol, std::uint32_t sequence,
                                               std::size_t body_limit)
    : protocol_(protocol), sequence_(sequence), body_limit_(body_limit) {
    if (!sequence || body_limit > receiver_http::max_body) {
        throw TransportException(TransportError::invalid_argument);
    }
    (void)protocol_name(protocol);
    header_bytes_.reserve(receiver_http::max_headers);
}
ReceiverResponseParser::~ReceiverResponseParser() {
    close();
}
void ReceiverResponseParser::close() noexcept {
    cleanse(header_bytes_.data(), header_bytes_.size());
    cleanse(response_.body.data(), response_.body.size());
    header_bytes_.clear();
    response_.body.clear();
    closed_ = true;
    complete_ = false;
}
void ReceiverResponseParser::parse_headers() {
    const std::string_view header(reinterpret_cast<const char*>(header_bytes_.data()),
                                  header_bytes_.size());
    const auto first_end = header.find("\r\n");
    if (first_end == std::string_view::npos || first_end > receiver_http::max_line) {
        invalid();
    }
    const auto first = header.substr(0, first_end);
    const std::string prefix = std::string(protocol_name(protocol_)) + " ";
    if (first.size() < prefix.size() + 4 || first.substr(0, prefix.size()) != prefix ||
        first[prefix.size() + 3] != ' ' || !field_value(first)) {
        invalid();
    }
    response_.status = decimal(first.substr(prefix.size(), 3));
    if (response_.status < 200 || response_.status > 599 || response_.status == 304) {
        invalid();
    }
    bool has_length = false;
    bool has_sequence = false;
    std::size_t offset = first_end + 2;
    while (offset + 2 < header.size()) {
        const auto end = header.find("\r\n", offset);
        if (end == std::string_view::npos || end - offset > receiver_http::max_line ||
            response_.headers.size() == receiver_http::max_fields) {
            invalid();
        }
        // Named members, not a structured binding: C++17 lambdas cannot capture those.
        auto parsed = parse_field_line(header.substr(offset, end - offset));
        auto& name = parsed.first;
        const auto value = parsed.second;
        if (std::any_of(response_.headers.begin(), response_.headers.end(),
                        [&](const auto& field) { return field.first == name; })) {
            invalid();
        }
        if (name == "transfer-encoding" || name == "upgrade" || name == "trailer") {
            invalid();
        }
        if (name == "content-length") {
            body_length_ = decimal(value);
            has_length = true;
            if (body_length_ > body_limit_ || response_.status == 204) {
                invalid();
            }
        }
        if (name == "cseq") {
            if (decimal(value) != sequence_) {
                throw TransportException(TransportError::correlation);
            }
            has_sequence = true;
        }
        if (name == "connection" && lower(value) != "keep-alive") {
            invalid();
        }
        response_.headers.emplace_back(std::move(name), std::string(value));
        offset = end + 2;
    }
    if (!has_length && response_.status != 204) {
        invalid();
    }
    if (protocol_ == ReceiverProtocol::rtsp && !has_sequence) {
        throw TransportException(TransportError::correlation);
    }
    response_.body.reserve(body_length_);
    headers_done_ = true;
    complete_ = body_length_ == 0;
}
void ReceiverResponseParser::feed(const Bytes& bytes) {
    try {
        if (closed_) {
            throw TransportException(TransportError::closed);
        }
        if (bytes.size() > receiver_http::max_feed) {
            invalid();
        }
        for (const auto byte : bytes) {
            if (complete_) {
                throw TransportException(TransportError::correlation);
            }
            if (!headers_done_) {
                if (header_bytes_.size() == receiver_http::max_headers) {
                    invalid();
                }
                header_bytes_.push_back(byte);
                const auto size = header_bytes_.size();
                if (size >= 4 && header_bytes_[size - 4] == '\r' &&
                    header_bytes_[size - 3] == '\n' && header_bytes_[size - 2] == '\r' &&
                    header_bytes_[size - 1] == '\n') {
                    parse_headers();
                }
            } else {
                response_.body.push_back(byte);
                complete_ = response_.body.size() == body_length_;
            }
        }
    } catch (...) {
        close();
        throw;
    }
}
void ReceiverResponseParser::finish() {
    if (closed_) {
        throw TransportException(TransportError::closed);
    }
    if (!complete_) {
        close();
        throw TransportException(TransportError::disconnected);
    }
}
ReceiverResponse ReceiverResponseParser::take() {
    try {
        if (closed_) {
            throw TransportException(TransportError::closed);
        }
        if (!complete_) {
            throw TransportException(TransportError::invalid_message);
        }
        auto output = std::move(response_);
        close();
        return output;
    } catch (...) {
        close();
        throw;
    }
}
namespace {
constexpr std::string_view header_terminator = "\r\n\r\n";
constexpr std::size_t max_method_size = 32;

ReceiverProtocol parse_protocol(std::string_view name) {
    if (name == "RTSP/1.0") {
        return ReceiverProtocol::rtsp;
    }
    if (name == "HTTP/1.1") {
        return ReceiverProtocol::http;
    }
    invalid();
}

/// Parse "METHOD target PROTOCOL" and the field lines of one complete header
/// block (ending in an empty line) into `request`. Returns the body length.
std::size_t parse_event_header(std::string_view header, EventRequest& request) {
    const auto first_end = header.find("\r\n");
    if (first_end == std::string_view::npos || first_end > receiver_http::max_line) {
        invalid();
    }
    const auto first = header.substr(0, first_end);
    const auto method_end = first.find(' ');
    if (method_end == std::string_view::npos) {
        invalid();
    }
    const auto target_end = first.find(' ', method_end + 1);
    if (target_end == std::string_view::npos) {
        invalid();
    }
    const auto method = first.substr(0, method_end);
    const auto target = first.substr(method_end + 1, target_end - method_end - 1);
    if (method.size() > max_method_size || !token(method) || target.empty() ||
        !std::all_of(target.begin(), target.end(),
                     [](unsigned char ch) { return ch > 32 && ch <= 126; })) {
        invalid();
    }
    request.method = std::string(method);
    request.target = std::string(target);
    request.protocol = parse_protocol(first.substr(target_end + 1));
    std::size_t body_length = 0;
    // Each field line ends in CRLF; the block ends with one more CRLF.
    for (std::size_t offset = first_end + 2; offset + 2 < header.size();) {
        const auto end = header.find("\r\n", offset);
        if (end == std::string_view::npos || end - offset > receiver_http::max_line ||
            request.headers.size() == receiver_http::max_fields) {
            invalid();
        }
        // Named members, not a structured binding: C++17 lambdas cannot capture those.
        auto parsed = parse_field_line(header.substr(offset, end - offset));
        auto& name = parsed.first;
        const auto value = parsed.second;
        if (std::any_of(request.headers.begin(), request.headers.end(),
                        [&](const auto& field) { return field.first == name; }) ||
            name == "transfer-encoding" || name == "upgrade" || name == "trailer") {
            invalid();
        }
        if (name == "content-length") {
            body_length = decimal(value);
            if (body_length > receiver_http::max_body) {
                invalid();
            }
        }
        if (name == "cseq") {
            (void)decimal(value); // Echoed verbatim in the reply; must be a number.
        }
        request.headers.emplace_back(std::move(name), std::string(value));
        offset = end + 2;
    }
    return body_length;
}

const std::string* find_field(const ReceiverHeaders& headers, std::string_view name) {
    for (const auto& field : headers) {
        if (field.first == name) {
            return &field.second;
        }
    }
    return nullptr;
}

/// Reply fields echoed from the request, in the reference reply's order.
struct EchoedField {
    std::string_view name; // Lower-case, as stored by the parser.
    const char* display_name;
};
constexpr EchoedField echoed_fields[] = {{"server", "Server"}, {"cseq", "CSeq"}};
} // namespace

EventRequestParser::~EventRequestParser() {
    close();
}

void EventRequestParser::close() noexcept {
    cleanse(pending_.data(), pending_.size());
    pending_.clear();
    closed_ = true;
}

void EventRequestParser::append(const Bytes& plaintext) {
    try {
        if (closed_) {
            throw TransportException(TransportError::closed);
        }
        if (plaintext.size() > receiver_http::max_pending_event_input - pending_.size()) {
            invalid();
        }
        pending_.insert(pending_.end(), plaintext.begin(), plaintext.end());
    } catch (...) {
        close();
        throw;
    }
}

std::optional<EventRequest> EventRequestParser::next() {
    try {
        if (closed_) {
            throw TransportException(TransportError::closed);
        }
        const std::string_view input(reinterpret_cast<const char*>(pending_.data()),
                                     pending_.size());
        const auto terminator = input.find(header_terminator);
        if (terminator == std::string_view::npos) {
            if (pending_.size() >= receiver_http::max_headers) {
                invalid();
            }
            return std::nullopt;
        }
        const auto header_size = terminator + header_terminator.size();
        if (header_size > receiver_http::max_headers) {
            invalid();
        }
        EventRequest request;
        const auto body_size = parse_event_header(input.substr(0, header_size), request);
        if (pending_.size() - header_size < body_size) {
            return std::nullopt; // Headers are parsed again once the body is complete.
        }
        const auto body_begin = pending_.begin() + static_cast<std::ptrdiff_t>(header_size);
        request.body.assign(body_begin, body_begin + static_cast<std::ptrdiff_t>(body_size));
        const auto consumed = header_size + body_size;
        cleanse(pending_.data(), consumed);
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(consumed));
        return request;
    } catch (...) {
        close();
        throw;
    }
}

void EventRequestParser::finish() {
    if (closed_) {
        throw TransportException(TransportError::closed);
    }
    const bool partial = !pending_.empty();
    close();
    if (partial) {
        throw TransportException(TransportError::disconnected);
    }
}

Bytes encode_event_response(const EventRequest& request) {
    std::string header;
    append_line(header, std::string(protocol_name(request.protocol)) + " 200 OK");
    append_line(header, "Content-Length: 0");
    append_line(header, "Audio-Latency: 0");
    for (const auto& field : echoed_fields) {
        if (const auto* value = find_field(request.headers, field.name)) {
            if (!field_value(*value)) {
                invalid();
            }
            append_line(header, std::string(field.display_name) + ": " + *value);
        }
    }
    append_line(header, "");
    return Bytes(header.begin(), header.end());
}
} // namespace send_airplay2::detail
