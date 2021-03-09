cmake_minimum_required(VERSION 3.0)
project(hwmond LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(SOURCE_FILES
        hwmond.c cpu_usage.c cpu_usage.h)

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_SOURCE_DIR}/modules")

find_package(LibUSB REQUIRED)

add_executable(hwmond ${SOURCE_FILES})

if (APPLE)
    find_library(CoreFoundation CoreFoundation)
    find_library(IOKit IOKit)
    target_link_libraries(hwmond ${CoreFoundation} ${IOKit} objc)
endif()

include_directories(${LIBUSB_INCLUDE_DIR})
target_link_libraries(hwmond ${LIBUSB_LIBRARY})
target_link_libraries(hwmond m)
target_link_libraries(hwmond pthread)
