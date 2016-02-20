cmake_minimum_required(VERSION 3.2)
project(mbw)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(SOURCE_FILES
    mbw.c)

add_executable(mbw ${SOURCE_FILES})
target_link_libraries(mbw Threads::Threads)
