add_library(diskhopper_platform STATIC
  src/UserPaths.cpp
)

target_include_directories(diskhopper_platform PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)