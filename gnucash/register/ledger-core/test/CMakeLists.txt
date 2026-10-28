
set(SPLIT_REG_TEST_SOURCES
    test-split-register.c
    utest-split-register-copy-ops.c
)

set(SPLIT_REG_TEST_INCLUDE_DIRS
    ${CMAKE_SOURCE_DIR}/libgnucash/engine
    ${CMAKE_SOURCE_DIR}/gnucash/register/ledger-core
    ${CMAKE_BINARY_DIR}/common # for config.h
    ${CMAKE_SOURCE_DIR}/common/test-core  # for unittest-support.h
)

set(SPLIT_REG_TEST_LIBS
    gnc-engine
    gnc-ledger-core
)

gnc_add_test(test-split-register-copy-ops
    "${SPLIT_REG_TEST_SOURCES};${CMAKE_SOURCE_DIR}/common/test-core/unittest-support.c"
    SPLIT_REG_TEST_INCLUDE_DIRS
    SPLIT_REG_TEST_LIBS
)

set_dist_list(test_ledger_core_DIST CMakeLists.txt ${SPLIT_REG_TEST_SOURCES} gtest-entry-ledger-close-gtk4.cpp)

set(ENTRY_LEDGER_GTK4_INCLUDES
    ${CMAKE_SOURCE_DIR}/libgnucash/engine
    ${CMAKE_SOURCE_DIR}/gnucash/register/ledger-core
    ${CMAKE_SOURCE_DIR}/gnucash/register/register-gnome
    ${CMAKE_SOURCE_DIR}/gnucash/register/register-core
    ${CMAKE_SOURCE_DIR}/common/test-core
    ${CMAKE_BINARY_DIR}/common ${GUILE_INCLUDE_DIRS} ${GTEST_INCLUDE_DIR})
set(ENTRY_LEDGER_GTK4_LIBS gnc-engine gnc-ledger-core gtest)
gnc_add_test_with_guile(gtest-entry-ledger-close-gtk4 gtest-entry-ledger-close-gtk4.cpp ENTRY_LEDGER_GTK4_INCLUDES ENTRY_LEDGER_GTK4_LIBS)
add_dependencies(gtest-entry-ledger-close-gtk4 scm-gnome-utils scm-expressions gncmod-backend-xml compiled-schemas)
set_tests_properties(gtest-entry-ledger-close-gtk4 PROPERTIES TIMEOUT 30)
set_property(TEST gtest-entry-ledger-close-gtk4 APPEND PROPERTY ENVIRONMENT
    "GSETTINGS_SCHEMA_DIR=${SCHEMADIR_BUILD}" "GSETTINGS_BACKEND=memory")
