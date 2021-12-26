cmake_minimum_required(VERSION 3.0)
project(hwmond LANGUAGES C)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wno-long-long -pedantic")
set(CMAKE_C_FLAGS_DEBUG "-g")
set(CMAKE_C_FLAGS_RELEASE "-Os")

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
