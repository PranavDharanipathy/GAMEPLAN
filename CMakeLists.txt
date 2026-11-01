cmake_minimum_required(VERSION 3.20)
project(GamePlan VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# Determinism: never let the compiler contract/reassociate floating point.
if(MSVC)
  add_compile_options(/W4 /fp:precise /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter -ffp-contract=off -fno-fast-math)
endif()

find_package(ZLIB)
find_package(Threads REQUIRED)

file(GLOB GP_CORE_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/core/src/*.cpp)
add_library(gameplan_core STATIC ${GP_CORE_SOURCES})
target_include_directories(gameplan_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/core/include)
target_link_libraries(gameplan_core PUBLIC Threads::Threads)
if(ZLIB_FOUND)
  target_link_libraries(gameplan_core PUBLIC ZLIB::ZLIB)
  target_compile_definitions(gameplan_core PUBLIC GP_HAVE_ZLIB=1)
endif()
target_compile_definitions(gameplan_core PUBLIC GP_SOURCE_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")

add_executable(gp-sim apps/gpsim/main.cpp)
target_link_libraries(gp-sim PRIVATE gameplan_core)

enable_testing()
file(GLOB GP_TEST_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/core/tests/test_*.cpp)
add_executable(gp-tests core/tests/test_main.cpp ${GP_TEST_SOURCES})
target_link_libraries(gp-tests PRIVATE gameplan_core)
add_test(NAME core-unit COMMAND gp-tests WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
