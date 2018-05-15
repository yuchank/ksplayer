cmake_minimum_required(VERSION 3.10)
project(ksplayer CXX C)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -D__STDC_CONSTANT_MACROS -w -Wl,-subsystem,console")
set(SDL2_PATH "${CMAKE_SOURCE_DIR}/x86_64-w64-mingw32")
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${ksplayer_SOURCE_DIR}/cmake")
find_package(SDL2 REQUIRED)

include_directories(${CMAKE_SOURCE_DIR}/include ${SDL2_INCLUDE_DIR})
link_directories(${CMAKE_SOURCE_DIR}/lib)

set(CMAKE_CXX_STANDARD 17)
set(SOURCE_FILES src/main.cpp)

add_executable(ksplayer ${SOURCE_FILES})
target_link_libraries(ksplayer avdevice avformat avutil avcodec swscale ${SDL2_LIBRARY})