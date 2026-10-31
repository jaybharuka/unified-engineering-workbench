cmake_minimum_required(VERSION 3.16)
project(UnifiedEngineeringWorkbench VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(WB_BUILD_TESTS "Build unit tests" ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /utf-8)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Flags string is reported in the Performance report (compiler + optimisation flags).
set(WB_FLAGS_REPORT "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}}")
string(STRIP "${WB_FLAGS_REPORT}" WB_FLAGS_REPORT)

# ---- core library: no HTTP, no UI, no globals (src/core) ----
add_library(wb_core STATIC
  src/core/json.cpp
  src/core/util.cpp
  src/core/fea1d.cpp
  src/core/stl_parser.cpp
  src/core/ansys_csv.cpp
  src/core/thread_pool.cpp
  src/core/batch.cpp
  src/core/sysinfo.cpp
  src/core/hardware.cpp
  src/core/backprop.cpp
  src/core/samples.cpp)
target_include_directories(wb_core PUBLIC src/core)
target_compile_definitions(wb_core PRIVATE WB_FLAGS_REPORT="${WB_FLAGS_REPORT}"
                                          WB_BUILD_TYPE="${CMAKE_BUILD_TYPE}")
find_package(Threads REQUIRED)
target_link_libraries(wb_core PUBLIC Threads::Threads)

# ---- HTTP transport + job control (src/server) ----
add_executable(wb_server src/server/main.cpp src/server/jobs.cpp src/server/api.cpp)
target_include_directories(wb_server PRIVATE third_party src/server)
target_link_libraries(wb_server PRIVATE wb_core)

# ---- command-line tool: regenerates results/ and sample data ----
add_executable(wb_cli src/cli/main.cpp)
target_link_libraries(wb_cli PRIVATE wb_core)

if(WIN32)
  target_compile_definitions(wb_server PRIVATE _WIN32_WINNT=0x0A00)
  target_link_libraries(wb_server PRIVATE ws2_32)
  add_executable(Launch WIN32 src/launcher/launcher.cpp)
  target_compile_definitions(Launch PRIVATE _WIN32_WINNT=0x0A00 UNICODE _UNICODE)
  target_link_libraries(Launch PRIVATE ws2_32 shell32 user32)
  if(MINGW)
    foreach(t wb_server wb_cli Launch)
      target_link_options(${t} PRIVATE -static -static-libgcc -static-libstdc++)
    endforeach()
  endif()
endif()

if(WB_BUILD_TESTS)
  enable_testing()
  add_executable(wb_tests
    tests/test_main.cpp tests/test_fea.cpp tests/test_parsers.cpp
    tests/test_batch.cpp tests/test_hardware.cpp tests/test_backprop.cpp tests/test_json.cpp)
  target_include_directories(wb_tests PRIVATE tests)
  target_link_libraries(wb_tests PRIVATE wb_core)
  if(MINGW)
    target_link_options(wb_tests PRIVATE -static -static-libgcc -static-libstdc++)
  endif()
  add_test(NAME unit COMMAND wb_tests WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
