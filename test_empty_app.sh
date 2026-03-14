#!/bin/bash
source ~/esp/esp-idf/export.sh
source ~/esp/esp-matter/export.sh
touch CMakeLists.txt
cat <<EOT > CMakeLists.txt
cmake_minimum_required(VERSION 3.16)
include(\$ENV{IDF_PATH}/tools/cmake/project.cmake)
project(test)
EOT
mkdir -p main
cat <<EOT > main/main_dummy.c
#include <stdio.h>
void app_main(void) {}
EOT
touch main/CMakeLists.txt
cat <<EOT > main/CMakeLists.txt
idf_component_register(SRCS "main_dummy.c" INCLUDE_DIRS "")
EOT
