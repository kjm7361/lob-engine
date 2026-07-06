cmake_minimum_required(VERSION 3.20)
project(lob-engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Warnings as errors.
add_compile_options(-Wall -Wextra -Werror -Wpedantic)

# ── Library ───────────────────────────────────────────────────────────────────
add_library(lob
    src/order_book.cpp
    src/matching_engine.cpp
)

target_include_directories(lob
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# ── Example ───────────────────────────────────────────────────────────────────
add_executable(simple_session examples/simple_session.cpp)
target_link_libraries(simple_session PRIVATE lob)

# ── Tests ─────────────────────────────────────────────────────────────────────
add_subdirectory(tests)
