add_library(diskhopper_cleaner STATIC
  src/Cleaner.cpp
)

target_include_directories(diskhopper_cleaner PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(diskhopper_cleaner PUBLIC
  diskhopper_safety
)