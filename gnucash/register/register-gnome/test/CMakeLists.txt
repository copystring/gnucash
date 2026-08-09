set(REGISTER_GNOME_TEST_INCLUDE_DIRS
  ${CMAKE_BINARY_DIR}/common
  ${CMAKE_SOURCE_DIR}/gnucash/register/register-gnome
  ${CMAKE_SOURCE_DIR}/libgnucash/core-utils
)

set(REGISTER_GNOME_TEST_LIBS
  gnc-core-utils
  gnc-register-gnome
)

set(test_gnc_completion_model_SOURCES
  test-gnc-completion-model.c
)

gnc_add_test(test-gnc-completion-model
  "${test_gnc_completion_model_SOURCES}"
  REGISTER_GNOME_TEST_INCLUDE_DIRS
  REGISTER_GNOME_TEST_LIBS
)

set_dist_list(test_register_gnome_DIST
  CMakeLists.txt
  ${test_gnc_completion_model_SOURCES}
)