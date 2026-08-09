
set(GNOME_UTILS_GUI_TEST_INCLUDE_DIRS
  ${CMAKE_BINARY_DIR}/common
  ${CMAKE_SOURCE_DIR}/gnucash/gnome-utils
  ${CMAKE_SOURCE_DIR}/libgnucash/engine
  ${GUILE_INCLUDE_DIRS}
)
set(GNOME_UTILS_GUI_TEST_LIBS
  test-core
  gnc-gnome-utils
)

set(GUILE_DEPENDS
  scm-gnome-utils
  test-core
  gnc-test-engine
  gnc-app-utils
  gnc-core-utils
  gnc-engine
)

gnc_add_scheme_test_targets(scm-test-load-gnome-utils-module
    SOURCES "test-load-gnome-utils-module.scm"
    OUTPUT_DIR "tests"
    DEPENDS "${GUILE_DEPENDS}")

gnc_add_scheme_tests(test-load-gnome-utils-module.scm)


set (test_gnc_file_dialog_SOURCES
  test-gnc-file-dialog.c
)

gnc_add_test (test-gnc-file-dialog "${test_gnc_file_dialog_SOURCES}"
  GNOME_UTILS_GUI_TEST_INCLUDE_DIRS GNOME_UTILS_GUI_TEST_LIBS)

set_dist_list(test_gnome_utils_DIST
  CMakeLists.txt
  test-load-gnome-utils-module.scm
  ${test_gnc_file_dialog_SOURCES}
)
