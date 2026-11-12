cmake_minimum_required(VERSION 3.15)

project(SystemMonitor VERSION 1.0.0 LANGUAGES CXX)

# ----------------------------------------------------------------------
# Dependencies
# ----------------------------------------------------------------------

find_package(WPEFramework REQUIRED)

find_package(${NAMESPACE}Plugins REQUIRED)
find_package(${NAMESPACE}Definitions REQUIRED)
find_package(${NAMESPACE}Messaging REQUIRED)
find_package(CompileSettingsDebug CONFIG REQUIRED)


# ----------------------------------------------------------------------
# Plugin configuration
# ----------------------------------------------------------------------

set(PLUGIN_NAME SystemMonitor)
set(MODULE_NAME ${NAMESPACE}${PROJECT_NAME})
set(PLUGIN_IMPLEMENTATION ${MODULE_NAME}Implementation)

set(CXX_STD 11)

message(STATUS "Setup ${MODULE_NAME} v${PROJECT_VERSION}")
message(STATUS "MODULE_NAME = [${MODULE_NAME}]")
message(STATUS "PLUGIN_IMPLEMENTATION = [${PLUGIN_IMPLEMENTATION}]")
message(STATUS "NAMESPACE = [${NAMESPACE}]")
message(STATUS "CXX_STD = [${CXX_STD}]")


set(PLUGIN_SYSTEMMONITOR_STARTMODE
    "Activated"
    CACHE STRING
    "Automatically start SystemMonitor plugin"
)

set(PLUGIN_SYSTEMMONITOR_MODE
    "Local"
    CACHE STRING
    "Controls if the plugin should run in its own process, in process or remote"
)


# ----------------------------------------------------------------------
# Optional build reference
# ----------------------------------------------------------------------

if(BUILD_REFERENCE)
    add_definitions(-DBUILD_REFERENCE=${BUILD_REFERENCE})
endif()


# ----------------------------------------------------------------------
# Main Thunder plugin
# ----------------------------------------------------------------------

add_library(${MODULE_NAME} SHARED
    SystemMonitor.cpp
    Module.cpp
)

set_target_properties(${MODULE_NAME}
    PROPERTIES
        CXX_STANDARD ${CXX_STD}
        CXX_STANDARD_REQUIRED YES
)

target_compile_definitions(${MODULE_NAME}
    PRIVATE
        MODULE_NAME=Plugin_${PLUGIN_NAME}
)

target_include_directories(${MODULE_NAME}
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../Helpers>
)

target_link_libraries(${MODULE_NAME}
    PRIVATE
        CompileSettingsDebug::CompileSettingsDebug
        ${NAMESPACE}Plugins::${NAMESPACE}Plugins
        ${NAMESPACE}Definitions::${NAMESPACE}Definitions
        ${NAMESPACE}Messaging::${NAMESPACE}Messaging
)


# ----------------------------------------------------------------------
# SystemMonitor implementation
# ----------------------------------------------------------------------

add_library(${PLUGIN_IMPLEMENTATION} SHARED
    SystemMonitorImplementation.cpp
)

set_target_properties(${PLUGIN_IMPLEMENTATION}
    PROPERTIES
        CXX_STANDARD ${CXX_STD}
        CXX_STANDARD_REQUIRED YES
)

target_compile_definitions(${PLUGIN_IMPLEMENTATION}
    PRIVATE
        MODULE_NAME=Plugin_${PLUGIN_NAME}Implementation
)

target_include_directories(${PLUGIN_IMPLEMENTATION}
    PRIVATE
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../Helpers>
)

target_link_libraries(${PLUGIN_IMPLEMENTATION}
    PRIVATE
        CompileSettingsDebug::CompileSettingsDebug
        ${NAMESPACE}Plugins::${NAMESPACE}Plugins
        ${NAMESPACE}Definitions::${NAMESPACE}Definitions
        ${NAMESPACE}Messaging::${NAMESPACE}Messaging
)


# ----------------------------------------------------------------------
# Install
# ----------------------------------------------------------------------

install(
    TARGETS ${MODULE_NAME}
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/${STORAGE_DIRECTORY}/plugins
    COMPONENT ${NAMESPACE}_Runtime
)

install(
    TARGETS ${PLUGIN_IMPLEMENTATION}
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT ${NAMESPACE}_Runtime
)


# ----------------------------------------------------------------------
# Generate Thunder plugin configuration
# ----------------------------------------------------------------------

write_config(${PLUGIN_NAME})