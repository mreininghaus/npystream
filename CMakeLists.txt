CMAKE_MINIMUM_REQUIRED(VERSION 3.10)

project(npystream LANGUAGES CXX VERSION 0.2.0)

add_library(npystream "src/npystream.cpp"
  "include/npystream/npystream.hpp"
  "include/npystream/map_type.hpp"
  "include/npystream/tuple_util.hpp"
)

include(GNUInstallDirs)

target_compile_features(npystream PUBLIC cxx_std_20)
set_property(TARGET npystream PROPERTY CXX_EXTENSIONS OFF)
target_include_directories(npystream SYSTEM PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>")
target_include_directories(npystream SYSTEM INTERFACE "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

if(MSVC)
  target_compile_options(npystream PRIVATE /W4 /WX)
else()
  target_compile_options(npystream PRIVATE -Wall -Wextra -pedantic -Wfatal-errors)
endif()

install(TARGETS npystream
  EXPORT npystream-targets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(FILES
  "include/npystream/npystream.hpp"
  "include/npystream/map_type.hpp"
  "include/npystream/tuple_util.hpp"
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/npystream
)
    
install(EXPORT npystream-targets
  FILE npystream-targets.cmake
  NAMESPACE npystream::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/npystream
)

include(CMakePackageConfigHelpers)
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/npystream-config.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/npystream
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/npystream-config.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/npystream
)

option (NPYSTREAM_BUILD_EXAMPLE "build npystream example" OFF)
if (NPYSTREAM_BUILD_EXAMPLE)
  add_executable(stream "examples/stream.cpp")
  target_link_libraries(stream npystream)
  if(MSVC)
    target_compile_options(stream PRIVATE /W4 /WX)
  else()
    target_compile_options(stream PRIVATE -Wall -Wextra -pedantic -Wfatal-errors)
  endif()
endif()
