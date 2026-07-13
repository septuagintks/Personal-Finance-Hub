# API Tests CMake Configuration

add_executable(pfh_api_tests
    api_auth_test.cpp
    api_resource_test.cpp
    http_boundary_test.cpp
)

target_link_libraries(pfh_api_tests PRIVATE
    pfh_presentation
    pfh_infrastructure
    pfh_test_support
    GTest::gtest
    GTest::gtest_main
    nlohmann_json::nlohmann_json
)

target_include_directories(pfh_api_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/support
)

include(GoogleTest)
gtest_discover_tests(pfh_api_tests DISCOVERY_MODE PRE_TEST)
