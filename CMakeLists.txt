cmake_minimum_required(VERSION 3.20)
project(itch_feed_handler 
    VERSION 1.0.0
    LANGUAGES CXX
    DESCRIPTION "Zero-Copy NASDAQ TotalView-ITCH 5.0 Feed Handler"
)

# ============================================================================
# C++20 Strict Configuration
# ============================================================================
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ============================================================================
# Compiler Flags for Low-Latency
# ============================================================================
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    # Common flags for all targets
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Werror
        -O3
        -march=native
    )
    # Note: -fno-exceptions and -fno-rtti are applied per-target
    # because GoogleTest requires both RTTI and exceptions
endif()

# ============================================================================
# Include Directories
# ============================================================================
include_directories(${CMAKE_SOURCE_DIR}/include)

# ============================================================================
# Fetch Dependencies (Google Test & Google Benchmark)
# ============================================================================
include(FetchContent)

# Google Test
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

# Google Benchmark
FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.8.3
)

# pybind11 for Python bindings
# Use CMake's FindPython3 (more reliable than pybind11's legacy detection)
find_package(Python3 COMPONENTS Interpreter Development NumPy QUIET)
set(PYBIND11_FINDPYTHON ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG v2.11.1
)

# Disable benchmark tests to speed up build
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest googlebenchmark pybind11)

# ============================================================================
# Source Files (library)
# ============================================================================
# Header-only library for now, will add implementation files later
add_library(itch_parser INTERFACE)
target_include_directories(itch_parser INTERFACE ${CMAKE_SOURCE_DIR}/include)

# ============================================================================
# Main Executable (PCAP Driver)
# ============================================================================
add_executable(itch_driver
    src/main.cpp
)
target_link_libraries(itch_driver
    PRIVATE
        itch_parser
)
# HFT compile options for production code
target_compile_options(itch_driver PRIVATE -fno-exceptions -fno-rtti)

# ============================================================================
# Chronos Market Replay Engine (ITCH Parser + OrderBook Integration)
# ============================================================================
add_executable(chronos_replay
    src/replay_driver.cpp
)
target_link_libraries(chronos_replay
    PRIVATE
        itch_parser
        itch_book
)
# HFT compile options for production code
target_compile_options(chronos_replay PRIVATE -fno-exceptions -fno-rtti)
# ============================================================================
# Benchmarks
# ============================================================================
add_executable(itch_benchmark
    benchmarks/hello_benchmark.cpp
    benchmarks/itch_bench.cpp
)
target_link_libraries(itch_benchmark 
    PRIVATE 
        itch_parser
        benchmark::benchmark
        benchmark::benchmark_main
)
# HFT compile options for benchmark code
target_compile_options(itch_benchmark PRIVATE -fno-exceptions -fno-rtti)

# ============================================================================
# Python Bindings (pybind11)
# ============================================================================
pybind11_add_module(itch_handler
    src/python_bindings.cpp
)
target_link_libraries(itch_handler
    PRIVATE
        itch_parser
)
# Python module needs exceptions and RTTI enabled (override project-wide flags)
# pybind11 requires both -fexceptions and -frtti
target_compile_options(itch_handler PRIVATE -fexceptions -frtti)

# Copy Python demo script to build directory
configure_file(
    ${CMAKE_SOURCE_DIR}/python/demo.py
    ${CMAKE_BINARY_DIR}/demo.py
    COPYONLY
)

# ============================================================================
# Order Book Library (Project 2: High-Frequency Matching Engine)
# ============================================================================
# Header-only library for memory infrastructure and order book components
add_library(itch_book INTERFACE)
target_include_directories(itch_book INTERFACE ${CMAKE_SOURCE_DIR}/include)

# ============================================================================
# Tests
# ============================================================================
enable_testing()

# Memory infrastructure tests (IntrusiveList, MemPool, Order)
add_executable(itch_memory_test
    tests/memory_test.cpp
)
target_link_libraries(itch_memory_test 
    PRIVATE 
        itch_book
        GTest::gtest_main
)

add_executable(itch_tests
    tests/hello_test.cpp
    tests/message_test.cpp
    tests/parser_test.cpp
)
target_link_libraries(itch_tests 
    PRIVATE 
        itch_parser
        GTest::gtest_main
)

## Matching engine tests (OrderBook, PriceLevel)
add_executable(itch_matching_test
    tests/matching_test.cpp
)
target_link_libraries(itch_matching_test 
    PRIVATE 
        itch_book
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(itch_tests)
gtest_discover_tests(itch_memory_test)
gtest_discover_tests(itch_matching_test)

# ============================================================================
# Custom Targets
# ============================================================================
add_custom_target(run_benchmarks
    COMMAND itch_benchmark
    DEPENDS itch_benchmark
    COMMENT "Running benchmarks..."
)

add_custom_target(run_tests
    COMMAND ctest --output-on-failure
    DEPENDS itch_tests
    COMMENT "Running tests..."
)
