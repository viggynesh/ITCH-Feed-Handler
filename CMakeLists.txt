cmake_minimum_required(VERSION 3.20)
project(itch_feed_handler LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to a Release build - this project is about performance, running it
# unoptimized gives meaningless numbers.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
    message(STATUS "No build type set, defaulting to Release")
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# -march=native matters a lot here (SSE/AVX + tuned scheduling). Not every
# compiler/arch spells it the same way, so probe before adding it.
include(CheckCXXCompilerFlag)
check_cxx_compiler_flag("-march=native" HAS_MARCH_NATIVE)
if(HAS_MARCH_NATIVE)
    add_compile_options($<$<CONFIG:Release>:-march=native>)
else()
    check_cxx_compiler_flag("-mcpu=native" HAS_MCPU_NATIVE)
    if(HAS_MCPU_NATIVE)
        add_compile_options($<$<CONFIG:Release>:-mcpu=native>)
    endif()
endif()

add_compile_options(-Wall -Wextra)

# ---- core library (parser + both order books + timing helpers) -------------
add_library(itch_core
    src/baseline_book.cpp
    src/optimized_book.cpp
    src/tsc.cpp)
target_include_directories(itch_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(itch_core PUBLIC cxx_std_20)

# ---- executables -----------------------------------------------------------
add_executable(baseline_book apps/baseline_main.cpp)
target_link_libraries(baseline_book PRIVATE itch_core)

add_executable(optimized_book apps/optimized_main.cpp)
target_link_libraries(optimized_book PRIVATE itch_core)

add_executable(run_benchmark benchmarks/run_benchmark.cpp)
target_link_libraries(run_benchmark PRIVATE itch_core)

add_executable(gen_synthetic tools/gen_synthetic.cpp)
target_link_libraries(gen_synthetic PRIVATE itch_core)

# ---- tests -----------------------------------------------------------------
option(BUILD_TESTING "Build unit tests (pulls GoogleTest via FetchContent)" ON)
if(BUILD_TESTING)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_executable(unit_tests
        tests/test_parser.cpp
        tests/test_book.cpp
        tests/test_consistency.cpp)
    target_link_libraries(unit_tests PRIVATE itch_core GTest::gtest_main)
    include(GoogleTest)
    gtest_discover_tests(unit_tests)
endif()
