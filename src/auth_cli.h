// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_AUTH_CLI_H
#define SEND_AIRPLAY2_AUTH_CLI_H
namespace send_airplay2::detail {
/// Execute an auth command (argv starts at command); diagnostics never include secrets.
/// Returns 0 on success, 2 for invalid arguments and 1 for runtime failure.
int run_auth_cli(int argc, const char* const* argv);
} // namespace send_airplay2::detail
#endif
