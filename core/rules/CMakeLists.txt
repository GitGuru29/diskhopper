add_library(diskhopper_rules STATIC
  src/DefaultRules.cpp
)

target_include_directories(diskhopper_rules PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(diskhopper_rules PUBLIC diskhopper_safety)