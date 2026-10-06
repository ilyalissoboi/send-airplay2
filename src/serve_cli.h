// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_SERVE_CLI_H
#define SEND_AIRPLAY2_SERVE_CLI_H
namespace send_airplay2::detail {
/** Execute `serve` (argv starts at the command name). Serves one local file to one
 * receiver address until a line or end-of-file arrives on standard input, then
 * stops the server and prints aggregate read counts. Prints the private media
 * URL once to standard output for the operator; diagnostics never repeat it.
 * Returns 0 after a clean stop, 2 for invalid arguments and 1 for runtime failure.
 */
int run_serve_cli(int argc, const char* const* argv);
} // namespace send_airplay2::detail
#endif
