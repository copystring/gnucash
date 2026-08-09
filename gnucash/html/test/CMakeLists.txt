set(HTML_TEST_INCLUDE_DIRS
  ${CMAKE_BINARY_DIR}/common
  ${CMAKE_SOURCE_DIR}/gnucash/html
)

set(HTML_TEST_LIBS
  gnc-html
  PkgConfig::GTK4
)

set(test_gnc_html_url_policy_SOURCES
  test-gnc-html-url-policy.cpp
)

gnc_add_test(test-gnc-html-url-policy
  "${test_gnc_html_url_policy_SOURCES}"
  HTML_TEST_INCLUDE_DIRS
  HTML_TEST_LIBS
)

set_dist_list(test_html_DIST
  CMakeLists.txt
  ${test_gnc_html_url_policy_SOURCES}
)
