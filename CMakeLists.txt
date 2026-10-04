cmake_minimum_required(VERSION 3.20)
project(diskhopper LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_subdirectory(core)
add_subdirectory(cli)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()