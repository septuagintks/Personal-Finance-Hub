# Integration Tests CMake Configuration
# In-memory repository tests validate persistence consistency rules without a
# live database. The production-enabled block below adds mandatory PostgreSQL
# and real Drogon runtime fixtures alongside these scenarios.

set(INTEGRATION_TEST_SOURCES
    repository_integration_test.cpp
)

add_executable(pfh_integration_tests ${INTEGRATION_TEST_SOURCES})

target_link_libraries(pfh_integration_tests PRIVATE
    pfh_test_support
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(pfh_integration_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

include(GoogleTest)
# Discover at test time (not configure/build time) so a failed first build does
# not permanently register pfh_integration_tests_NOT_BUILT in CTest.
gtest_discover_tests(pfh_integration_tests
    DISCOVERY_MODE PRE_TEST
)

if(PFH_BUILD_POSTGRESQL)
    add_executable(pfh_postgresql_integration_tests
        postgresql_integration_test.cpp
    )
    target_link_libraries(pfh_postgresql_integration_tests PRIVATE
        pfh_test_support
        pfh_infrastructure
        GTest::gtest
    )
    target_include_directories(pfh_postgresql_integration_tests PRIVATE
        ${CMAKE_SOURCE_DIR}/include
    )
    add_test(
        NAME postgresql_integration
        COMMAND pfh_postgresql_integration_tests
    )
    set_tests_properties(postgresql_integration PROPERTIES
        LABELS "integration;postgresql;external"
        TIMEOUT 180
    )
    add_test(
        NAME drogon_runtime_integration
        COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_SOURCE_DIR}/tests/runtime/drogon_api_smoke.py
                --server $<TARGET_FILE:pfh_server>
                --port 18080
    )
    set_tests_properties(drogon_runtime_integration PROPERTIES
        LABELS "integration;postgresql;drogon;runtime"
        TIMEOUT 180
        RUN_SERIAL TRUE
    )
endif()
