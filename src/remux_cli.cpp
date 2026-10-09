// SPDX-License-Identifier: Apache-2.0
#include "remux_cli.h"
#include "file_media_source.h"
#include "hls_remux.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace send_airplay2::detail {
namespace {
namespace fs = std::filesystem;
constexpr std::size_t copy_chunk = 64 * 1024; // MediaSource's read capacity bound.

struct RemuxArguments {
    std::string file;
    std::string out;
};

RemuxArguments parse_arguments(int argc, const char* const* argv) {
    RemuxArguments arguments;
    for (int index = 1; index < argc; ++index) {
        const std::string_view option = argv[index];
        if (index + 1 >= argc) {
            throw std::invalid_argument("unknown or incomplete option: " + std::string(option));
        }
        const std::string_view value = argv[++index];
        if (option == "--file") {
            arguments.file = std::string(value);
        } else if (option == "--out") {
            arguments.out = std::string(value);
        } else {
            throw std::invalid_argument("unknown option: " + std::string(option));
        }
    }
    if (arguments.file.empty() || arguments.out.empty()) {
        throw std::invalid_argument("remux requires --file and --out");
    }
    return arguments;
}

/// Copies one resource through its read_at, as the media server would.
std::uint64_t write_resource(const MediaResource& resource, const fs::path& path) {
    std::ofstream stream(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error("output file cannot be created");
    }
    const std::atomic_bool not_stopped{false};
    const std::atomic_bool not_cancelled{false};
    const MediaReadContext context{&not_stopped, &not_cancelled,
                                   std::chrono::steady_clock::now() + std::chrono::minutes(10)};
    std::array<std::uint8_t, copy_chunk> buffer{};
    const auto size = resource.source.size();
    std::uint64_t offset = 0;
    while (offset < size) {
        const auto wanted =
            static_cast<std::size_t>(std::min<std::uint64_t>(buffer.size(), size - offset));
        const auto count = resource.source.read_at(offset, buffer.data(), wanted, context);
        if (count == 0 || count > wanted) {
            throw std::runtime_error("a remuxed resource could not be read");
        }
        stream.write(reinterpret_cast<const char*>(buffer.data()),
                     static_cast<std::streamsize>(count));
        offset += count;
    }
    if (!stream.flush()) {
        throw std::runtime_error("output file cannot be written");
    }
    return size;
}

int remux(const RemuxArguments& arguments) {
    const fs::path out(arguments.out);
    std::error_code error;
    if (fs::exists(out, error) && !fs::is_empty(out, error)) {
        throw std::invalid_argument("--out must be a new or empty directory");
    }
    auto source = open_file_media_source(arguments.file, std::make_shared<FileReadStats>());
    const auto presentation = remux_mp4_to_hls(std::move(source));
    fs::create_directories(out);
    std::uint64_t bytes = 0;
    for (const auto& resource : presentation.resources) {
        bytes += write_resource(resource, out / resource.name);
    }
    std::cout << "Remux: segments=" << presentation.segment_count
              << " target_duration=" << presentation.target_duration_seconds
              << " files=" << presentation.resources.size() << " bytes=" << bytes << std::endl;
    return 0;
}
} // namespace

int run_remux_cli(int argc, const char* const* argv) {
    try {
        return remux(parse_arguments(argc, argv));
    } catch (const std::invalid_argument& error) {
        std::cerr << "Arguments: " << error.what() << '\n';
        return 2;
    } catch (const RemuxException& error) {
        std::cerr << "Remux (" << remux_failure_name(error.reason()) << "): " << error.what()
                  << '\n';
        return 2;
    } catch (const std::exception&) {
        std::cerr << "Remux failed while reading the input or writing the output.\n";
    }
    return 1;
}
} // namespace send_airplay2::detail
