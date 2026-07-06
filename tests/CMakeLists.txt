include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP ON
)
# Prevent GoogleTest from overriding our compiler/linker options on Windows.
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(lob_tests
    order_book_test.cpp
    matching_engine_test.cpp
)

target_link_libraries(lob_tests
    PRIVATE
        lob
        GTest::gtest_main
)

target_include_directories(lob_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)

include(GoogleTest)
gtest_discover_tests(lob_tests)
