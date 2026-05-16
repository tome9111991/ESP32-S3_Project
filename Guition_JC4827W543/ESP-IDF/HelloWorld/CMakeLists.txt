# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

# Lokale Arduino-Library als ESP-IDF-Komponente einbinden.
set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../../../../libraries/LovyanGFX")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(guition_helloworld)
