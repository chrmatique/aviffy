cmake_minimum_required(VERSION 3.16)
project(aviffy LANGUAGES CXX VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(AVIF REQUIRED IMPORTED_TARGET libavif)
pkg_check_modules(JPEG REQUIRED IMPORTED_TARGET libjpeg)
find_package(Threads REQUIRED)

add_executable(aviffy
  src/main.cpp
  src/convert.cpp
  src/scan.cpp
)

target_include_directories(aviffy PRIVATE src)
target_link_libraries(aviffy PRIVATE
  PkgConfig::AVIF
  PkgConfig::JPEG
  Threads::Threads
)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(aviffy PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS aviffy RUNTIME DESTINATION bin)
