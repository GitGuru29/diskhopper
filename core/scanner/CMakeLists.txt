add_library(diskhopper_scanner STATIC
  src/Scanner.cpp
)

target_include_directories(diskhopper_scanner PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(diskhopper_scanner PUBLIC diskhopper_safety)