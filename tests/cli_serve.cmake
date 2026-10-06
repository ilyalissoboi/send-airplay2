# SPDX-License-Identifier: Apache-2.0
# Exercises the real serve command over loopback only: argument refusal, then a
# start/stop cycle driven by end-of-file on standard input. No receiver is contacted.
set(media "${WORK_DIR}/cli-serve-media.bin")
set(empty_input "${WORK_DIR}/cli-serve-empty-input.txt")
# Literal 10-byte representation; CMake writes it without newline translation.
file(WRITE "${media}" "0123456789")
file(WRITE "${empty_input}" "")

function(expect_refusal scenario expected_error)
  execute_process(COMMAND "${CLI}" serve ${ARGN}
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors ENCODING UTF-8)
  if(NOT status EQUAL 2 OR NOT errors MATCHES "${expected_error}" OR output MATCHES "http://")
    message(FATAL_ERROR "serve ${scenario}: expected exit 2 and '${expected_error}', got ${status}: ${errors}")
  endif()
endfunction()

expect_refusal("missing file" "requires --address and --file" --address 127.0.0.1)
expect_refusal("unknown option" "unknown option: --pin" --address 127.0.0.1 --file "${media}" --pin 1)
expect_refusal("incomplete option" "incomplete option: --file" --address 127.0.0.1 --file)
expect_refusal("port bound" "port must be 1..65535" --address 127.0.0.1 --file "${media}" --port 0)
expect_refusal("absent media" "not an existing regular file" --address 127.0.0.1 --file "${WORK_DIR}/cli-serve-absent.bin")
expect_refusal("directory media" "not an existing regular file" --address 127.0.0.1 --file "${WORK_DIR}")
expect_refusal("host name address" "Invalid media server options" --address localhost --file "${media}")
expect_refusal("content type without subtype" "Invalid media server options" --address 127.0.0.1 --file "${media}" --content-type video)

execute_process(COMMAND "${CLI}" serve --address 127.0.0.1 --file "${media}"
  INPUT_FILE "${empty_input}"
  RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors ENCODING UTF-8)
if(NOT status EQUAL 0 OR NOT errors STREQUAL "")
  message(FATAL_ERROR "serve start/stop: expected exit 0 without diagnostics, got ${status}: ${errors}")
endif()
foreach(expected
    "Serving 10 bytes as video/mp4 to receiver 127.0.0.1 only."
    "Private URL \\(do not share or log\\): http://127\\.0\\.0\\.1:[0-9]+/media/[^\n]+\n"
    "Stopped. reads=0 bytes=0 failed_reads=0\r?\n")
  if(NOT output MATCHES "${expected}")
    message(FATAL_ERROR "serve start/stop: output lacks '${expected}': ${output}")
  endif()
endforeach()
file(REMOVE "${media}" "${empty_input}")
