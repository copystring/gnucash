set(MODULEPATH ${CMAKE_SOURCE_DIR}/gnucash/gnome/)

set(GNOME_TEST_INCLUDE_DIRS
  ${CMAKE_BINARY_DIR}/common # for config.h
  ${CMAKE_SOURCE_DIR}/common/test-core
  ${CMAKE_SOURCE_DIR}/gnucash/gnome/
  ${CMAKE_SOURCE_DIR}/gnucash/gnome-utils/
  ${CMAKE_SOURCE_DIR}/libgnucash/engine
  ${GUILE_INCLUDE_DIRS}
)

set(GNOME_TEST_LIBS
        gnc-gnome
        gnc-test-engine
        test-core
        ${GUILE_LDFLAGS})

macro(add_gnome_test _TARGET _SOURCE_FILES)
  gnc_add_test(${_TARGET} "${_SOURCE_FILES}" GNOME_TEST_INCLUDE_DIRS GNOME_TEST_LIBS)
endmacro()

add_gnome_test(test-dialog-date-close test-dialog-date-close.c)
add_gnome_test(test-dialog-lot-viewer-scrub test-dialog-lot-viewer-scrub.c)
gnc_add_test_with_guile(test-assistant-hierarchy-ownership
  test-assistant-hierarchy-ownership.cpp GNOME_TEST_INCLUDE_DIRS GNOME_TEST_LIBS)
set_property(TEST test-assistant-hierarchy-ownership APPEND PROPERTY ENVIRONMENT
  GSETTINGS_SCHEMA_DIR=${CMAKE_BINARY_DIR}/share/glib-2.0/schemas)

gnc_add_test_with_guile(test-invoice-report-builtin-default test-invoice-report-builtin-default.cpp
  GNOME_TEST_INCLUDE_DIRS GNOME_TEST_LIBS
)

set(GUILE_DEPENDS
  scm-core-utils
  test-core
  gnc-test-engine
  gnc-app-utils
  gnc-core-utils
  gnc-engine
)

set (test_assistant-stock-transaction_SOURCES
  gtest-assistant-stock-transaction.cpp
)

set (test_assistant-stock-transaction_INCLUDE_DIRS
  ${CMAKE_BINARY_DIR}/common
  ${CMAKE_SOURCE_DIR}/libgnucash/engine
  ${GNOME_UTILS_GUI_TEST_INCLUDE_DIRS}
  ${CMAKE_SOURCE_DIR}/gnucash/register/ledger-core
  ${CMAKE_SOURCE_DIR}/gnucash/register/register-core
  ${CMAKE_SOURCE_DIR}/gnucash/register/register-gnome
  ${GNOME_UTILS_GUI_TEST_LIBS}
  ${GLIB2_INCLUDE_DIRS}
)

set (test_assistant-stock-transaction_LIBS
  gnc-engine
  gnc-gnome
  gnc-gnome-utils
  gtest
)

gnc_add_test (test-assistant-stock-transaction
  "${test_assistant-stock-transaction_SOURCES}"
  test_assistant-stock-transaction_INCLUDE_DIRS
  test_assistant-stock-transaction_LIBS
)

if (GNC_ENABLE_SHUTDOWN_TEST_HOOKS)
  gnc_generate_gresources(
    BASE gtest-register-save-close-resources
    RESOURCE_FILES "../../ui/gnc-main-window.ui"
                   "../../ui/gnc-plugin-page-register.ui"
                   "../../ui/gnc-plugin-register.ui")
  set(REGISTER_LIFECYCLE_SOURCES
    test-gnc-plugin-page-register-save-close-lifecycle.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/gtest-register-save-close-resources.c)
  set(REGISTER_LIFECYCLE_INCLUDE_DIRS
    ${GNOME_TEST_INCLUDE_DIRS}
    ${CMAKE_SOURCE_DIR}/libgnucash/engine/test-core)
  set(REGISTER_LIFECYCLE_LIBS
    ${GNOME_TEST_LIBS}
    gtest
    gnc-gnome-utils
    gnc-register-gnome
    gnc-register-core
    gnc-ledger-core)
  gnc_add_test_with_guile(test-gnc-plugin-page-register-save-close-lifecycle
    "${REGISTER_LIFECYCLE_SOURCES}"
    REGISTER_LIFECYCLE_INCLUDE_DIRS REGISTER_LIFECYCLE_LIBS)
  target_compile_definitions(test-gnc-plugin-page-register-save-close-lifecycle
    PRIVATE GNC_ENABLE_SHUTDOWN_TEST_HOOKS)
  add_dependencies(test-gnc-plugin-page-register-save-close-lifecycle
    scm-gnome-utils scm-expressions gncmod-backend-xml compiled-schemas)
  set_tests_properties(test-gnc-plugin-page-register-save-close-lifecycle PROPERTIES DISABLED TRUE)
  set_property(TEST test-gnc-plugin-page-register-save-close-lifecycle APPEND PROPERTY ENVIRONMENT
    "GSETTINGS_SCHEMA_DIR=${SCHEMADIR_BUILD}" "GSETTINGS_BACKEND=memory")
  get_test_property(test-gnc-plugin-page-register-save-close-lifecycle
    ENVIRONMENT REGISTER_LIFECYCLE_ENVIRONMENT)
  foreach(lifecycle_case IN ITEMS SaveClose CancelClose ValidationFailure BackendFailure ReentrantClose)
    add_test(NAME "test-gnc-plugin-page-register-save-close-lifecycle.${lifecycle_case}"
      COMMAND $<TARGET_FILE:test-gnc-plugin-page-register-save-close-lifecycle>
        "--gtest_filter=RegisterSaveCloseLifecycle.${lifecycle_case}")
    set_tests_properties("test-gnc-plugin-page-register-save-close-lifecycle.${lifecycle_case}" PROPERTIES
      ENVIRONMENT "${REGISTER_LIFECYCLE_ENVIRONMENT}"
      TIMEOUT 30)
  endforeach()
endif()

if (GNC_ENABLE_SHUTDOWN_TEST_HOOKS)
  add_gnome_test(test-order-save-request test-order-save-request.c)
  set_tests_properties(test-order-save-request PROPERTIES TIMEOUT 30)
endif()

set_dist_list(test_gnome_DIST
  CMakeLists.txt
  test-invoice-report-builtin-default.cpp
  test-order-save-request.c
  test-dialog-date-close.c
  test-dialog-lot-viewer-scrub.c
  test-assistant-hierarchy-ownership.cpp
  ${test_gnome_scheme_SOURCES}
  ${test_gnome_SOURCES}
  ${test_assistant-stock-transaction_SOURCES}
)
