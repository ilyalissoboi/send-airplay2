// SPDX-License-Identifier: Apache-2.0
#ifndef SEND_AIRPLAY2_API_HOST_CLI_H
#define SEND_AIRPLAY2_API_HOST_CLI_H
namespace send_airplay2::detail {
/// Shared public-C-interface development driver. Arguments start at argv[1].
int run_api_host(int argc, const char* const* argv);
} // namespace send_airplay2::detail
#endif
