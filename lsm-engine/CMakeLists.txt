cmake_minimum_required(VERSION 3.16)
project(lsm_engine VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------------------------
# Compiler options
# ---------------------------------------------------------------------------
if(MSVC)
    add_compile_options(/W4 /WX- /permissive- /utf-8)
    # Suppress MSVC warning about POSIX deprecation (we handle it via #ifdef)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O2)
endif()

# ---------------------------------------------------------------------------
# Core library (all engine source files)
# ---------------------------------------------------------------------------
add_library(lsm_core STATIC
    src/bloom_filter.cpp
    src/memtable.cpp
    src/wal.cpp
    src/sstable.cpp
    src/compaction.cpp
    src/lsm_engine.cpp
)
target_include_directories(lsm_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# ---------------------------------------------------------------------------
# Google Test via FetchContent
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.14.0
)
# Prevent GTest from overriding our compiler/linker settings on Windows
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

enable_testing()

# ---------------------------------------------------------------------------
# Test executables
# ---------------------------------------------------------------------------
function(add_lsm_test name)
    add_executable(${name} tests/${name}.cpp)
    target_link_libraries(${name} PRIVATE lsm_core GTest::gtest_main)
    add_test(NAME ${name} COMMAND ${name})
    # Increase timeout for the 1M-key integration test
    set_tests_properties(${name} PROPERTIES TIMEOUT 300)
endfunction()

add_lsm_test(test_memtable)
add_lsm_test(test_bloom_filter)
add_lsm_test(test_sstable)
add_lsm_test(test_compaction)
add_lsm_test(test_engine)

# ---------------------------------------------------------------------------
# Benchmark binary
# ---------------------------------------------------------------------------
add_executable(lsm_benchmark bench/benchmark.cpp)
target_link_libraries(lsm_benchmark PRIVATE lsm_core)

# ---------------------------------------------------------------------------
# Installation (optional)
# ---------------------------------------------------------------------------
install(TARGETS lsm_core ARCHIVE DESTINATION lib)
install(DIRECTORY src/ DESTINATION include/lsm_engine
        FILES_MATCHING PATTERN "*.h")
