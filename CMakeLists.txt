cmake_minimum_required(VERSION 3.10)
project(ksplayer CXX C)

SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D__STDC_CONSTANT_MACROS")

include_directories(${CMAKE_SOURCE_DIR}/include)
link_directories(${CMAKE_SOURCE_DIR}/lib)

set(CMAKE_CXX_STANDARD 17)

add_executable(ksplayer src/main.cpp)
target_link_libraries(ksplayer avdevice avformat avutil)