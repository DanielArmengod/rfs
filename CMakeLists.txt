cmake_minimum_required(VERSION 3.16)
project(elmeutfg C)

set(CMAKE_C_STANDARD 11)

add_executable(rfsMaster_m2 rfsMaster_m2.c rfsCommon.c)

add_executable(rfsSlave_m2 rfsSlave_m2.c rfsCommon.c)