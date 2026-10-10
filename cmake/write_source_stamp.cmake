# SPDX-License-Identifier: Apache-2.0
# Writes OUT, a source stamp for a built library (scripts/pack_nuget.ps1):
#   commit=<HEAD of SOURCE when the library was linked, or unknown>
#   native_changes=<yes when NATIVE_PATHS had uncommitted changes, else no>
# Run after every build of the library (CMakeLists.txt), which leaves the
# library current with the tree, so the tree's state is the library's source.
# The pack script also checks with git that the commit's native sources match
# HEAD, rather than trusting the stamp's age.
set(commit unknown)
set(native_changes yes)
if(GIT AND EXISTS "${GIT}")
  execute_process(COMMAND "${GIT}" -C "${SOURCE}" rev-parse HEAD
                  OUTPUT_VARIABLE head RESULT_VARIABLE head_result
                  OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  separate_arguments(paths NATIVE_COMMAND "${NATIVE_PATHS}")
  execute_process(COMMAND "${GIT}" -C "${SOURCE}" status --porcelain -- ${paths}
                  OUTPUT_VARIABLE status RESULT_VARIABLE status_result ERROR_QUIET)
  if(head_result EQUAL 0 AND status_result EQUAL 0)
    set(commit "${head}")
    if(status STREQUAL "")
      set(native_changes no)
    endif()
  endif()
endif()
file(WRITE "${OUT}" "commit=${commit}\nnative_changes=${native_changes}\n")
