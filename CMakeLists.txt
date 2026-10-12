# ###############################################################################
# MIT License
#
# Copyright (c) 2025 Nico Trost
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
# ###############################################################################

cmake_minimum_required(VERSION 3.22)
project(WandererCoverSDK VERSION 1.5.0 DESCRIPTION "Wanderer Cover SDK for telescope control")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

option(WANDERER_COVER_BUILD_TESTS "Build the interactive test program" ON)

# std::thread needs pthreads linked explicitly on glibc < 2.34
find_package(Threads REQUIRED)

# Find libudev package
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBUDEV libudev)

if(NOT LIBUDEV_FOUND)
	message(FATAL_ERROR "libudev not found. Install it with: sudo apt-get install libudev-dev")
endif()

# SDK Library - only the C API in WandererCoverSDK.h is exported
add_library(WandererCoverSDK SHARED
	WandererCoverSDK.cpp
	WandererCoverLogging.cpp
	WandererCoverSerialPort.cpp
	WandererCoverDevice.cpp
	WandererCoverProtocol.cpp)
target_include_directories(WandererCoverSDK PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(WandererCoverSDK PRIVATE ${LIBUDEV_INCLUDE_DIRS})
target_link_libraries(WandererCoverSDK PRIVATE Threads::Threads ${LIBUDEV_LIBRARIES})
target_compile_definitions(WandererCoverSDK PRIVATE WANDERER_COVER_SDK_BUILD)
target_compile_options(WandererCoverSDK PRIVATE -Wall -Wextra)
set_target_properties(WandererCoverSDK PROPERTIES
	CXX_VISIBILITY_PRESET hidden
	VISIBILITY_INLINES_HIDDEN ON
	VERSION ${PROJECT_VERSION}
	SOVERSION ${PROJECT_VERSION_MAJOR})

if(WANDERER_COVER_BUILD_TESTS)
	# Interactive test against real hardware
	add_executable(test_wanderer_cover test_wanderer_cover.cpp)
	target_link_libraries(test_wanderer_cover WandererCoverSDK)
endif()

# Installation
install(TARGETS WandererCoverSDK
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
)
install(FILES WandererCoverSDK.h DESTINATION include)
# CPack configuration for Debian package generation
set(CPACK_GENERATOR "DEB")
set(CPACK_PACKAGE_CONTACT "Nico")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/nitr57/WandererCoverSDK")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "Nico")
set(CPACK_DEBIAN_PACKAGE_DESCRIPTION_SUMMARY "SDK for controlling Wanderer Cover devices via serial communication")

# Runtime dependencies
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libudev1")

# License
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")

include(CPack)
