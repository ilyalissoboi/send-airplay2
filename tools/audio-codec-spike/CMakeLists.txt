# SPDX-License-Identifier: Apache-2.0
find_package(FFMPEG 9.0 REQUIRED)
add_library(sap2_codec_probe SHARED probe.cpp)
target_compile_features(sap2_codec_probe PRIVATE cxx_std_17)
target_compile_definitions(sap2_codec_probe PRIVATE SAP2_CODEC_PROBE_BUILD)
target_include_directories(sap2_codec_probe SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_libraries(sap2_codec_probe PRIVATE ${FFMPEG_LIBRARIES})
sap2_warnings(sap2_codec_probe)
set_target_properties(sap2_codec_probe PROPERTIES CXX_EXTENSIONS OFF)
if(WINDOWS_STORE)
  # The library is separate from send_airplay2.dll; no codec is loaded by old callers.
  file(GLOB sap2_codec_runtime_dlls "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/av*.dll"
    "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin/swresample*.dll")
  add_custom_command(TARGET sap2_codec_probe POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${sap2_codec_runtime_dlls} "$<TARGET_FILE_DIR:sap2_codec_probe>" COMMAND_EXPAND_LISTS)
elseif(BUILD_TESTING)
  add_executable(sap2_codec_probe_tests probe_tests.cpp)
  target_link_libraries(sap2_codec_probe_tests PRIVATE sap2_codec_probe)
  target_include_directories(sap2_codec_probe_tests SYSTEM PRIVATE ${FFMPEG_INCLUDE_DIRS})
  target_link_libraries(sap2_codec_probe_tests PRIVATE ${FFMPEG_LIBRARIES})
  target_compile_features(sap2_codec_probe_tests PRIVATE cxx_std_17)
  sap2_warnings(sap2_codec_probe_tests)
  add_test(NAME audio_codec_probe COMMAND sap2_codec_probe_tests
    "${CMAKE_CURRENT_SOURCE_DIR}/fixtures")
  set_tests_properties(audio_codec_probe PROPERTIES TIMEOUT 60)
endif()
