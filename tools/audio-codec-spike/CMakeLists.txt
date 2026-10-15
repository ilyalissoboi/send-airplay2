# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20)
# Standalone CI builds install only the probe manifest's codec dependencies.
# The optional root-project build remains available for integration development.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(sap2_audio_codec_spike LANGUAGES C CXX)
  if(NOT WINDOWS_STORE)
    include(CTest)
  endif()
endif()
function(sap2_codec_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()
find_package(FFMPEG 9.0 REQUIRED)
find_package(Threads REQUIRED)
set(sap2_decoder_root "${CMAKE_CURRENT_SOURCE_DIR}/../..")
# Private optional adapter: no FFmpeg dependency in the shipping playback target.
add_library(sap2_audio_decoder STATIC
  "${sap2_decoder_root}/src/audio_delivery_policy.cpp"
  "${sap2_decoder_root}/src/audio_decoder_source.cpp"
  "${sap2_decoder_root}/src/stereo_aac_encoder.cpp"
  "${sap2_decoder_root}/src/stereo_aac_hls.cpp"
  "${sap2_decoder_root}/src/hls_authoring.cpp"
  "${sap2_decoder_root}/src/hls_remux.cpp"
  "${sap2_decoder_root}/src/hls_variant.cpp"
  "${sap2_decoder_root}/src/fmp4_writer.cpp"
  "${sap2_decoder_root}/src/media_track_inventory.cpp"
  "${sap2_decoder_root}/src/mp4_demux.cpp"
  "${sap2_decoder_root}/src/mkv_demux.cpp"
  "${sap2_decoder_root}/src/sample_entries.cpp"
  "${sap2_decoder_root}/src/text_tracks.cpp")
target_compile_features(sap2_audio_decoder PUBLIC cxx_std_17)
target_include_directories(sap2_audio_decoder PUBLIC
  "${sap2_decoder_root}/src" "${sap2_decoder_root}/include")
target_include_directories(sap2_audio_decoder SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(sap2_audio_decoder PRIVATE ${FFMPEG_LIBRARIES} Threads::Threads)
sap2_codec_warnings(sap2_audio_decoder)
set_target_properties(sap2_audio_decoder PROPERTIES CXX_EXTENSIONS OFF)
add_library(sap2_codec_probe SHARED probe.cpp)
target_compile_features(sap2_codec_probe PRIVATE cxx_std_17)
target_compile_definitions(sap2_codec_probe PRIVATE SAP2_CODEC_PROBE_BUILD)
target_include_directories(sap2_codec_probe SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(sap2_codec_probe PRIVATE ${FFMPEG_LIBRARIES})
sap2_codec_warnings(sap2_codec_probe)
set_target_properties(sap2_codec_probe PROPERTIES CXX_EXTENSIONS OFF)
if(WINDOWS_STORE)
  # The library is separate from send_airplay2.dll; no codec is loaded by old callers.
  file(GLOB sap2_codec_runtime_dlls "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/av*.dll"
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/swresample*.dll")
  add_custom_command(TARGET sap2_codec_probe POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${sap2_codec_runtime_dlls} "$<TARGET_FILE_DIR:sap2_codec_probe>" COMMAND_EXPAND_LISTS)
elseif(BUILD_TESTING)
  add_executable(sap2_dts_seek_tests dts_seek_tests.cpp)
  target_link_libraries(sap2_dts_seek_tests PRIVATE sap2_audio_decoder ${FFMPEG_LIBRARIES})
  target_include_directories(sap2_dts_seek_tests SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
  sap2_codec_warnings(sap2_dts_seek_tests)
  add_test(NAME matroska_dts_bounded_seek COMMAND sap2_dts_seek_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hls/dts.mkv")
  set_tests_properties(matroska_dts_bounded_seek PROPERTIES TIMEOUT 120)
  add_executable(sap2_dts_lacing_tests dts_lacing_tests.cpp)
  target_link_libraries(sap2_dts_lacing_tests PRIVATE sap2_audio_decoder ${FFMPEG_LIBRARIES})
  target_include_directories(sap2_dts_lacing_tests SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
  sap2_codec_warnings(sap2_dts_lacing_tests)
  add_test(NAME matroska_dts_lacing COMMAND sap2_dts_lacing_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/dts-lacing")
  set_tests_properties(matroska_dts_lacing PROPERTIES TIMEOUT 120)
  add_executable(sap2_origin_hls_tests origin_hls_tests.cpp)
  target_link_libraries(sap2_origin_hls_tests PRIVATE sap2_audio_decoder ${FFMPEG_LIBRARIES})
  target_include_directories(sap2_origin_hls_tests SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
  sap2_codec_warnings(sap2_origin_hls_tests)
  add_test(NAME matroska_presentation_origin COMMAND sap2_origin_hls_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/origin")
  set_tests_properties(matroska_presentation_origin PROPERTIES TIMEOUT 120)
  add_executable(sap2_automatic_audio_tests "${sap2_decoder_root}/tests/automatic_audio_tests.cpp"
    "${sap2_decoder_root}/src/prepared_automatic_media.cpp" "${sap2_decoder_root}/src/prepared_stereo_media.cpp")
  target_compile_definitions(sap2_automatic_audio_tests PRIVATE SAP2_HAS_STEREO_AAC=1)
  target_link_libraries(sap2_automatic_audio_tests PRIVATE sap2_audio_decoder ${FFMPEG_LIBRARIES})
  target_include_directories(sap2_automatic_audio_tests SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
  sap2_codec_warnings(sap2_automatic_audio_tests)
  add_test(NAME automatic_audio_policy COMMAND sap2_automatic_audio_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/audio-policy" "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/decoder"
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/expanded-decoder")
  set_tests_properties(automatic_audio_policy PROPERTIES TIMEOUT 120)
  add_executable(sap2_compact_index_tests compact_index_tests.cpp)
  target_link_libraries(sap2_compact_index_tests PRIVATE sap2_audio_decoder ${FFMPEG_LIBRARIES})
  target_include_directories(sap2_compact_index_tests SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
  sap2_codec_warnings(sap2_compact_index_tests)
  add_test(NAME matroska_compact_index COMMAND sap2_compact_index_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/compressed-index"
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/compact-index")
  set_tests_properties(matroska_compact_index PROPERTIES TIMEOUT 120)
  add_executable(sap2_compressed_index_tests compressed_index_tests.cpp)
  target_link_libraries(sap2_compressed_index_tests PRIVATE sap2_audio_decoder ${FFMPEG_LIBRARIES})
  target_include_directories(sap2_compressed_index_tests SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
  sap2_codec_warnings(sap2_compressed_index_tests)
  add_test(NAME matroska_compressed_index COMMAND sap2_compressed_index_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/compressed-index")
  set_tests_properties(matroska_compressed_index PROPERTIES TIMEOUT 120)
  add_executable(sap2_pcm_index_tests pcm_index_tests.cpp)
  target_link_libraries(sap2_pcm_index_tests PRIVATE sap2_audio_decoder ${FFMPEG_LIBRARIES})
  target_include_directories(sap2_pcm_index_tests SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
  sap2_codec_warnings(sap2_pcm_index_tests)
  add_test(NAME matroska_pcm_index COMMAND sap2_pcm_index_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/pcm-index")
  set_tests_properties(matroska_pcm_index PROPERTIES TIMEOUT 90)
  add_executable(sap2_coarse_clock_tests coarse_clock_tests.cpp)
  target_link_libraries(sap2_coarse_clock_tests PRIVATE sap2_audio_decoder ${FFMPEG_LIBRARIES})
  target_include_directories(sap2_coarse_clock_tests SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
  sap2_codec_warnings(sap2_coarse_clock_tests)
  add_test(NAME coarse_matroska_clock COMMAND sap2_coarse_clock_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/coarse-clock")
  set_tests_properties(coarse_matroska_clock PROPERTIES TIMEOUT 90)
  add_executable(sap2_expanded_hls_tests expanded_hls_tests.cpp)
  target_link_libraries(sap2_expanded_hls_tests PRIVATE sap2_audio_decoder ${FFMPEG_LIBRARIES})
  target_include_directories(sap2_expanded_hls_tests SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
  sap2_codec_warnings(sap2_expanded_hls_tests)
  add_test(NAME expanded_audio_hls COMMAND sap2_expanded_hls_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/expanded-decoder")
  set_tests_properties(expanded_audio_hls PROPERTIES TIMEOUT 60)
  add_executable(sap2_expanded_decoder_tests expanded_decoder_tests.cpp)
  target_link_libraries(sap2_expanded_decoder_tests PRIVATE sap2_audio_decoder)
  sap2_codec_warnings(sap2_expanded_decoder_tests)
  add_test(NAME expanded_audio_decoder COMMAND sap2_expanded_decoder_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/expanded-decoder")
  set_tests_properties(expanded_audio_decoder PROPERTIES TIMEOUT 60)
  add_executable(sap2_stereo_aac_readiness_tests stereo_aac_readiness_tests.cpp)
  target_link_libraries(sap2_stereo_aac_readiness_tests PRIVATE sap2_audio_decoder ${FFMPEG_LIBRARIES})
  target_include_directories(sap2_stereo_aac_readiness_tests SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
  sap2_codec_warnings(sap2_stereo_aac_readiness_tests)
  add_test(NAME stereo_aac_readiness COMMAND sap2_stereo_aac_readiness_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hls")
  set_tests_properties(stereo_aac_readiness PROPERTIES TIMEOUT 60)
  add_executable(sap2_stereo_aac_hls_tests stereo_aac_hls_tests.cpp)
  target_link_libraries(sap2_stereo_aac_hls_tests PRIVATE sap2_audio_decoder ${FFMPEG_LIBRARIES})
  target_include_directories(sap2_stereo_aac_hls_tests SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
  sap2_codec_warnings(sap2_stereo_aac_hls_tests)
  add_test(NAME stereo_aac_hls COMMAND sap2_stereo_aac_hls_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/hls")
  set_tests_properties(stereo_aac_hls PROPERTIES TIMEOUT 60)
  add_executable(sap2_audio_decoder_tests decoder_source_tests.cpp)
  target_link_libraries(sap2_audio_decoder_tests PRIVATE sap2_audio_decoder)
  sap2_codec_warnings(sap2_audio_decoder_tests)
  add_test(NAME audio_decoder_source COMMAND sap2_audio_decoder_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/decoder")
  set_tests_properties(audio_decoder_source PROPERTIES TIMEOUT 60)
  add_executable(sap2_codec_probe_tests probe_tests.cpp)
  target_link_libraries(sap2_codec_probe_tests PRIVATE sap2_codec_probe)
  target_include_directories(sap2_codec_probe_tests SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_link_libraries(sap2_codec_probe_tests PRIVATE ${FFMPEG_LIBRARIES})
  target_compile_features(sap2_codec_probe_tests PRIVATE cxx_std_17)
  sap2_codec_warnings(sap2_codec_probe_tests)
  add_test(NAME audio_codec_probe COMMAND sap2_codec_probe_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
  set_tests_properties(audio_codec_probe PROPERTIES TIMEOUT 60)
endif()
