/*
 * If not stated otherwise in this file or this component's LICENSE file the
 * following copyright and licenses apply:
 *
 * Copyright 2024 RDK Management
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ProcessWatcherImplementation.h"
#include <csignal>
#include <cerrno>

namespace WPEFramework {
namespace Plugin {

    SERVICE_REGISTRATION(ProcessWatcherImplementation, 1, 0);

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    ProcessWatcherImplementation::ProcessWatcherImplementation()
        : _adminLock()
    {
        LOGINFO("ProcessWatcherImplementation constructed");
    }

    ProcessWatcherImplementation::~ProcessWatcherImplementation()
    {
        LOGINFO("ProcessWatcherImplementation destroyed");
    }

    // -------------------------------------------------------------------------
    // API implementations
    // -------------------------------------------------------------------------

    Core::hresult ProcessWatcherImplementation::GetApiVersionNumber(int& version)
    {
        version = 1;
        return Core::ERROR_NONE;
    }

    Core::hresult ProcessWatcherImplementation::GetState()
    {
        LOGINFO("GetState called");
        return Core::ERROR_NONE;
    }

    Core::hresult ProcessWatcherImplementation::GetSystemResourceInfo(string& info)
    {
        // TODO: populate info with JSON-formatted system resource data
        LOGINFO("GetSystemResourceInfo called");
        (void)info;
        return Core::ERROR_NONE;
    }

    Core::hresult ProcessWatcherImplementation::KillProcess(int pid, bool& result)
    {
        result = false;

        if (pid <= 0) {
            LOGERR("KillProcess: invalid PID %d", pid);
            return Core::ERROR_BAD_REQUEST;
        }

        if (::kill(static_cast<pid_t>(pid), SIGKILL) == 0) {
            LOGINFO("KillProcess: killed PID %d", pid);
            result = true;
            return Core::ERROR_NONE;
        }

        LOGERR("KillProcess: kill(%d) failed, errno=%d", pid, errno);
        return Core::ERROR_GENERAL;
    }

} // namespace Plugin
} // namespace WPEFramework
