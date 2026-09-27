cmake_minimum_required(VERSION 3.16)

# This folder is inside a separate workspace repo right now. Stop Git checks
# for downloaded components from walking up into that parent repo. If this
# project gets its own .git folder, Git can still find it.
set(ENV{GIT_CEILING_DIRECTORIES} "${CMAKE_CURRENT_LIST_DIR}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Build the sensor app and only what it depends on. Arduino lists a lot of
# extra features, but these tests don't need RainMaker, Zigbee, speech, etc.
set(COMPONENTS main)
project(solaryou_sensor)
