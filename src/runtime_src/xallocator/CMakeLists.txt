add_subdirectory(include)
include_directories(
  ${BOOST_FILESYSTEM_INCLUDE_DIRS}
  ${BOOST_SYSTEM_INCLUDE_DIRS}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  )

file(GLOB XALLOCATOR_FILES
  "*.h"
  "*.cpp"
  )

set(XALLOCATOR_ALL_SRC
  ${XALLOCATOR_FILES}
  )

add_library(xallocatorobj OBJECT ${XALLOCATOR_ALL_SRC})

