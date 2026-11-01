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

# ---- Core library: everything except main() ----------------------------------
# main.cpp and the tests both link this, so the same code is compiled once
# and tested exactly as it runs.
add_library(schedule_core STATIC
    src/TimePeriod.cpp
)
target_include_directories(schedule_core PUBLIC include)
schedule_set_warnings(schedule_core)

# ---- Executable --------------------------------------------------------------
add_executable(schedule_search src/main.cpp)
target_link_libraries(schedule_search PRIVATE schedule_core)
schedule_set_warnings(schedule_search)
