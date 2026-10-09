// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_HLS_DIRECTORY_H
#define SEND_AIRPLAY2_HLS_DIRECTORY_H
#include "file_media_source.h"
#include "send_airplay2/media_server.h"
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace send_airplay2::detail {
/// The files of a pre-made HLS presentation, ready for
/// MediaServer::start_resource_set. playlist_name is the entry resource.
struct HlsDirectory {
    std::string playlist_name;
    std::vector<MediaResource> resources;
};

/** Content type for an HLS file name by extension: playlists (.m3u8), fMP4
 * init/media segments (.mp4, .m4s, .m4v), audio (.m4a, .aac) and transport
 * streams (.ts). Returns an empty view for any other extension. */
[[nodiscard]] std::string_view hls_content_type(std::string_view name);

/** Development CLI only (D60 phase 1): serve the files of a pre-made HLS
 * presentation, such as one written by ffmpeg, from the playlist's directory.
 *
 * Reads the playlist, and every playlist it references, and serves exactly
 * the files they name: URI lines and URI="..." attributes (EXT-X-MAP,
 * EXT-X-MEDIA, EXT-X-I-FRAME-STREAM-INF). Every reference must be a plain file
 * name in the same directory that MediaResource accepts as a name, with an
 * extension hls_content_type() knows; absolute URLs, subdirectories, queries
 * and parent references are refused, so nothing outside that list is served.
 *
 * Each file's size is snapshotted here; reads reopen the file per call, so a
 * long presentation holds no open handles. Files must not change while served:
 * a shorter file fails that request. read_at updates `stats` like
 * open_file_media_source's sources.
 *
 * @throws std::invalid_argument for a missing file, a playlist larger than
 *         4 MiB or not starting with #EXTM3U, a refused reference, or more
 *         than 65536 files. Messages name the offending reference, never the
 *         directory path.
 */
[[nodiscard]] HlsDirectory open_hls_directory(const std::filesystem::path& playlist,
                                              std::shared_ptr<FileReadStats> stats);
} // namespace send_airplay2::detail
#endif
