cmake_minimum_required(VERSION 3.18)

project(epoll_coroutine)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O2")
set_source_files_properties(coroutines.c  PROPERTIES COMPILE_FLAGS  "-D_FORTIFY_SOURCE=0")
add_executable(
        epoll_coroutine
        main.c
        coroutines.c
)