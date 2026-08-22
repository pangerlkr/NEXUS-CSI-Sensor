# NEXUS CSI Sensor - on-target unit & stress test application
#
# Build/flash/run with:
#   idf.py -C firmware/test set-target esp32
#   idf.py -C firmware/test flash monitor
#
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nexus_tests)
