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

# Find packages (will be configured as dependencies are added)
# find_package(Drogon REQUIRED)
# find_package(PostgreSQL REQUIRED)
find_package(spdlog REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(GTest REQUIRED)

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
    # Core types (header-only for now)
    # src/domain/typed_id.cpp  # Header-only
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

# Domain Library (no external dependencies)
# add_library(pfh_domain STATIC ${PFH_DOMAIN_SOURCES})
# target_include_directories(pfh_domain PUBLIC ${PFH_INCLUDE_DIR})

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

# Integration Tests
# add_subdirectory(tests/integration)

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
