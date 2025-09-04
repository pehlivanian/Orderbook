cmake_minimum_required(VERSION 3.22)

project(proto)

set(CMAKE_VERBOSE_MAKEFILE ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_CXX_FLAGS "-Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -mavx")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -mavx")

# gprof
if (PROFILE_RESULTS)
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg")
  SET(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -pg")
  SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -pg")
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++20 -pedantic -O3")
set(warnings "-Wall -Wparenthesis -Wno_unused_Variable -Wno_sign_compare")

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Option to enable dequeue order tracking (OFF by default for performance)
option(MAINTAIN_INTERNAL_DEQUEUE_ORDER "Enable dequeue order tracking for testing" OFF)

find_package(Boost COMPONENTS filesystem system thread chrono REQUIRED)
find_package(GTest REQUIRED)
find_package(benchmark REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(include)

# Object files (libraries)
add_library(forwardlistadaptor OBJECT forwardlistadaptor.cpp)
add_library(priorityqueueadaptor OBJECT priorityqueueadaptor.cpp)
add_library(orderstatisticstreeadaptor OBJECT orderstatisticstreeadaptor.cpp)
add_library(orderbook OBJECT orderbook.cpp)

#######
# EXECUTABLES - Main Applications
#######
add_executable(driver driver.cpp)
if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  target_compile_options(driver PRIVATE
    -fdiagnostics-show-template-tree
  )
endif()
target_compile_definitions(driver PUBLIC -DCXX_FILESYSTEM_HEADER=<boost/filesystem.hpp>)
target_compile_definitions(driver PUBLIC -DCXX_FILESYSTEM_NAMESPACE=boost::filesystem)
target_link_libraries(driver PUBLIC Boost::filesystem pthread)
set_target_properties(driver PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

add_executable(orderedqueue_driver orderedqueue_driver.cpp)
set_target_properties(orderedqueue_driver PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

#######
# TESTING - Tests and Benchmarks
#######
# Set output directory for test executables
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)

# Only build tests if dequeue order tracking is enabled
if(MAINTAIN_INTERNAL_DEQUEUE_ORDER)
  message(STATUS "Building tests with MAINTAIN_INTERNAL_DEQUEUE_ORDER enabled")
  add_executable(tests tests.cpp)
  target_compile_definitions(tests PRIVATE MAINTAIN_INTERNAL_DEQUEUE_ORDER)
  target_link_libraries(tests ${GTEST_LIBRARIES} pthread)
else()
  message(STATUS "Skipping tests - MAINTAIN_INTERNAL_DEQUEUE_ORDER is disabled")
endif()

add_executable(benchmarks benchmarks.cpp)
target_link_libraries(benchmarks Disruptor benchmark::benchmark ${Boost_LIBRARIES} pthread)

add_executable(benchmarks_new benchmarks_new.cpp)
target_link_libraries(benchmarks_new Disruptor benchmark::benchmark ${Boost_LIBRARIES} pthread)

add_executable(high_throughput_bench high_throughput_bench.cpp)
target_link_libraries(high_throughput_bench pthread)
