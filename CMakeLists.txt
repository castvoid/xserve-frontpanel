cmake_minimum_required(VERSION 3.0)
project(hwmond)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wno-long-long -pedantic")

set(SOURCE_FILES
        hwmond.c)

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_SOURCE_DIR}/modules")

find_package(LibUSB REQUIRED)

add_executable(hwmond ${SOURCE_FILES})

if (APPLE)
    find_library(CoreFoundation CoreFoundation)
    find_library(IOKit IOKit)
    target_link_libraries(hwmond ${CoreFoundation} ${IOKit})
endif()

include_directories(${LIBUSB_INCLUDE_DIR})
target_link_libraries(hwmond ${LIBUSB_LIBRARY})
target_link_libraries(hwmond m)
