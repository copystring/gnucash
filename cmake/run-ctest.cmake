if(NOT CTEST_COMMAND)
  message(FATAL_ERROR "CTEST_COMMAND is required")
endif()

set(test_command "${CTEST_COMMAND}")
if(USE_VIRTUAL_DISPLAY)
  find_program(XVFB_RUN_EXECUTABLE xvfb-run)
  if(NOT XVFB_RUN_EXECUTABLE)
    message(FATAL_ERROR
      "xvfb-run is required to run the GnuCash tests on Linux")
  endif()
  set(test_command "${XVFB_RUN_EXECUTABLE}" -a "${CTEST_COMMAND}")
endif()

execute_process(COMMAND ${test_command} RESULT_VARIABLE test_result)
if(NOT test_result EQUAL 0)
  message(FATAL_ERROR "CTest failed with exit code ${test_result}")
endif()
