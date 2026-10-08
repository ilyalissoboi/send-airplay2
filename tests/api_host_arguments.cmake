# SPDX-License-Identifier: Apache-2.0
# Exercises airplay2-api-host without contacting any receiver: argument refusals
# (exit 2), option ranges rejected by sap2_cast_create (exit 2), then a missing
# credential profile (exit 1), which must stop before any network work.
# Discovery refusals open no socket either.
# Nothing may print a private URL.
set(media "${WORK_DIR}/api-host-media.bin")
file(WRITE "${media}" "0123456789")

function(run_host expected_status expected_text scenario)
  execute_process(COMMAND "${HOST}" ${ARGN}
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE errors ENCODING UTF-8)
  set(combined "${output}${errors}")
  if(NOT status EQUAL expected_status OR NOT combined MATCHES "${expected_text}" OR
     combined MATCHES "http://")
    message(FATAL_ERROR "api host ${scenario}: expected exit ${expected_status} and '${expected_text}', got ${status}: ${combined}")
  endif()
endfunction()

run_host(0 "Usage: airplay2-api-host" "help" --help)
run_host(2 "requires --address, --profile and --file" "missing profile" --address 127.0.0.1 --file "${media}")
run_host(2 "unknown option: --pin" "unknown option" --address 127.0.0.1 --profile p --file "${media}" --pin 1)
run_host(2 "incomplete option: --file" "incomplete option" --address 127.0.0.1 --profile p --file)
run_host(2 "not an existing regular file" "absent media" --address 127.0.0.1 --profile p --file "${WORK_DIR}/api-host-absent.bin")
run_host(2 "exclusive" "exclusive modes" --address 127.0.0.1 --profile p --file "${media}" --cycles 2 --cancel-after-ms 5)
# Ranges are validated by the library itself, through the public interface.
run_host(2 "Create: invalid_argument" "media connection upper bound" --address 127.0.0.1 --profile p --file "${media}" --media-connections 17)
run_host(2 "Create: invalid_argument" "zero start timeout" --address 127.0.0.1 --profile p --file "${media}" --start-timeout-ms 0)
run_host(2 "Create: invalid_argument" "invalid profile" --address 127.0.0.1 --profile Not-Valid --file "${media}")
# An absent profile (Windows) or unsupported store (other platforms) ends the
# start before the media server or any receiver connection; the source is
# still released exactly once.
run_host(1 "Start: (profile_not_found|unsupported).*releases=1" "absent profile" --address 127.0.0.1 --profile api-host-test-absent-5d2e8b --file "${media}")
# Pairing mode: --file is refused, and an unreachable receiver fails before any
# PIN prompt (the built-in store reports the profile absent on Windows and is
# unsupported elsewhere; both stop without a PIN request).
run_host(2 "--pair requires --address and --profile only" "pair with file" --pair --address 127.0.0.1 --profile p --file "${media}")
run_host(1 "Pair: (connection|unsupported)" "pair unreachable" --pair --address 127.0.0.1 --port 9 --profile api-host-pair-absent-3c1d)
# Discovery mode: cast options are refused, and out-of-range durations are
# refused by sap2_discover() itself before any socket is opened.
run_host(2 "--discover accepts only" "discover with address" --discover --address 127.0.0.1)
# Options are refused by presence, so defaults and cast-only tuning count too.
run_host(2 "--discover accepts only" "discover with port" --discover --port 9)
run_host(2 "--discover accepts only" "discover with hold time" --discover --hold-ms 1)
run_host(2 "--discover accepts only" "discover with pair" --discover --pair)
run_host(2 "require --discover" "expect-name without discover" --address 127.0.0.1 --profile p --file "${media}" --expect-name TV)
run_host(2 "require --discover" "default scan time without discover" --address 127.0.0.1 --profile p --file "${media}" --discover-ms 5000)
run_host(2 "require --discover" "scan time in pair mode" --pair --address 127.0.0.1 --profile p --discover-ms 5000)
run_host(2 "Discover: invalid_argument receivers=0" "zero scan time" --discover --discover-ms 0)
run_host(2 "Discover: invalid_argument receivers=0" "scan time above the maximum" --discover --discover-ms 60001)
file(REMOVE "${media}")
