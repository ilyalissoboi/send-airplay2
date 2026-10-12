# SPDX-License-Identifier: Apache-2.0
set(sap2_previous_module_path "${CMAKE_MODULE_PATH}")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}")
_find_package(${ARGS})
set(CMAKE_MODULE_PATH "${sap2_previous_module_path}")
unset(sap2_previous_module_path)
