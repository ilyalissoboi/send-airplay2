// SPDX-License-Identifier: Apache-2.0
// Temporary synthetic files and loopback HTTP only: no receiver or private media.
#include "file_media_source.h"
#include "send_airplay2/media_server.h"
#include <boost/asio.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace {
namespace asio = boost::asio;
namespace fs = std::filesystem;
using namespace send_airplay2;
using namespace send_airplay2::detail;

int failures = 0;
const char* group = "setup";
void check(bool passed, const std::string& scenario) {
    if (!passed) {
        std::cerr << "FAIL [" << group << "]: " << scenario << '\n';
        ++failures;
    }
}

/// Independent expected content: byte value depends only on its absolute offset.
std::uint8_t pattern(std::uint64_t offset) {
    return static_cast<std::uint8_t>(offset % 251);
}

constexpr std::uint64_t patterned_size = 200000; // Spans several 64-KiB server chunks.
constexpr std::size_t server_chunk = 65536;

/// Owns a uniquely named temporary file and removes it on scope exit.
class TemporaryFile {
public:
    TemporaryFile() {
        std::random_device random;
        path_ = fs::temp_directory_path() /
                ("sap2-file-source-" + std::to_string(random()) + std::to_string(random()));
    }
    ~TemporaryFile() {
        std::error_code ignored;
        fs::remove(path_, ignored);
    }
    TemporaryFile(const TemporaryFile&) = delete;
    TemporaryFile& operator=(const TemporaryFile&) = delete;
    TemporaryFile(TemporaryFile&&) = delete;
    TemporaryFile& operator=(TemporaryFile&&) = delete;
    [[nodiscard]] const fs::path& path() const noexcept {
        return path_;
    }
    void write_patterned(std::uint64_t size) const {
        std::ofstream output(path_, std::ios::binary | std::ios::trunc);
        for (std::uint64_t offset = 0; offset < size; ++offset) {
            output.put(static_cast<char>(pattern(offset)));
        }
        if (!output) {
            throw std::runtime_error("cannot write temporary fixture");
        }
    }

private:
    fs::path path_;
};

/// Context flags for direct callback calls; the far deadline never expires in a test.
struct Context {
    std::atomic_bool stopped{false};
    std::atomic_bool cancelled{false};
    [[nodiscard]] MediaReadContext get() const {
        return {&stopped, &cancelled, std::chrono::steady_clock::now() + std::chrono::minutes(5)};
    }
};

bool matches_pattern(const std::vector<std::uint8_t>& bytes, std::uint64_t offset,
                     std::size_t count) {
    for (std::size_t index = 0; index < count; ++index) {
        if (bytes[index] != pattern(offset + index)) {
            return false;
        }
    }
    return true;
}

void open_refusal_tests() {
    group = "open refusals";
    const TemporaryFile absent;
    bool refused = false;
    try {
        (void)open_file_media_source(absent.path(), std::make_shared<FileReadStats>());
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    check(refused, "absent path is refused as an argument error");
    refused = false;
    try {
        (void)open_file_media_source(fs::temp_directory_path(), std::make_shared<FileReadStats>());
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    check(refused, "directory is refused as an argument error");
    TemporaryFile file;
    file.write_patterned(1);
    refused = false;
    try {
        (void)open_file_media_source(file.path(), nullptr);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    check(refused, "missing statistics owner is refused");
}

void offset_and_statistics_tests() {
    group = "offsets, short reads and statistics";
    TemporaryFile file;
    file.write_patterned(patterned_size);
    auto stats = std::make_shared<FileReadStats>();
    auto source = open_file_media_source(file.path(), stats);
    check(source.size() == patterned_size, "size snapshot equals file length");
    Context context;
    std::vector<std::uint8_t> buffer(server_chunk);
    struct Case {
        std::uint64_t offset;
        std::size_t capacity;
        std::size_t expected;
    };
    // Literal boundaries: start, mid-file full chunk, and a read crossing EOF.
    const Case cases[] = {
        {0, 16, 16}, {server_chunk, server_chunk, server_chunk}, {patterned_size - 10, 64, 10}};
    for (const auto& test : cases) {
        const auto count = source.read_at(test.offset, buffer.data(), test.capacity, context.get());
        const auto scenario =
            "offset " + std::to_string(test.offset) + " capacity " + std::to_string(test.capacity);
        check(count == test.expected, scenario + ": byte count");
        check(matches_pattern(buffer, test.offset, count), scenario + ": content");
    }
    check(source.read_at(patterned_size, buffer.data(), 16, context.get()) == 0,
          "read at end returns zero");
    check(stats->reads == 3, "successful reads counted");
    check(stats->bytes == 16 + server_chunk + 10, "returned bytes counted");
    check(stats->failures == 1, "zero-byte read counted as failure");
    check(stats->lowest_offset == 0, "lowest offset recorded");
    check(stats->highest_end == patterned_size, "highest end recorded");
}

void cancellation_tests() {
    group = "cancellation";
    TemporaryFile file;
    file.write_patterned(64);
    auto stats = std::make_shared<FileReadStats>();
    auto source = open_file_media_source(file.path(), stats);
    std::vector<std::uint8_t> buffer(64);
    Context stopped;
    stopped.stopped = true;
    check(source.read_at(0, buffer.data(), 64, stopped.get()) == 0, "server stop yields no bytes");
    Context cancelled;
    cancelled.cancelled = true;
    check(source.read_at(0, buffer.data(), 64, cancelled.get()) == 0,
          "request cancellation yields no bytes");
    check(stats->reads == 0 && stats->failures == 2, "cancelled reads are failures");
}

void concurrency_tests() {
    group = "concurrent readers";
    TemporaryFile file;
    file.write_patterned(patterned_size);
    auto source = open_file_media_source(file.path(), std::make_shared<FileReadStats>());
    constexpr int reader_count = 4; // Default MediaServer worker count.
    constexpr int reads_per_reader = 500;
    std::atomic_int mismatches{0};
    std::vector<std::thread> readers;
    for (int reader = 0; reader < reader_count; ++reader) {
        readers.emplace_back([&source, &mismatches, reader] {
            Context context;
            std::mt19937_64 random(static_cast<std::uint64_t>(reader) + 1);
            std::vector<std::uint8_t> buffer(4096);
            for (int read = 0; read < reads_per_reader; ++read) {
                const auto offset = random() % patterned_size;
                const auto count =
                    source.read_at(offset, buffer.data(), buffer.size(), context.get());
                const auto expected =
                    std::min<std::uint64_t>(buffer.size(), patterned_size - offset);
                if (count != expected || !matches_pattern(buffer, offset, count)) {
                    ++mismatches;
                }
            }
        });
    }
    for (auto& reader : readers) {
        reader.join();
    }
    check(mismatches == 0, "serialized seek/read pairs return exact bytes for every reader");
}

void truncation_tests() {
    group = "file shrinks after open";
    TemporaryFile file;
    file.write_patterned(1024);
    auto source = open_file_media_source(file.path(), std::make_shared<FileReadStats>());
    std::error_code error;
    fs::resize_file(file.path(), 512, error);
    if (error) {
        std::cout << "SKIP [" << group << "]: host refused resize of an open file\n";
        return;
    }
    Context context;
    std::vector<std::uint8_t> buffer(64);
    check(source.size() == 1024, "size remains the snapshot");
    check(source.read_at(600, buffer.data(), 64, context.get()) == 0,
          "read beyond new end yields zero, so the server fails the request");
}

void large_offset_tests() {
    group = "offsets above 4 GiB";
#ifdef _WIN32
    // NTFS files are not sparse by default; avoid writing gigabytes on CI hosts.
    std::cout << "SKIP [" << group << "]: sparse fixture not created on Windows\n";
#else
    constexpr std::uint64_t marker_offset = 5ULL * 1024 * 1024 * 1024; // 5 GiB.
    const std::string marker = "sap2-large-mark!";                     // 16 bytes.
    TemporaryFile file;
    {
        std::ofstream output(file.path(), std::ios::binary | std::ios::trunc);
        output.seekp(static_cast<std::streamoff>(marker_offset));
        output.write(marker.data(), static_cast<std::streamsize>(marker.size()));
        if (!output) {
            std::cout << "SKIP [" << group << "]: host cannot create a sparse fixture\n";
            return;
        }
    }
    auto source = open_file_media_source(file.path(), std::make_shared<FileReadStats>());
    check(source.size() == marker_offset + marker.size(), "64-bit size snapshot");
    Context context;
    std::vector<std::uint8_t> buffer(marker.size());
    const auto count = source.read_at(marker_offset, buffer.data(), buffer.size(), context.get());
    check(count == marker.size() && std::string(buffer.begin(), buffer.end()) == marker,
          "read at 5 GiB returns the marker without offset truncation");
#endif
}

/// Minimal synchronous HTTP/1.1 exchange; the server closes after one response.
std::string http_exchange(const std::string& url, const std::string& headers) {
    const auto authority_end = url.find('/', 7);
    const auto authority = url.substr(7, authority_end - 7);
    const auto colon = authority.rfind(':');
    asio::io_context io;
    asio::ip::tcp::socket socket(io);
    socket.connect({asio::ip::make_address(authority.substr(0, colon)),
                    static_cast<std::uint16_t>(std::stoul(authority.substr(colon + 1)))});
    const auto request = "GET " + url.substr(authority_end) + " HTTP/1.1\r\nHost: " + authority +
                         "\r\n" + headers + "\r\n";
    asio::write(socket, asio::buffer(request));
    std::string reply;
    std::array<char, 4096> buffer{};
    boost::system::error_code error;
    while (!error) {
        const auto count = socket.read_some(asio::buffer(buffer), error);
        reply.append(buffer.data(), count);
    }
    return reply;
}

void loopback_http_tests() {
    group = "MediaServer over loopback";
    TemporaryFile file;
    file.write_patterned(patterned_size);
    auto stats = std::make_shared<FileReadStats>();
    MediaServerOptions options;
    options.receiver_address = "127.0.0.1";
    auto server = MediaServer::start(open_file_media_source(file.path(), stats), options);
    const auto reply = http_exchange(server->url(), "Range: bytes=70000-70099\r\n");
    check(reply.rfind("HTTP/1.1 206 ", 0) == 0, "range request returns 206");
    check(reply.find("\r\nContent-Range: bytes 70000-70099/200000\r\n") != std::string::npos,
          "exact Content-Range for the file size");
    const auto body_start = reply.find("\r\n\r\n");
    const auto body =
        body_start == std::string::npos ? std::string{} : reply.substr(body_start + 4);
    const std::vector<std::uint8_t> bytes(body.begin(), body.end());
    check(body.size() == 100 && matches_pattern(bytes, 70000, 100), "exact file bytes in body");
    server->stop();
    check(stats->lowest_offset == 70000 && stats->highest_end == 70100,
          "statistics reflect the served range");
}
} // namespace

int main() {
    try {
        open_refusal_tests();
        offset_and_statistics_tests();
        cancellation_tests();
        concurrency_tests();
        truncation_tests();
        large_offset_tests();
        loopback_http_tests();
    } catch (const std::exception& error) {
        std::cerr << "FAIL [" << group << "]: test infrastructure exception: " << error.what()
                  << '\n';
        return 1;
    }
    return failures ? 1 : 0;
}
