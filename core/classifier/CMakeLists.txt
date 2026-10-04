add_library(diskhopper_classifier STATIC
  src/Classifier.cpp
)

target_include_directories(diskhopper_classifier PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(diskhopper_classifier PUBLIC
  diskhopper_scanner
  diskhopper_rules
)