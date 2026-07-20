cmake_minimum_required(VERSION 3.20)
project(lob-engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-Wall -Wextra -Werror -Wpedantic)

find_package(ZLIB REQUIRED)

# enable_testing() must live here (not in a subdirectory) so that
# `ctest --test-dir <build>` can find tests registered by subdirectories.
enable_testing()

# ── Core matching engine ──────────────────────────────────────────────────────

add_library(lob
    src/order_book.cpp
    src/matching_engine.cpp
)

target_include_directories(lob
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

# ── Backtesting module ────────────────────────────────────────────────────────

add_library(lob_backtest
    src/backtest/market_event.cpp
    src/backtest/market_data_replay.cpp
    src/backtest/portfolio.cpp
    src/backtest/performance.cpp
    src/backtest/runner.cpp
    src/backtest/backtest_engine.cpp
)

target_include_directories(lob_backtest
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(lob_backtest PUBLIC lob)

# ── ITCH 5.0 parser ──────────────────────────────────────────────────────────

add_library(lob_itch
    src/itch/parser.cpp
)

target_include_directories(lob_itch
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(lob_itch
    PUBLIC  lob
    PRIVATE ZLIB::ZLIB
)

# ── Examples ──────────────────────────────────────────────────────────────────

add_executable(simple_session examples/simple_session.cpp)
target_link_libraries(simple_session PRIVATE lob)

add_executable(backtest_demo examples/backtest_demo.cpp)
target_link_libraries(backtest_demo PRIVATE lob_backtest)

add_executable(simple_backtest examples/simple_backtest.cpp)
target_link_libraries(simple_backtest PRIVATE lob_backtest)

# ── Tests and benchmarks ──────────────────────────────────────────────────────

add_subdirectory(tests)
add_subdirectory(benchmarks)
