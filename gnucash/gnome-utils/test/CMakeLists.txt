
set(GNOME_UTILS_GUI_TEST_INCLUDE_DIRS
  ${CMAKE_BINARY_DIR}/common
  ${CMAKE_SOURCE_DIR}/gnucash/gnome-utils
  ${CMAKE_SOURCE_DIR}/libgnucash/engine
  ${CMAKE_SOURCE_DIR}/libgnucash/core-utils
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


set (test_gnc_file_load_cancel_SOURCES
  test-gnc-file-load-cancel.c
)

gnc_add_test (test-gnc-file-load-cancel "${test_gnc_file_load_cancel_SOURCES}"
  GNOME_UTILS_GUI_TEST_INCLUDE_DIRS GNOME_UTILS_GUI_TEST_LIBS
  GNC_TEST_FILES=${CMAKE_SOURCE_DIR}/libgnucash/backend/xml/test/test-files/xml2)
add_dependencies (test-gnc-file-load-cancel gncmod-backend-xml)


set (test_gnc_account_type_list_SOURCES
  test-gnc-account-type-list.c
)

gnc_add_test (test-gnc-account-type-list "${test_gnc_account_type_list_SOURCES}"
  GNOME_UTILS_GUI_TEST_INCLUDE_DIRS GNOME_UTILS_GUI_TEST_LIBS)

set (test_gnc_tree_view_SOURCES
  test-gnc-tree-view.c
)

gnc_add_test (test-gnc-tree-view "${test_gnc_tree_view_SOURCES}"
  GNOME_UTILS_GUI_TEST_INCLUDE_DIRS GNOME_UTILS_GUI_TEST_LIBS)

set (test_gnc_register_input_SOURCES
  test-gnc-register-input.c
)

gnc_add_test (test-gnc-register-input "${test_gnc_register_input_SOURCES}"
  GNOME_UTILS_GUI_TEST_INCLUDE_DIRS GNOME_UTILS_GUI_TEST_LIBS)

set (test_gnc_statusbar_SOURCES
  test-gnc-statusbar.c
)

gnc_add_test (test-gnc-statusbar "${test_gnc_statusbar_SOURCES}"
  GNOME_UTILS_GUI_TEST_INCLUDE_DIRS GNOME_UTILS_GUI_TEST_LIBS)

set (test_gnc_toolbar_SOURCES
  test-gnc-toolbar.c
)

gnc_add_test (test-gnc-toolbar "${test_gnc_toolbar_SOURCES}"
  GNOME_UTILS_GUI_TEST_INCLUDE_DIRS GNOME_UTILS_GUI_TEST_LIBS
  GNC_UI_SOURCE_DIR=${CMAKE_SOURCE_DIR}/gnucash/ui)

set (test_gnc_window_application_SOURCES
  test-gnc-window-application.c
)

gnc_add_test (test-gnc-window-application
  "${test_gnc_window_application_SOURCES}"
  GNOME_UTILS_GUI_TEST_INCLUDE_DIRS GNOME_UTILS_GUI_TEST_LIBS)

set (test_gnucash_guile_bootstrap_SOURCES
  test-gnucash-guile-bootstrap.c
  ${CMAKE_SOURCE_DIR}/gnucash/gnucash-guile-bootstrap.c
)

set (GUILE_APPLICATION_TEST_INCLUDE_DIRS
  ${GNOME_UTILS_GUI_TEST_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/gnucash
)

set (GUILE_APPLICATION_TEST_LIBS
  PkgConfig::GTK4
  PkgConfig::GLIB2
  ${GUILE_LDFLAGS}
)

gnc_add_test_with_guile (test-gnucash-guile-bootstrap
  "${test_gnucash_guile_bootstrap_SOURCES}"
  GUILE_APPLICATION_TEST_INCLUDE_DIRS GUILE_APPLICATION_TEST_LIBS)

set (test_gnc_accelerators_SOURCES
  test-gnc-accelerators.c
)

gnc_add_test (test-gnc-accelerators "${test_gnc_accelerators_SOURCES}"
  GNOME_UTILS_GUI_TEST_INCLUDE_DIRS GNOME_UTILS_GUI_TEST_LIBS)

set (test_gnc_dense_cal_SOURCES
  test-gnc-dense-cal.c
)

gnc_add_test (test-gnc-dense-cal "${test_gnc_dense_cal_SOURCES}"
  GNOME_UTILS_GUI_TEST_INCLUDE_DIRS GNOME_UTILS_GUI_TEST_LIBS)

set (test_gnc_dropdown_width_SOURCES
  test-gnc-dropdown-width.c
)

gnc_add_test (test-gnc-dropdown-width "${test_gnc_dropdown_width_SOURCES}"
  GNOME_UTILS_GUI_TEST_INCLUDE_DIRS GNOME_UTILS_GUI_TEST_LIBS)

set (test_gnc_scrub_job_runner_SOURCES
  test-gnc-scrub-job-runner.c
)

gnc_add_test (test-gnc-scrub-job-runner "${test_gnc_scrub_job_runner_SOURCES}"
  GNOME_UTILS_GUI_TEST_INCLUDE_DIRS GNOME_UTILS_GUI_TEST_LIBS)


set_dist_list(test_gnome_utils_DIST
  CMakeLists.txt
  test-load-gnome-utils-module.scm
  ${test_gnc_file_dialog_SOURCES}
  ${test_gnc_file_load_cancel_SOURCES}
  ${test_gnc_account_type_list_SOURCES}
  ${test_gnc_tree_view_SOURCES}
  ${test_gnc_register_input_SOURCES}
  ${test_gnc_statusbar_SOURCES}
  ${test_gnc_toolbar_SOURCES}
  ${test_gnc_window_application_SOURCES}
  test-gnucash-guile-bootstrap.c
  ${test_gnc_accelerators_SOURCES}
  ${test_gnc_dense_cal_SOURCES}
  ${test_gnc_dropdown_width_SOURCES}
  ${test_gnc_scrub_job_runner_SOURCES}
)
