# If not stated otherwise in this file or this component's LICENSE file the
# following copyright and licenses apply:
#
# Copyright 2024 RDK Management
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set(PLUGIN_NAME GStreamerPlayer)
set(MODULE_NAME ${NAMESPACE}${PLUGIN_NAME})
set(PLUGIN_IMPLEMENTATION ${MODULE_NAME}Implementation)

set(PLUGIN_GSTREAMERPLAYER_AUTOSTART   "false" CACHE STRING "Automatically start GStreamerPlayer plugin")
set(PLUGIN_GSTREAMERPLAYER_STARTUPORDER ""      CACHE STRING "Startup order of GStreamerPlayer plugin")
set(PLUGIN_GSTREAMERPLAYER_MODE        "Local"  CACHE STRING "Run mode: Local (out-of-process), Inline or Remote")

find_package(${NAMESPACE}Plugins     REQUIRED)
find_package(${NAMESPACE}Definitions REQUIRED)
find_package(GStreamer                REQUIRED)

if(USE_THUNDER_R4)
    find_package(${NAMESPACE}COM       REQUIRED)
else()
    find_package(${NAMESPACE}Protocols REQUIRED)
endif()

# ---------------------------------------------------------------------------
# Plugin library  (in-process proxy – lives in the Thunder framework process)
# ---------------------------------------------------------------------------
add_library(${MODULE_NAME} SHARED
    GStreamerPlayer.cpp
    Module.cpp)

set_target_properties(${MODULE_NAME} PROPERTIES
    CXX_STANDARD          11
    CXX_STANDARD_REQUIRED YES)

target_compile_definitions(${MODULE_NAME} PRIVATE MODULE_NAME=Plugin_${PLUGIN_NAME})

target_include_directories(${MODULE_NAME} PRIVATE ../helpers)

target_link_libraries(${MODULE_NAME}
    PRIVATE
        ${NAMESPACE}Plugins::${NAMESPACE}Plugins
        ${NAMESPACE}Definitions::${NAMESPACE}Definitions)

install(TARGETS ${MODULE_NAME}
    DESTINATION lib/${STORAGE_DIRECTORY}/plugins)

# ---------------------------------------------------------------------------
# Implementation library  (out-of-process host – owns the GStreamer pipeline)
# ---------------------------------------------------------------------------
add_library(${PLUGIN_IMPLEMENTATION} SHARED
    GStreamerPlayerImplementation.cpp
    Module.cpp)

set_target_properties(${PLUGIN_IMPLEMENTATION} PROPERTIES
    CXX_STANDARD          11
    CXX_STANDARD_REQUIRED YES)

target_compile_definitions(${PLUGIN_IMPLEMENTATION} PRIVATE MODULE_NAME=Plugin_${PLUGIN_NAME}Implementation)

target_include_directories(${PLUGIN_IMPLEMENTATION} PRIVATE ../helpers)

target_link_libraries(${PLUGIN_IMPLEMENTATION}
    PRIVATE
        ${NAMESPACE}Plugins::${NAMESPACE}Plugins
        ${NAMESPACE}Definitions::${NAMESPACE}Definitions
        GStreamer::GStreamer)   # brings in GStreamer headers + gstreamer-1.0 library

if(USE_THUNDER_R4)
    target_link_libraries(${PLUGIN_IMPLEMENTATION} PRIVATE ${NAMESPACE}COM::${NAMESPACE}COM)
else()
    target_link_libraries(${PLUGIN_IMPLEMENTATION} PRIVATE ${NAMESPACE}Protocols::${NAMESPACE}Protocols)
endif()

install(TARGETS ${PLUGIN_IMPLEMENTATION}
    DESTINATION lib/${STORAGE_DIRECTORY}/plugins)

# Generate the Thunder JSON config file from GStreamerPlayer.conf.in
write_config(${PLUGIN_NAME})
