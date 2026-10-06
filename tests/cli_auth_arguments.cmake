# SPDX-License-Identifier: Apache-2.0
# Failures are checked through the real CLI and must never echo secret-like arguments.
execute_process(COMMAND "${CLI}" pair --address 127.0.0.1 --profile synthetic --pin 01234567
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors ENCODING UTF-8)
if(NOT status EQUAL 2 OR NOT errors MATCHES "Unknown authentication option" OR "${output}${errors}" MATCHES "01234567")
  message(FATAL_ERROR "Auth CLI must reject a PIN argument without repeating it")
endif()
if(WIN32)
  # execute_process supplies a redirected pipe, never the user's console. Refusal
  # must happen before enrollment; the random absent profile is never written.
  string(RANDOM LENGTH 32 ALPHABET 0123456789abcdef suffix)
  execute_process(COMMAND "${CLI}" pair --address 127.0.0.1 --port 1 --profile "test-${suffix}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors ENCODING UTF-8)
  if(NOT status EQUAL 2 OR NOT errors MATCHES "interactive Windows console")
    message(FATAL_ERROR "Redirected PIN input was not refused before receiver contact: ${status} ${errors}")
  endif()
endif()
