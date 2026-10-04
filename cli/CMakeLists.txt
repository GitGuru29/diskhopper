add_executable(diskhopper
  main.cpp
)

target_link_libraries(diskhopper PRIVATE diskhopper_scanner)