# If not stated otherwise in this file or this component's Licenses.txt file the
# following copyright and licenses apply:
#
# Copyright 2016 RDK Management
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

#
# features
#

add_definitions (-DUSE_IARM)
option(USE_IARM "USE_IARM" ON)

add_definitions (-DUSE_IARM_BUS)
option(USE_IARM_BUS "USE_IARM_BUS" ON)

add_definitions (-DUSE_IARMBUS)

add_definitions (-DHAS_API_SYSTEM)
add_definitions (-DHAS_API_POWERSTATE)


if (BUILD_ENABLE_TELEMETRY_LOGGING)
    message("Building with telemetry logging")
    add_definitions (-DENABLE_TELEMETRY_LOGGING)
endif()

add_definitions (-DENABLE_DEEP_SLEEP)

add_definitions (-DHAS_FRONT_PANEL)
option(HAS_FRONT_PANEL "HAS_FRONT_PANEL" ON)

