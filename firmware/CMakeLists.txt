# NEXUS CSI Sensor - top level ESP-IDF project file
#
# Build with:  idf.py set-target esp32 && idf.py build
#
cmake_minimum_required(VERSION 3.16)

# Custom partition table lives next to this file.
set(PROJECT_VER "1.0.0")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(nexus_csi_sensor)
