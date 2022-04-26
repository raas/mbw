cmake_minimum_required(VERSION 3.10)

project(mbw VERSION 1.5)

add_executable(mbw mbw.c)
install(TARGETS mbw DESTINATION bin)
