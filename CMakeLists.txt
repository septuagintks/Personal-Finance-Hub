# Personal Finance Hub - CMake Build Configuration
# Version: 1.0
# Backend: C++23
# Architecture: Clean Architecture + Lightweight DDD

cmake_minimum_required(VERSION 3.20)
project(PersonalFinanceHub VERSION 0.1.0 LANGUAGES CXX)

# =============================================================================
# Project Settings
# =============================================================================

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dashboard month windows use the C++20 IANA timezone database. Calendar-only
# chrono support is insufficient: the standard library must also provide and
# link locate_zone/time_zone. Fail at configure time with a useful diagnostic
# instead of failing deep in the report headers during the build.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles([=[
    #include <chrono>
    int main() {
        const auto* zone = std::chrono::locate_zone("UTC");
        const auto local = zone->to_local(std::chrono::system_clock::now());
        (void)local;
        return 0;
    }
]=] PFH_HAS_STD_CHRONO_TZDB)
if(NOT PFH_HAS_STD_CHRONO_TZDB)
    message(FATAL_ERROR
        "PFH requires a C++ standard library with std::chrono timezone database support "
        "(locate_zone/time_zone). Use a newer GCC/libstdc++ toolchain or another "
        "C++23 toolchain that passes this probe.")
endif()

# =============================================================================
# Build Type Configuration
# =============================================================================

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type (Debug/Release)" FORCE)
endif()

message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")

# =============================================================================
# Compiler Options
# =============================================================================

if(MSVC)
    add_compile_options(
        /W4                 # Warning level 4
        /WX                 # Treat warnings as errors
        /permissive-        # Standards conformance
        /utf-8              # UTF-8 source and execution charset
    )
    add_compile_definitions(
        _CRT_SECURE_NO_WARNINGS
        NOMINMAX
    )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall               # Enable most warnings
        -Wextra             # Enable extra warnings
        -Werror             # Treat warnings as errors
        -pedantic           # Strict ISO C++ compliance
        -Wconversion        # Warn on implicit conversions
        -Wsign-conversion   # Warn on sign conversions
    )
endif()

# Debug-specific flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_definitions(DEBUG)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-g -O0)
    endif()
endif()

# Release-specific flags
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_definitions(NDEBUG)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        add_compile_options(-O3)
    endif()
endif()

# =============================================================================
# Dependencies
# =============================================================================

find_package(Threads REQUIRED)

# PostgreSQL persistence: optional for local development without a database,
# required for production builds. When OFF, only In-Memory repositories compile.
option(PFH_BUILD_POSTGRESQL "Build PostgreSQL persistence adapters" OFF)

if(PFH_BUILD_POSTGRESQL)
    find_package(Drogon REQUIRED)
    find_package(PostgreSQL REQUIRED)
    find_package(OpenSSL REQUIRED)
    find_path(PFH_ARGON2_INCLUDE_DIR NAMES argon2.h REQUIRED)
    find_library(PFH_ARGON2_LIBRARY NAMES argon2 libargon2 REQUIRED)
    add_library(pfh_argon2 UNKNOWN IMPORTED)
    set_target_properties(pfh_argon2 PROPERTIES
        IMPORTED_LOCATION "${PFH_ARGON2_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${PFH_ARGON2_INCLUDE_DIR}"
    )
    message(STATUS "PostgreSQL persistence: ENABLED")
else()
    message(STATUS "PostgreSQL persistence: DISABLED (In-Memory only)")
endif()

# Resolves spdlog, nlohmann_json and GoogleTest via find_package with a
# FetchContent fallback (see cmake/Dependencies.cmake).
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(Dependencies)

# =============================================================================
# Include Directories
# =============================================================================

set(PFH_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(${PFH_INCLUDE_DIR})

# =============================================================================
# Source Files (to be populated)
# =============================================================================

# Domain Layer - Pure business logic, no framework dependencies
set(PFH_DOMAIN_SOURCES
    src/domain/decimal.cpp
    src/domain/currency.cpp
    src/domain/money.cpp
    src/domain/exchange_rate.cpp
    src/domain/currency_conversion_service.cpp
    src/domain/transfer_domain_service.cpp
    src/domain/balance_calculation_service.cpp
)

# Application Layer - Use cases and application services
set(PFH_APPLICATION_SOURCES
    src/application/auth_service.cpp
    src/application/resource_use_cases.cpp
    src/application/finance_application_service.cpp
    src/application/local_event_bus.cpp
    src/application/outbox_publisher.cpp
    src/application/supplemental_audit_handler.cpp
)

# Infrastructure Layer - External services, persistence, etc.
# Currently header-only In-Memory repositories live in include/.
# PostgreSQL implementations added in P1-S10-03, guarded by PFH_HAS_POSTGRESQL.
set(PFH_POSTGRESQL_SOURCES
    src/infrastructure/persistence/drogon_transaction_context.cpp
    src/infrastructure/persistence/drogon_unit_of_work.cpp
    src/infrastructure/persistence/drogon_unit_of_work_factory.cpp
    src/infrastructure/persistence/postgres_repository_support.cpp
    src/infrastructure/persistence/postgres_active_currency_query.cpp
    src/infrastructure/persistence/account_repository_impl.cpp
    src/infrastructure/persistence/transaction_repository_impl.cpp
    src/infrastructure/persistence/category_repository_impl.cpp
    src/infrastructure/persistence/tag_repository_impl.cpp
    src/infrastructure/persistence/user_repository_impl.cpp
    src/infrastructure/persistence/user_preference_repository_impl.cpp
    src/infrastructure/persistence/exchange_rate_repository_impl.cpp
    src/infrastructure/persistence/postgres_result_set.cpp
    src/infrastructure/persistence/rls_session.cpp
    src/infrastructure/persistence/auth_session_repository_impl.cpp
    src/infrastructure/persistence/audit_log_repository_impl.cpp
    src/infrastructure/persistence/registration_defaults_repository_impl.cpp
    src/infrastructure/persistence/postgres_outbox_repository.cpp
    src/infrastructure/persistence/postgres_supplemental_audit_store.cpp
    src/infrastructure/persistence/postgres_session_cleanup_repository.cpp
    src/infrastructure/persistence/postgres_job_lease_repository.cpp
    src/infrastructure/external/drogon_http_transport.cpp
    src/infrastructure/scheduler/drogon_timer_scheduler.cpp
)

set(PFH_PRODUCTION_SECURITY_SOURCES
    src/infrastructure/security/argon2_password_hasher.cpp
    src/infrastructure/security/openssl_token_service.cpp
)

set(PFH_INFRASTRUCTURE_SOURCES
    src/infrastructure/placeholder.cpp
    src/infrastructure/external/exchange_rate_providers.cpp
    src/infrastructure/scheduler/bounded_thread_pool.cpp
    src/infrastructure/scheduler/recurring_job.cpp
    src/infrastructure/scheduler/job_manager.cpp
    src/infrastructure/scheduler/scheduled_jobs.cpp
)

if(PFH_BUILD_POSTGRESQL)
    list(APPEND PFH_INFRASTRUCTURE_SOURCES ${PFH_POSTGRESQL_SOURCES})
    list(APPEND PFH_INFRASTRUCTURE_SOURCES ${PFH_PRODUCTION_SECURITY_SOURCES})
endif()

# Presentation Layer - REST API controllers
set(PFH_PRESENTATION_SOURCES
    src/presentation/time_codec.cpp
    src/presentation/json_request_parser.cpp
    src/presentation/http_response_mapper.cpp
    src/presentation/jwt_filter.cpp
    src/presentation/auth_controller.cpp
    src/presentation/resource_controllers.cpp
    src/presentation/transaction_controller.cpp
    src/presentation/transfer_controller.cpp
    src/presentation/report_controller.cpp
    src/presentation/api_application.cpp
)

if(PFH_BUILD_POSTGRESQL)
    list(APPEND PFH_PRESENTATION_SOURCES
        src/presentation/drogon_http_adapter.cpp
    )
endif()

# =============================================================================
# Library Targets
# =============================================================================

# Domain Library (no external dependencies - MUST NOT link Drogon/PostgreSQL/spdlog)
add_library(pfh_domain STATIC ${PFH_DOMAIN_SOURCES})
target_include_directories(pfh_domain PUBLIC ${PFH_INCLUDE_DIR})

# Application Layer - use-case orchestration, depending only on Domain ports.
add_library(pfh_application STATIC ${PFH_APPLICATION_SOURCES})
target_include_directories(pfh_application PUBLIC ${PFH_INCLUDE_DIR})
target_link_libraries(pfh_application PUBLIC pfh_domain)

# Infrastructure Library - In-Memory (always) + PostgreSQL (when PFH_BUILD_POSTGRESQL=ON)
add_library(pfh_infrastructure STATIC ${PFH_INFRASTRUCTURE_SOURCES})
target_include_directories(pfh_infrastructure PUBLIC ${PFH_INCLUDE_DIR})
target_link_libraries(pfh_infrastructure PUBLIC
    pfh_application
    spdlog::spdlog
    nlohmann_json::nlohmann_json
    Threads::Threads
)

if(PFH_BUILD_POSTGRESQL)
    target_compile_definitions(pfh_infrastructure PUBLIC PFH_HAS_POSTGRESQL=1)
    target_link_libraries(pfh_infrastructure PUBLIC
        Drogon::Drogon
        PostgreSQL::PostgreSQL
        OpenSSL::Crypto
        pfh_argon2
    )
endif()

# Presentation Library - framework-neutral HTTP boundary and Drogon adapter.
add_library(pfh_presentation STATIC ${PFH_PRESENTATION_SOURCES})
target_include_directories(pfh_presentation PUBLIC ${PFH_INCLUDE_DIR})
target_link_libraries(pfh_presentation PUBLIC
    pfh_application
    nlohmann_json::nlohmann_json
)
if(PFH_BUILD_POSTGRESQL)
    target_compile_definitions(pfh_presentation PUBLIC PFH_HAS_POSTGRESQL=1)
    target_link_libraries(pfh_presentation PUBLIC Drogon::Drogon)
endif()

# Header-only fixtures/helpers shared by all test layers.
add_library(pfh_test_support INTERFACE)
target_include_directories(pfh_test_support INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/support
)
target_link_libraries(pfh_test_support INTERFACE pfh_application)

# The regular local build intentionally works without Drogon. Still compile all
# PostgreSQL adapter translation units against a narrow API-shaped test stub so
# syntax/type errors cannot hide behind PFH_BUILD_POSTGRESQL=OFF. Production ON
# builds always use the real Drogon target and do not see this include path.
if(NOT PFH_BUILD_POSTGRESQL)
    add_library(pfh_postgresql_adapter_compile_gate OBJECT
        ${PFH_POSTGRESQL_SOURCES}
    )
    target_compile_definitions(pfh_postgresql_adapter_compile_gate PRIVATE
        PFH_HAS_POSTGRESQL=1
    )
    target_include_directories(pfh_postgresql_adapter_compile_gate BEFORE PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/drogon_stub
        ${PFH_INCLUDE_DIR}
    )
    target_link_libraries(pfh_postgresql_adapter_compile_gate PRIVATE
        pfh_application
        spdlog::spdlog
        nlohmann_json::nlohmann_json
    )

    add_library(pfh_production_bootstrap_compile_gate OBJECT
        src/bootstrap/database_client_factory.cpp
        src/bootstrap/production_composition_root.cpp
        src/presentation/drogon_http_adapter.cpp
    )
    target_compile_definitions(pfh_production_bootstrap_compile_gate PRIVATE
        PFH_HAS_POSTGRESQL=1
    )
    target_include_directories(pfh_production_bootstrap_compile_gate BEFORE PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/drogon_stub
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/security_stub
        ${PFH_INCLUDE_DIR}
    )
    target_link_libraries(pfh_production_bootstrap_compile_gate PRIVATE
        pfh_presentation
        pfh_infrastructure
        spdlog::spdlog
        nlohmann_json::nlohmann_json
    )

    add_library(pfh_production_security_compile_gate OBJECT
        ${PFH_PRODUCTION_SECURITY_SOURCES}
    )
    target_compile_definitions(pfh_production_security_compile_gate PRIVATE
        PFH_HAS_POSTGRESQL=1
    )
    target_include_directories(pfh_production_security_compile_gate BEFORE PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/support/security_stub
        ${PFH_INCLUDE_DIR}
    )
    target_link_libraries(pfh_production_security_compile_gate PRIVATE
        pfh_application
        nlohmann_json::nlohmann_json
    )
endif()

# =============================================================================
# Executable Target
# =============================================================================

set(PFH_BOOTSTRAP_SOURCES src/bootstrap/main.cpp)
if(PFH_BUILD_POSTGRESQL)
    list(APPEND PFH_BOOTSTRAP_SOURCES
        src/bootstrap/database_client_factory.cpp
        src/bootstrap/production_composition_root.cpp
    )
endif()

add_executable(pfh_server ${PFH_BOOTSTRAP_SOURCES})

target_link_libraries(pfh_server PRIVATE
    pfh_presentation
    pfh_infrastructure
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

# =============================================================================
# Testing
# =============================================================================

enable_testing()

# Unit Tests
add_subdirectory(tests/unit)

# Migration enum-cast gate: scans all V*.sql under migrations/ and fails when an
# enum column literal lacks an explicit PostgreSQL type cast (e.g.
# 'expense'::category_board). Regression for V3 empty-DB migration failure
# (S10 report §4.2). Runs without PostgreSQL, Flyway, or Docker.
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(
    NAME migration_enum_casts
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/sql/validate_enum_casts.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
set_tests_properties(migration_enum_casts PROPERTIES LABELS "migrations;sql")

add_test(
    NAME currency_catalog_parity
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/sql/validate_currency_catalog.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
set_tests_properties(currency_catalog_parity PROPERTIES
    LABELS "migrations;domain;static"
)

# Offline regression gate for the PostgreSQL adapter's transaction/RLS wiring.
# This complements the compile gate above; real database behavior remains part
# of the external PostgreSQL test matrix.
add_test(
    NAME postgresql_adapter_contracts
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/sql/validate_postgresql_adapter_contracts.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
set_tests_properties(postgresql_adapter_contracts PROPERTIES
    LABELS "postgresql;static"
)

add_test(
    NAME openapi_contract
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/sql/validate_openapi_contract.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)
set_tests_properties(openapi_contract PROPERTIES LABELS "api;contract;static")

# Integration Tests
add_subdirectory(tests/integration)

# Framework-neutral API tests run without Drogon/PostgreSQL and exercise the
# same parser/filter/controller graph used by the production adapter.
add_subdirectory(tests/api)

# =============================================================================
# Installation (optional)
# =============================================================================

# install(TARGETS pfh_server
#     RUNTIME DESTINATION bin
# )

# =============================================================================
# Summary
# =============================================================================

message(STATUS "=============================================================")
message(STATUS "Personal Finance Hub - Configuration Summary")
message(STATUS "=============================================================")
message(STATUS "Project Version: ${PROJECT_VERSION}")
message(STATUS "C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
if(PFH_BUILD_POSTGRESQL)
    message(STATUS "PostgreSQL: ${PostgreSQL_VERSION_STRING}")
    message(STATUS "Drogon: ${Drogon_VERSION}")
endif()
message(STATUS "=============================================================")
