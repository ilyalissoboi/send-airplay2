// SPDX-License-Identifier: Apache-2.0
#include "api_host_cli.h"
int main(int argc, char** argv) {
    return send_airplay2::detail::run_api_host(argc, argv);
}
