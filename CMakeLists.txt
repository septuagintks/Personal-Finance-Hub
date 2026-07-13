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

# find_package(Drogon REQUIRED)     # Added in P1-S10
# find_package(PostgreSQL REQUIRED)  # Added in P1-S07

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
    # Core types (header-only for now)
    # src/application/error.cpp  # Header-only
)

# Infrastructure Layer - External services, persistence, etc.
set(PFH_INFRASTRUCTURE_SOURCES
    # Configuration and logging (header-only for now)
    # src/infrastructure/config.cpp  # Header-only
    # src/infrastructure/logger.cpp  # Header-only
)

# Presentation Layer - REST API controllers
set(PFH_PRESENTATION_SOURCES
    # Will be added as implementation progresses
)

# =============================================================================
# Library Targets
# =============================================================================

# Domain Library (no external dependencies - MUST NOT link Drogon/PostgreSQL/spdlog)
add_library(pfh_domain STATIC ${PFH_DOMAIN_SOURCES})
target_include_directories(pfh_domain PUBLIC ${PFH_INCLUDE_DIR})

# Application Library
# add_library(pfh_application STATIC ${PFH_APPLICATION_SOURCES})
# target_link_libraries(pfh_application PUBLIC pfh_domain)

# Infrastructure Library
# add_library(pfh_infrastructure STATIC ${PFH_INFRASTRUCTURE_SOURCES})
# target_link_libraries(pfh_infrastructure PUBLIC pfh_domain)

# Presentation Library
# add_library(pfh_presentation STATIC ${PFH_PRESENTATION_SOURCES})
# target_link_libraries(pfh_presentation PUBLIC pfh_application)

# =============================================================================
# Executable Target
# =============================================================================

add_executable(pfh_server
    src/bootstrap/main.cpp
)

target_link_libraries(pfh_server PRIVATE
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

target_include_directories(pfh_server PRIVATE
    ${PFH_INCLUDE_DIR}
)

# target_link_libraries(pfh_server PRIVATE
#     pfh_presentation
#     pfh_application
#     pfh_infrastructure
#     pfh_domain
#     # Drogon::Drogon
#     # spdlog::spdlog
# )

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

# Integration Tests
add_subdirectory(tests/integration)

# API Tests
# add_subdirectory(tests/api)

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
message(STATUS "=============================================================")
