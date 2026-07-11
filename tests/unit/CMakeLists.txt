# Unit Tests CMake Configuration
# These tests do not require database or Drogon.
# GoogleTest is resolved by cmake/Dependencies.cmake (root scope).

# Unit test sources
set(UNIT_TEST_SOURCES
    smoke_test.cpp
    typed_id_test.cpp
    error_test.cpp
    decimal_test.cpp
    currency_test.cpp
    money_test.cpp
    exchange_rate_test.cpp
    currency_conversion_service_test.cpp
    transfer_domain_service_test.cpp
    balance_calculation_service_test.cpp
    category_test.cpp
    config_test.cpp
    logger_test.cpp
    use_case_test.cpp
)

add_executable(pfh_unit_tests ${UNIT_TEST_SOURCES})

target_link_libraries(pfh_unit_tests PRIVATE
    pfh_domain
    GTest::gtest
    GTest::gtest_main
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

target_include_directories(pfh_unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/tests/support
)

include(GoogleTest)
gtest_discover_tests(pfh_unit_tests)
