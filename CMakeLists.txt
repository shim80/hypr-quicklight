cmake_minimum_required(VERSION 3.20)
project(hypr-quiklight LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)

pkg_check_modules(HIDAPI REQUIRED hidapi-hidraw)
pkg_check_modules(WAYLAND_CLIENT REQUIRED wayland-client)
pkg_check_modules(WAYLAND_PROTOCOLS REQUIRED wayland-protocols)
pkg_check_modules(WAYLANDPP REQUIRED wayland-client++)

find_program(WAYLAND_SCANNERPP wayland-scanner++ REQUIRED)

set(PROTO_XML_EXT_COPY   ${CMAKE_SOURCE_DIR}/protocols/ext-image-copy-capture-v1.xml)
set(PROTO_XML_EXT_SOURCE ${CMAKE_SOURCE_DIR}/protocols/ext-image-capture-source-v1.xml)

set(GEN_EXT_COPY_HPP   ${CMAKE_BINARY_DIR}/wayland-client-ext-image-copy-capture.hpp)
set(GEN_EXT_COPY_CPP   ${CMAKE_BINARY_DIR}/wayland-client-ext-image-copy-capture.cpp)
set(GEN_EXT_SOURCE_HPP ${CMAKE_BINARY_DIR}/wayland-client-ext-image-capture-source.hpp)
set(GEN_EXT_SOURCE_CPP ${CMAKE_BINARY_DIR}/wayland-client-ext-image-capture-source.cpp)

add_custom_command(
  OUTPUT ${GEN_EXT_SOURCE_HPP} ${GEN_EXT_SOURCE_CPP}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/cmake/waylandpp-extra-source.hpp
          ${CMAKE_BINARY_DIR}/waylandpp-extra-source.hpp
  COMMAND ${WAYLAND_SCANNERPP} -x waylandpp-extra-source.hpp
          ${PROTO_XML_EXT_SOURCE}
          ${GEN_EXT_SOURCE_HPP}
          ${GEN_EXT_SOURCE_CPP}
  DEPENDS
          ${PROTO_XML_EXT_SOURCE}
          ${CMAKE_SOURCE_DIR}/cmake/waylandpp-extra-source.hpp
  VERBATIM
)

add_custom_command(
  OUTPUT ${GEN_EXT_COPY_HPP} ${GEN_EXT_COPY_CPP}
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/cmake/waylandpp-extra-copy.hpp
          ${CMAKE_BINARY_DIR}/waylandpp-extra-copy.hpp
  COMMAND ${WAYLAND_SCANNERPP} -x waylandpp-extra-copy.hpp
          ${PROTO_XML_EXT_COPY}
          ${GEN_EXT_COPY_HPP}
          ${GEN_EXT_COPY_CPP}
  DEPENDS
          ${PROTO_XML_EXT_COPY}
          ${GEN_EXT_SOURCE_HPP}
          ${CMAKE_SOURCE_DIR}/cmake/waylandpp-extra-copy.hpp
  VERBATIM
)

add_library(ext-image-protocols STATIC
  ${GEN_EXT_COPY_CPP}
  ${GEN_EXT_SOURCE_CPP}
)

target_include_directories(ext-image-protocols PUBLIC
  ${CMAKE_BINARY_DIR}
  ${WAYLANDPP_INCLUDE_DIRS}
)

target_compile_options(ext-image-protocols PRIVATE
  ${WAYLANDPP_CFLAGS_OTHER}
)

target_link_directories(ext-image-protocols PUBLIC
  ${WAYLANDPP_LIBRARY_DIRS}
)

target_link_libraries(ext-image-protocols PUBLIC
  ${WAYLANDPP_LIBRARIES}
)

add_executable(hypr-quiklight
  src/main.cpp
  src/quiklight_hid.cpp
  wayland/capture.cpp
)

target_include_directories(hypr-quiklight PRIVATE
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_BINARY_DIR}
  ${HIDAPI_INCLUDE_DIRS}
  ${WAYLAND_CLIENT_INCLUDE_DIRS}
  ${WAYLANDPP_INCLUDE_DIRS}
)

target_compile_options(hypr-quiklight PRIVATE
  -Wall -Wextra -Wpedantic
  ${HIDAPI_CFLAGS_OTHER}
  ${WAYLAND_CLIENT_CFLAGS_OTHER}
  ${WAYLANDPP_CFLAGS_OTHER}
)

target_link_directories(hypr-quiklight PRIVATE
  ${HIDAPI_LIBRARY_DIRS}
  ${WAYLAND_CLIENT_LIBRARY_DIRS}
  ${WAYLANDPP_LIBRARY_DIRS}
)

target_link_libraries(hypr-quiklight PRIVATE
  ext-image-protocols
  ${HIDAPI_LIBRARIES}
  ${WAYLAND_CLIENT_LIBRARIES}
  ${WAYLANDPP_LIBRARIES}
)
