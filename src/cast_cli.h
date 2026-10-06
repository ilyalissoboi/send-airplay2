// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_CAST_CLI_H
#define SEND_AIRPLAY2_CAST_CLI_H
namespace send_airplay2::detail {
/** Execute `cast` (argv starts at the command name): play one local file on a
 * paired receiver through a URL playback session, until a line or end-of-file
 * arrives on standard input. Then stop the session and the media server and
 * print a sanitized summary. Never prints the private media URL, receiver
 * identifiers or credential data. Development command; controls (pause, seek)
 * are not implemented yet.
 * Returns 0 after a clean stop, 2 for invalid arguments and 1 for failure.
 */
int run_cast_cli(int argc, const char* const* argv);
} // namespace send_airplay2::detail
#endif
