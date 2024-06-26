cmake_minimum_required(VERSION 3.14.1)

project(exec_on_macos C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(exec "exec.cc")
