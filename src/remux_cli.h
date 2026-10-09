// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_REMUX_CLI_H
#define SEND_AIRPLAY2_REMUX_CLI_H
namespace send_airplay2::detail {
/** Execute `remux --file PATH --out DIR` (argv starts at the command name):
 * development only (D60). Builds the HLS presentation the remux would serve
 * for an MP4/MOV or MKV file and writes its files (playlist, init segment, media
 * segments) into DIR, which is created and must not already contain files,
 * so tools such as ffmpeg can check it offline. Contacts no receiver.
 * Returns 0 on success, 2 for invalid arguments or an input that cannot be
 * remuxed, and 1 for read/write failures.
 */
int run_remux_cli(int argc, const char* const* argv);
} // namespace send_airplay2::detail
#endif
