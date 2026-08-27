# If not stated otherwise in this file or this component's license file the
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

set(PLUGIN_NAME ProcessWatcher)
set(MODULE_NAME ${NAMESPACE}${PLUGIN_NAME})
set(PLUGIN_IMPLEMENTATION ${MODULE_NAME}Implementation)

set(PLUGIN_PROCESSWATCHER_AUTOSTART   "true" CACHE STRING "Automatically start ProcessWatcher plugin")
set(PLUGIN_PROCESSWATCHER_STARTUPORDER ""    CACHE STRING "Startup order of ProcessWatcher plugin")
set(PLUGIN_PROCESSWATCHER_MODE         "Off" CACHE STRING "Controls if the plugin should run in its own process, in process or remote")

find_package(${NAMESPACE}Plugins     REQUIRED)
find_package(${NAMESPACE}Definitions REQUIRED)

if(USE_THUNDER_R4)
    find_package(${NAMESPACE}COM REQUIRED)
else()
    find_package(${NAMESPACE}Protocols REQUIRED)
endif()

# -----------------------------------------------------------------------------
# Outer plugin shell
#
# Loaded in the Thunder host process.
# Handles plugin lifecycle (Initialize/Deinitialize), registers both JSON-RPC
# endpoints (via JProcessWatcher) and exposes IProcessWatcher over COM-RPC.
# -----------------------------------------------------------------------------
add_library(${MODULE_NAME} SHARED
        ProcessWatcher.cpp
        Module.cpp)

set_target_properties(${MODULE_NAME} PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES)

target_compile_definitions(${MODULE_NAME} PRIVATE
        MODULE_NAME=Plugin_${PLUGIN_NAME})

target_include_directories(${MODULE_NAME} PRIVATE
        ../helpers)

target_link_libraries(${MODULE_NAME} PRIVATE
        ${NAMESPACE}Plugins::${NAMESPACE}Plugins
        ${NAMESPACE}Definitions::${NAMESPACE}Definitions)

install(TARGETS ${MODULE_NAME}
        DESTINATION lib/${STORAGE_DIRECTORY}/plugins)

# -----------------------------------------------------------------------------
# Out-of-process implementation
#
# Spawned by Thunder in a separate helper process.
# Contains the actual KillProcess / GetSystemResourceInfo logic
# and manages the subscriber list.
# -----------------------------------------------------------------------------
add_library(${PLUGIN_IMPLEMENTATION} SHARED
        ProcessWatcherImplementation.cpp
        Module.cpp)

set_target_properties(${PLUGIN_IMPLEMENTATION} PROPERTIES
        CXX_STANDARD 11
        CXX_STANDARD_REQUIRED YES)

target_compile_definitions(${PLUGIN_IMPLEMENTATION} PRIVATE
        MODULE_NAME=Plugin_${PLUGIN_NAME}Implementation)

target_include_directories(${PLUGIN_IMPLEMENTATION} PRIVATE
        ../helpers)

target_link_libraries(${PLUGIN_IMPLEMENTATION} PRIVATE
        ${NAMESPACE}Plugins::${NAMESPACE}Plugins
        ${NAMESPACE}Definitions::${NAMESPACE}Definitions)

if(USE_THUNDER_R4)
    target_link_libraries(${PLUGIN_IMPLEMENTATION} PRIVATE
            ${NAMESPACE}COM::${NAMESPACE}COM)
else()
    target_link_libraries(${PLUGIN_IMPLEMENTATION} PRIVATE
            ${NAMESPACE}Protocols::${NAMESPACE}Protocols)
endif()

install(TARGETS ${PLUGIN_IMPLEMENTATION}
        DESTINATION lib/${STORAGE_DIRECTORY}/plugins)

write_config(${PLUGIN_NAME})
