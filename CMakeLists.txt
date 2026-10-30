cmake_minimum_required(VERSION 3.20)
project(arena_platform CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(ENABLE_SANITIZERS "Build with ASan + UBSan" OFF)
option(BUILD_TESTS "Build unit tests" ON)

add_compile_options(-Wall -Wextra -Wpedantic)
if(ENABLE_SANITIZERS)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

include(FetchContent)
FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git GIT_TAG v0.18.3 GIT_SHALLOW TRUE)
FetchContent_Declare(json
  GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.11.3 GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(httplib json)

find_package(Threads REQUIRED)

# Pure logic (no I/O) -> unit-testable
add_library(core
  src/matchmaker/match_queue.cpp
  src/matchmaker/server_registry.cpp
  src/gameserver/world.cpp)
target_include_directories(core PUBLIC src)

add_executable(matchmaker src/matchmaker/main.cpp)
target_link_libraries(matchmaker PRIVATE core httplib::httplib nlohmann_json::nlohmann_json Threads::Threads)

add_executable(gameserver src/gameserver/main.cpp)
target_link_libraries(gameserver PRIVATE core httplib::httplib nlohmann_json::nlohmann_json Threads::Threads)

add_executable(bot src/client/bot.cpp)
target_link_libraries(bot PRIVATE core httplib::httplib nlohmann_json::nlohmann_json Threads::Threads)

if(BUILD_TESTS)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git GIT_TAG v1.15.2 GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(googletest)
  enable_testing()
  add_executable(unit_tests
    tests/match_queue_test.cpp
    tests/server_registry_test.cpp
    tests/world_test.cpp
    tests/protocol_test.cpp)
  target_link_libraries(unit_tests PRIVATE core GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(unit_tests)
endif()
