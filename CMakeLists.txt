# Copyright (c) 2018 Calvin Rose
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to
# deal in the Software without restriction, including without limitation the
# rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
# sell copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
# IN THE SOFTWARE.

cmake_minimum_required(VERSION 3.7)
project(janetui)

# Set Some Variables
set(TARGET_NAME ${PROJECT_NAME})
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_FLAGS "${CMAKE_C_CFLAGS} -fPIC")

# Set configurations
SET(CMAKE_C_FLAGS_RELWITHDEBINFO "-O2 -g")
SET(CMAKE_C_FLAGS_RELEASE "-O2")
SET(CMAKE_C_FLAGS_DEBUG  "-O0 -g")

set(SOURCES
main.c
)

# Get the header ui.h
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/libui)

# Build libui as static library
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")
add_subdirectory(libui)
set(_COMMON_CFLAGS "")
set(_COMMON_LDFLAGS "")

# Build our library
add_library(${TARGET_NAME} MODULE ${SOURCES})
target_link_libraries(${TARGET_NAME} libui glib-2.0 gtk-3 gdk-3)
