# Personal Finance Hub - Dependency Resolution
# Version: 1.0
#
# Strategy:
#   1. Prefer find_package (vcpkg / system packages) when available.
#   2. Fall back to FetchContent so the project builds out-of-the-box without
#      a package manager. This keeps the domain layer dependency-free while
#      giving infrastructure/tests spdlog, nlohmann_json and GoogleTest.
#
# Set PFH_FORCE_FETCHCONTENT=ON to always fetch (useful for reproducible CI).

include(FetchContent)

option(PFH_FORCE_FETCHCONTENT "Always use FetchContent instead of find_package" OFF)

# --- GoogleTest -------------------------------------------------------------
if(NOT PFH_FORCE_FETCHCONTENT)
    find_package(GTest QUIET)
endif()

if(NOT GTest_FOUND)
    message(STATUS "PFH: fetching GoogleTest via FetchContent")
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE
    )
    # Prevent GoogleTest from overriding compiler/linker options on Windows.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

# --- spdlog -----------------------------------------------------------------
if(NOT PFH_FORCE_FETCHCONTENT)
    find_package(spdlog QUIET)
endif()

if(NOT spdlog_FOUND)
    message(STATUS "PFH: fetching spdlog via FetchContent")
    FetchContent_Declare(
        spdlog
        GIT_REPOSITORY https://github.com/gabime/spdlog.git
        GIT_TAG v1.14.1
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(spdlog)
endif()

# --- nlohmann_json ----------------------------------------------------------
if(NOT PFH_FORCE_FETCHCONTENT)
    find_package(nlohmann_json QUIET)
endif()

if(NOT nlohmann_json_FOUND)
    message(STATUS "PFH: fetching nlohmann_json via FetchContent")
    FetchContent_Declare(
        nlohmann_json
        GIT_REPOSITORY https://github.com/nlohmann/json.git
        GIT_TAG v3.11.3
        GIT_SHALLOW TRUE
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()
