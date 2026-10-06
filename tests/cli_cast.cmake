# SPDX-License-Identifier: Apache-2.0
# Exercises the real cast command without contacting any receiver: argument
# refusals (exit 2), then a missing credential profile (exit 1), which must stop
# before any network work. Nothing may print a private URL.
set(media "${WORK_DIR}/cli-cast-media.bin")
file(WRITE "${media}" "0123456789")

function(run_cast expected_status expected_error scenario)
  execute_process(COMMAND "${CLI}" cast ${ARGN}
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors ENCODING UTF-8)
  if(NOT status EQUAL expected_status OR NOT errors MATCHES "${expected_error}" OR
     output MATCHES "http://" OR errors MATCHES "http://")
    message(FATAL_ERROR "cast ${scenario}: expected exit ${expected_status} and '${expected_error}', got ${status}: ${errors}")
  endif()
endfunction()

run_cast(2 "requires --address, --profile and --file" "missing profile" --address 127.0.0.1 --file "${media}")
run_cast(2 "unknown option: --pin" "unknown option" --address 127.0.0.1 --profile p --file "${media}" --pin 1)
run_cast(2 "incomplete option: --file" "incomplete option" --address 127.0.0.1 --profile p --file)
run_cast(2 "port must be 1..65535" "port bound" --address 127.0.0.1 --profile p --file "${media}" --port 0)
run_cast(2 "start timeout must be 1..120000" "start timeout bound" --address 127.0.0.1 --profile p --file "${media}" --start-timeout-ms 0)
run_cast(2 "not an existing regular file" "absent media" --address 127.0.0.1 --profile p --file "${WORK_DIR}/cli-cast-absent.bin")
# A random-looking profile is absent from the store (Windows), or the store is
# unsupported (other platforms); both stop before the media server or receiver.
run_cast(1 "Credentials:" "absent profile" --address 127.0.0.1 --profile cast-test-absent-7f3a9c --file "${media}")
file(REMOVE "${media}")
