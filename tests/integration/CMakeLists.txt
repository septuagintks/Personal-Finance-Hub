# Integration Tests CMake Configuration
# In-memory repository tests validate persistence consistency rules without
# requiring a live PostgreSQL instance. PostgreSQL-backed tests can be added
# later alongside these scenarios.

set(INTEGRATION_TEST_SOURCES
    repository_integration_test.cpp
)

add_executable(pfh_integration_tests ${INTEGRATION_TEST_SOURCES})

target_link_libraries(pfh_integration_tests PRIVATE
    pfh_domain
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(pfh_integration_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/support
)

include(GoogleTest)
# Discover at test time (not configure/build time) so a failed first build does
# not permanently register pfh_integration_tests_NOT_BUILT in CTest.
gtest_discover_tests(pfh_integration_tests
    DISCOVERY_MODE PRE_TEST
)
