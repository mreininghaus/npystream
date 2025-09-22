CMAKE_MINIMUM_REQUIRED(VERSION 3.23)

project(npystream LANGUAGES CXX VERSION 0.3.0)

add_library(npystream)

target_sources(npystream
  PRIVATE "src/npystream.cpp"
  PUBLIC FILE_SET HEADERS
  BASE_DIRS "include"
  FILES "include/npystream/npystream.hpp"
        "include/npystream/map_type.hpp"
        "include/npystream/tuple_util.hpp"
)

target_compile_features(npystream PUBLIC cxx_std_20)
set_property(TARGET npystream PROPERTY CXX_EXTENSIONS OFF)
target_include_directories(npystream SYSTEM INTERFACE "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

include(GNUInstallDirs)

if(MSVC)
  target_compile_options(npystream PRIVATE /W4 /WX)
else()
  target_compile_options(npystream PRIVATE -Wall -Wextra -pedantic -Wfatal-errors)
endif()

install(TARGETS npystream
  EXPORT npystream-targets
  FILE_SET HEADERS
)

install(EXPORT npystream-targets
  FILE npystream-targets.cmake
  NAMESPACE npystream::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/npystream"
)

include(CMakePackageConfigHelpers)
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/npystream-config.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/npystream"
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/npystream-version.cmake"
  COMPATIBILITY ExactVersion
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/npystream-config.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/npystream-version.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/npystream"
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

# set(CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
# include(Packing)
