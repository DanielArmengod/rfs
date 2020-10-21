cmake_minimum_required(VERSION 3.16)
project(elmeutfg C)

set(CMAKE_C_STANDARD 11)

add_executable(rfsMaster rfsMaster.c rfsCommon.c)

add_executable(rfsSlave rfsSlave.c rfsCommon.c)