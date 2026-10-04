add_executable(core_tests
  core_tests.cpp
)

target_link_libraries(core_tests PRIVATE
  diskhopper_classifier
  diskhopper_scanner
  diskhopper_rules
)

add_test(NAME core_tests COMMAND core_tests)