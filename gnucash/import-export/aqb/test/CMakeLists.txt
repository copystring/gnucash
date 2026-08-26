set(test_aqb_SOURCES test-aqb.c test-kvp.c test-standing-orders.c)
set(test_flicker_gui_SOURCES test-flicker-gui.c)
set(test_gwen_gui_shutdown_SOURCES test-gwen-gui-shutdown.c
  ../gnc-gwen-gui.c)

set(test_aqb_INCLUDE_DIRS
  ${CMAKE_BINARY_DIR}/common
  ${CMAKE_SOURCE_DIR}/gnucash/import-export/aqb
  ${CMAKE_SOURCE_DIR}/common/test-core
  ${CMAKE_SOURCE_DIR}/common
  ${CMAKE_SOURCE_DIR}/gnucash/import-export
  ${CMAKE_SOURCE_DIR}/gnucash/gnome
  ${CMAKE_SOURCE_DIR}/gnucash/register/ledger-core
  ${CMAKE_SOURCE_DIR}/gnucash/register/register-gnome
  ${CMAKE_SOURCE_DIR}/gnucash/register/register-core
  ${CMAKE_SOURCE_DIR}/gnucash/gnome-utils
  ${CMAKE_SOURCE_DIR}/libgnucash/app-utils
  ${CMAKE_SOURCE_DIR}/libgnucash/engine
  ${CMAKE_SOURCE_DIR}/libgnucash/core-utils
  ${CMAKE_SOURCE_DIR}/libgnucash/gnc-module
  ${AQBANKING_INCLUDE_DIRS}
)

set(test_aqb_LIBS
  gncmod-aqbanking gnc-generic-import gnc-gnome gnc-gnome-utils
  gnc-ledger-core gnc-app-utils
  gnc-backend-xml-utils gnc-engine gnc-core-utils gnc-module
)

set_dist_list(test_aqb_DIST ${test_aqb_SOURCES} file-book.gnucash
  file-book-hbcislot.gnucash ${test_flicker_gui_SOURCES}
  ${test_gwen_gui_shutdown_SOURCES} CMakeLists.txt )
if(WITH_AQBANKING)
  gnc_add_test(test-aqb
    "${test_aqb_SOURCES}"
    test_aqb_INCLUDE_DIRS
    test_aqb_LIBS SRCDIR=${CMAKE_CURRENT_SOURCE_DIR})
  gnc_add_test(test-flicker-gui
    "${test_flicker_gui_SOURCES}"
    test_aqb_INCLUDE_DIRS
    test_aqb_LIBS)
  gnc_add_test(test-gwen-gui-shutdown
    "${test_gwen_gui_shutdown_SOURCES}"
    test_aqb_INCLUDE_DIRS
    test_aqb_LIBS)
  target_compile_definitions(test-gwen-gui-shutdown PRIVATE
    GNC_GWEN_GUI_TESTING
    G_LOG_DOMAIN="gnc.import.aqbanking")
  if(WIN32)
    file(GLOB GWENHYWFAR_CONFIGMGR_PLUGINS
      LIST_DIRECTORIES FALSE
      "${GWENHYWFAR_LIBDIR}/gwenhywfar/plugins/*/configmgr/dir.dll")
    list(LENGTH GWENHYWFAR_CONFIGMGR_PLUGINS
      GWENHYWFAR_CONFIGMGR_PLUGIN_COUNT)
    if(NOT GWENHYWFAR_CONFIGMGR_PLUGIN_COUNT EQUAL 1)
      message(FATAL_ERROR
        "Expected one Gwenhywfar configmgr plugin below ${GWENHYWFAR_LIBDIR}")
    endif()
    list(GET GWENHYWFAR_CONFIGMGR_PLUGINS 0 GWENHYWFAR_CONFIGMGR_PLUGIN)
    get_filename_component(GWENHYWFAR_CONFIGMGR_DIR
      "${GWENHYWFAR_CONFIGMGR_PLUGIN}" DIRECTORY)
    set(AQBANKING_TEST_LOCALE_DIR "${AQBANKING_PREFIX}/share/locale")
    if(NOT IS_DIRECTORY "${AQBANKING_TEST_LOCALE_DIR}")
      message(FATAL_ERROR
        "AqBanking locale directory not found below ${AQBANKING_PREFIX}")
    endif()
    target_compile_definitions(test-gwen-gui-shutdown PRIVATE
      GNC_TEST_GWEN_CONFIGMGR_DIR="${GWENHYWFAR_CONFIGMGR_DIR}")

    # AqBanking on Windows resolves its installed paths from the executable's
    # prefix. Give the test a private install-shaped runtime layout so that its
    # unmodified locale-path initialization sees the configured dependency.
    set(AQBANKING_TEST_RUNTIME_PREFIX
      "${CMAKE_CURRENT_BINARY_DIR}/aqbanking-runtime")
    file(MAKE_DIRECTORY "${AQBANKING_TEST_RUNTIME_PREFIX}/bin")
    file(COPY "${AQBANKING_TEST_LOCALE_DIR}"
      DESTINATION "${AQBANKING_TEST_RUNTIME_PREFIX}/share")
    set_target_properties(test-gwen-gui-shutdown PROPERTIES
      RUNTIME_OUTPUT_DIRECTORY "${AQBANKING_TEST_RUNTIME_PREFIX}/bin")
  endif()
 endif()
