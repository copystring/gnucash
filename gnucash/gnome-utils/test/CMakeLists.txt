
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

gnc_add_test (test-dialog-object-references-response
  test-dialog-object-references-response.cpp
  GNOME_UTILS_GUI_TEST_INCLUDE_DIRS GNOME_UTILS_GUI_TEST_LIBS)

gnc_add_test (test-dialog-date-range-response
  test-dialog-date-range-response.cpp
  GNOME_UTILS_GUI_TEST_INCLUDE_DIRS GNOME_UTILS_GUI_TEST_LIBS)
set_tests_properties(test-dialog-date-range-response PROPERTIES TIMEOUT 15)

gnc_add_test (test-gui-query-response
  test-gui-query-response.cpp
  GNOME_UTILS_GUI_TEST_INCLUDE_DIRS GNOME_UTILS_GUI_TEST_LIBS)
set_tests_properties(test-gui-query-response PROPERTIES TIMEOUT 30)

gnc_add_test (test-dialog-account-cascade-response
  test-dialog-account-cascade-response.cpp
  GNOME_UTILS_GUI_TEST_INCLUDE_DIRS GNOME_UTILS_GUI_TEST_LIBS)
set_tests_properties(test-dialog-account-cascade-response PROPERTIES TIMEOUT 30)

set_dist_list(test_gnome_utils_DIST
  CMakeLists.txt
  test-load-gnome-utils-module.scm
  test-dialog-object-references-response.cpp
  test-dialog-date-range-response.cpp
  test-gui-query-response.cpp
  test-dialog-account-cascade-response.cpp)
