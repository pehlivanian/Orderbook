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

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17 -pedantic -O3")
set(warnings "-Wall -Wparenthesis -Wno_unused_Variable -Wno_sign_compare")

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Boost COMPONENTS filesystem)
find_package(GTest REQUIRED)

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(include)

# object files
add_library(forwardlistadaptor OBJECT forwardlistadaptor.cpp)
add_library(priorityqueueadaptor OBJECT priorityqueueadaptor.cpp)
add_library(orderstatisticstreeadaptor OBJECT orderstatisticstreeadaptor.cpp)
add_library(orderbook OBJECT orderbook.cpp)

add_executable(driver driver.cpp)
target_compile_definitions(driver PUBLIC -DCXX_FILESYSTEM_HEADER=<boost/filesystem.hpp>)
target_compile_definitions(driver PUBLIC -DCXX_FILESYSTEM_NAMESPACE=boost::filesystem)
target_link_libraries(driver PUBLIC Boost::filesystem pthread)

add_executable(orderedqueue_driver orderedqueue_driver.cpp)

#######
# TESTS
#######
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
add_executable(tests tests.cpp)
target_link_libraries(tests ${GTEST_LIBRARIES} pthread)






