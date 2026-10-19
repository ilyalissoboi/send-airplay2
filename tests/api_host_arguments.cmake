# SPDX-License-Identifier: Apache-2.0
# Exercises airplay2-api-host without contacting any receiver: argument refusals
# (exit 2), option ranges rejected by sap2_cast_create (exit 2), then a missing
# credential profile (exit 1), which must stop before any network work.
# Discovery refusals open no socket either.
# Nothing may print a private URL.
set(media "${WORK_DIR}/api-host-media.bin")
set(POLICY_FIXTURES "${CMAKE_CURRENT_LIST_DIR}/../tools/audio-codec-spike/fixtures/audio-policy")
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
run_host(2 "exclusive with remux" "automatic and legacy modes are exclusive" --automatic-audio --remux --address 127.0.0.1 --profile p --file "${media}")
run_host(2 "requires automatic audio" "preservation requires an automatic mode" --preserve-audio --address 127.0.0.1 --profile p --file "${media}")
run_host(2 "must be provided together" "automatic cache configuration is paired" --automatic-audio --cache-directory "${WORK_DIR}" --address 127.0.0.1 --profile p --file "${media}")
run_host(2 "accepts file" "offline audio check rejects receiver options" --check-audio --address 127.0.0.1 --file "${media}")
run_host(1 "Audio check: media_(unsupported|malformed)" "offline refusal without credentials or cache" --check-audio --file "${media}")
run_host(0 "Audio check: ok selected_audio_id=42 action=2 reason=2 source_family=1 output_family=1 channels=2" "offline copy works without backend/cache" --check-audio --file "${POLICY_FIXTURES}/aac-48000-2.mp4" --preserve-audio)
run_host(0 "Audio check: ok selected_audio_id=0 action=1" "offline video only" --check-audio --file "${POLICY_FIXTURES}/video-only.mp4")
run_host(0 "Audio check: ok selected_audio_id=1 action=4 reason=4 source_family=1 output_family=1 channels=2" "audio-only progressive AAC" --check-audio --audio-only --preserve-audio --file "${POLICY_FIXTURES}/../audio-only/aac.mp4")
run_host(0 "Audio check: ok selected_audio_id=1 action=4 reason=4 source_family=10 output_family=10 channels=2" "audio-only progressive MP3" --check-audio --audio-only --file "${POLICY_FIXTURES}/../../../mp3-probe/fixtures/vbr441.mp3")
run_host(1 "Audio check: media_unsupported" "audio-only profile refuses video" --check-audio --audio-only --file "${POLICY_FIXTURES}/aac-48000-2.mp4")
run_host(2 "requires automatic audio" "audio-only flag requires automatic mode" --audio-only --address 127.0.0.1 --profile p --file "${media}")
run_host(2 "not pairing options" "audio-only flag refuses pairing mode" --pair --audio-only --address 127.0.0.1 --profile p)
run_host(2 "requires cache directory and quota" "conversion requires explicit host cache" --stereo-aac --address 127.0.0.1 --profile p --file "${media}")
run_host(2 "require --stereo-aac" "selection requires conversion" --audio-track 47 --address 127.0.0.1 --profile p --file "${media}")
run_host(2 "exclusive with remux" "delivery modes are exclusive" --stereo-aac --remux --cache-directory "${WORK_DIR}" --cache-quota-bytes 1024 --address 127.0.0.1 --profile p --file "${media}")
run_host(1 "Probe: media_(malformed|unsupported)" "metadata failure is contained without receiver" --probe-tracks --file "${media}")
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
