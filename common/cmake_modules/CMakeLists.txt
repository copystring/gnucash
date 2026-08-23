
set(cmake_FILES
  GncAddGSchemaTargets.cmake
  GncAddSchemeTargets.cmake
  GncAddSwigCommand.cmake
  GncAddTest.cmake
  GncFindLibm.cmake
  GncGenerateGResources.cmake
  GncMinGWTargetPaths.cmake
  MacroAddSourceFileCompileFlags.cmake
  MacroAppendForeach.cmake
  MakeDist.cmake
  MakeDistFiles.cmake
  MakeDistCheck.cmake
)

set(_gnc_mingw_target_path_test_cases
  valid
  valid-linker-flags-preserved
  valid-explicit-target-roots
  valid-posix-triplet
  reject-msys
  reject-msys-root-prefix
  reject-msys-ldflag
  reject-msys-cflag
  reject-msys-wl-ldflag-comma
  reject-msys-wl-ldflag-joined
  reject-msys-shell-ldflag
  reject-msys-shell-cflag
  reject-msys-split-ldflag
  reject-msys-split-cflag
  reject-msys-genex-library
  reject-msys-genex-include
  reject-probe-library
  reject-posix-library
  reject-posix-include
  reject-global-c-flags
  reject-global-cxx-flags
  reject-global-exe-linker-flags
  reject-global-shared-linker-flags
  reject-global-module-linker-flags
  reject-global-config-flags
  reject-pkg-cflags-other
  reject-pkg-ldflags-other
  reject-runtime-loader
  reject-runtime-executable
  reject-runtime-mingw-dlls)
foreach(_test_case IN LISTS _gnc_mingw_target_path_test_cases)
  add_test(NAME test-gnc-mingw-target-paths-${_test_case}
    COMMAND ${CMAKE_COMMAND}
      -DTEST_CASE=${_test_case}
      -DTEST_MODULE=${CMAKE_CURRENT_SOURCE_DIR}/GncMinGWTargetPaths.cmake
      -P ${CMAKE_CURRENT_SOURCE_DIR}/test-GncMinGWTargetPaths.cmake)
endforeach()
unset(_gnc_mingw_target_path_test_cases)

set_dist_list(cmake_modules_DIST CMakeLists.txt COPYING-CMAKE-SCRIPTS.txt
  test-GncMinGWTargetPaths.cmake ${cmake_FILES})
