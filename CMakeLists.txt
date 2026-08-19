cmake_minimum_required(VERSION 3.20)
project(xiu C CXX ASM)

add_subdirectory(boot)
add_subdirectory(kernel)
add_subdirectory(drivers)
add_subdirectory(usr)
