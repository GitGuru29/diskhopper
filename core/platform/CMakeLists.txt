add_library(diskhopper_platform STATIC
  src/UserPaths.cpp
  src/Trash.cpp
)

target_include_directories(diskhopper_platform PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

find_library(CORESERVICES_FRAMEWORK CoreServices REQUIRED)
target_link_libraries(diskhopper_platform PRIVATE
  ${CORESERVICES_FRAMEWORK}
)
target_compile_options(diskhopper_platform PRIVATE
  -Wno-deprecated-declarations
)