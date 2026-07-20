include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ── Core engine tests ─────────────────────────────────────────────────────────

add_executable(lob_tests
    order_book_test.cpp
    matching_engine_test.cpp
    matching_engine_t_test.cpp
)

target_link_libraries(lob_tests
    PRIVATE
        lob
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(lob_tests)

# ── Backtest module tests ─────────────────────────────────────────────────────

add_executable(backtest_tests
    backtest/backtest_test.cpp
)

target_link_libraries(backtest_tests
    PRIVATE
        lob_backtest
        GTest::gtest_main
)

gtest_discover_tests(backtest_tests)

# ── ITCH parser tests ─────────────────────────────────────────────────────────

add_executable(itch_tests
    itch/parser_test.cpp
)

target_link_libraries(itch_tests
    PRIVATE
        lob_itch
        GTest::gtest_main
)

gtest_discover_tests(itch_tests)

# ── Replay driver tests ───────────────────────────────────────────────────────

add_executable(replay_tests
    replay/replay_test.cpp
)

target_link_libraries(replay_tests
    PRIVATE
        lob_replay
        GTest::gtest_main
)

gtest_discover_tests(replay_tests)
