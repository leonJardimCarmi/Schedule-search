cmake_minimum_required(VERSION 3.20)
project(schedule_search LANGUAGES CXX)

# ---- Language standard -------------------------------------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)   # fail instead of silently falling back
set(CMAKE_CXX_EXTENSIONS OFF)         # -std=c++17, not -std=gnu++17

# ---- Warnings, applied to our own targets only -------------------------------
function(schedule_set_warnings target)
    if(MSVC)
        # /utf-8: source files and string literals are UTF-8 (Hebrew text is safe)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# ---- Third-party dependencies (downloaded at configure time) ----------------
include(FetchContent)

# nlohmann/json: a single-header JSON library. The release archive contains
# only what is needed to build, so it downloads fast.
FetchContent_Declare(json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

# ---- Core library: everything except main() ----------------------------------
# main.cpp and the tests both link this, so the same code is compiled once
# and tested exactly as it runs.
add_library(schedule_core STATIC
    src/TimePeriod.cpp
    src/Date.cpp
    src/Employee.cpp
    src/Problem.cpp
    src/io/Diagnostics.cpp
    src/io/JsonLoader.cpp
    src/io/Printer.cpp
)
target_include_directories(schedule_core PUBLIC include)
target_link_libraries(schedule_core PUBLIC nlohmann_json::nlohmann_json)
schedule_set_warnings(schedule_core)

# ---- Executable --------------------------------------------------------------
add_executable(schedule_search src/main.cpp)
target_link_libraries(schedule_search PRIVATE schedule_core)
schedule_set_warnings(schedule_search)

# ---- Tests -------------------------------------------------------------------
option(SCHEDULE_BUILD_TESTS "Build the unit tests" ON)
if(SCHEDULE_BUILD_TESTS)
    FetchContent_Declare(Catch2
        URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.16.1.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(tests
        tests/test_timeperiod.cpp
        tests/test_date.cpp
        tests/test_loader.cpp
    )
    # Catch2WithMain supplies main() for the test runner.
    target_link_libraries(tests PRIVATE schedule_core Catch2::Catch2WithMain)
    schedule_set_warnings(tests)

    # Register every TEST_CASE with CTest, so "ctest" (or VS Test Explorer) sees them.
    include(CTest)
    include(Catch)
    catch_discover_tests(tests)
endif()
