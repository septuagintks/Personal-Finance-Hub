# Integration Tests CMake Configuration
# These tests require PostgreSQL database

# find_package(GTest REQUIRED)

# Integration test sources will be added as implementation progresses
set(INTEGRATION_TEST_SOURCES
    # repository_test.cpp
)

# add_executable(pfh_integration_tests ${INTEGRATION_TEST_SOURCES})

# target_link_libraries(pfh_integration_tests PRIVATE
#     pfh_infrastructure
#     pfh_domain
#     GTest::gtest
#     GTest::gtest_main
#     PostgreSQL::PostgreSQL
# )

# target_include_directories(pfh_integration_tests PRIVATE
#     ${CMAKE_SOURCE_DIR}/include
#     ${CMAKE_SOURCE_DIR}/tests/support
# )

# include(GoogleTest)
# gtest_discover_tests(pfh_integration_tests)
