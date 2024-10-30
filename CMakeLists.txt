# Automated home irrigation based on weather data provided by CIMIS weather stations (https://cimis.water.ca.gov/Default.aspx).
# Copyright (C) 2024  Natalie C. Pueyo Svoboda

# Set the minimum required version of CMake 
cmake_minimum_required(VERSION 3.10)

# Project name and version 
project(Irrigation VERSION 1.0)

# Set the desired C or C++ standard using either C_STANDARD <STANDARD> or CMAKE_CXX_STANDARD <STANDARD>
# and the variable CMAKE_CXX_STANDARD_REQUIRED to True
set(C_STANDARD 11)
# set(CMAKE_CXX_STANDARD_REQUIRED True)

# Use configure_file to configure and copy TutorialConfig.h.in to TutorialConfig.h
configure_file(IrrigationConfig.h.in IrrigationConfig.h)

# needed to use curl as a static lib
add_definitions(-DCURL_STATICLIB)

# Add an executable called <NAME> to the project
# Be sure to specify the correct source file (e.g. tutorial.cxx for C++ or tutorial.c for C)
# add_executable(Irrigation ${SOURCES})
add_executable(Irrigation irrigation.c)

list(APPEND CMAKE_PREFIX_PATH "/usr/local/lib/cmake/jansson/")
message("CMAKE_PREFIX_PATH = ${CMAKE_PREFIX_PATH}")

# if adding more packages, rememeber to make and install even after cmake build
# package names are case sensitive
find_package(jansson)
target_link_libraries(Irrigation PUBLIC jansson)

# for curl, see https://discourse.cmake.org/t/trying-to-statically-link-curl-directory-for-cmake-on-linux-undefined-reference-to-curl-easy-init/8835/2
find_package(CURL REQUIRED)
target_link_libraries(Irrigation PUBLIC CURL::libcurl) 

# for mosquitto, the MQTT broker, find_library didn't work so had to manually add it
# did not work when using the snap installation of mosquitto, had to install through apt
# requires:
# $ sudo apt install mosquitto mosquitto-clients libmosquitto-dev
find_library(Mosquitto_libs mosquitto)
target_link_libraries(Irrigation PUBLIC ${Mosquitto_libs})

# Use target_include_directories to include ${PROJECT_BINARY_DIR}
target_include_directories(Irrigation PUBLIC 
                            "${PROJECT_BINARY_DIR}"
                            )




