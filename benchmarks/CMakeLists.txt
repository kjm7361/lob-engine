add_executable(lob_bench lob_bench.cpp)

target_link_libraries(lob_bench PRIVATE lob)

target_include_directories(lob_bench
    PRIVATE
        ${CMAKE_SOURCE_DIR}/include
)
