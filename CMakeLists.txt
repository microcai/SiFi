cmake_minimum_required(VERSION 3.0)

project(sifi)

if( ${CMAKE_BUILD_TYPE} STREQUAL "Debug")

set(CMAKE_CXX_FLAGS "-O0 -g3")
set(CMAKE_C_FLAGS "-O0 -g3")


endif()

find_package(Threads REQUIRED)
find_package(Boost REQUIRED COMPONENTS coroutine system)
find_package(PulseAudio REQUIRED)

find_library(PULSEAUDIO_SIMPLE_LIBRARY NAMES pulse-simple)
find_library(FFTW_LIBRARY NAMES fftw3)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++1y")

add_executable(sifi main.cpp)

target_link_libraries(sifi ${Boost_LIBRARIES} ${PULSEAUDIO_LIBRARY} ${PULSEAUDIO_SIMPLE_LIBRARY} ${FFTW_LIBRARY} ${CMAKE_THREAD_LIBS_INIT})

install(TARGETS sifi RUNTIME DESTINATION bin)
