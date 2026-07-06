# API Tests CMake Configuration
# These tests start a test Drogon app with test database

# find_package(GTest REQUIRED)

# API test sources will be added as implementation progresses
set(API_TEST_SOURCES
    # api_smoke_test.cpp
)

# add_executable(pfh_api_tests ${API_TEST_SOURCES})

# target_link_libraries(pfh_api_tests PRIVATE
#     pfh_presentation
#     pfh_application
#     pfh_infrastructure
#     pfh_domain
#     GTest::GTest
#     GTest::Main
#     Drogon::Drogon
# )

# target_include_directories(pfh_api_tests PRIVATE
#     ${CMAKE_SOURCE_DIR}/include
#     ${CMAKE_SOURCE_DIR}/tests/support
# )

# include(GoogleTest)
# gtest_discover_tests(pfh_api_tests)
