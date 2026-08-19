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

#include "ProcessHandlerImplementation.h"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>

#include <sys/types.h>
#include <signal.h>

namespace WPEFramework {
namespace Plugin {

    // Register so Thunder's COM-RPC layer can instantiate this class when
    // the proxy (ProcessHandler.cpp) calls _service->Root<>().
    SERVICE_REGISTRATION(ProcessHandlerImplementation, 1, 0);

    // =========================================================================
    // Constructor / Destructor
    // =========================================================================

    ProcessHandlerImplementation::ProcessHandlerImplementation()
    {
        SYSLOG(Logging::Startup, (_T("ProcessHandlerImplementation constructor")));
    }

    ProcessHandlerImplementation::~ProcessHandlerImplementation()
    {
        SYSLOG(Logging::Shutdown, (_T("ProcessHandlerImplementation destructor")));
    }

    // =========================================================================
    // Kill
    // =========================================================================

    Core::hresult ProcessHandlerImplementation::Kill(const uint32_t pid)
    {
        if (pid == 0) {
            SYSLOG(Logging::Error, (_T("ProcessHandler::Kill: pid 0 is invalid")));
            return Core::ERROR_BAD_REQUEST;
        }

        SYSLOG(Logging::Startup, (_T("ProcessHandler::Kill: sending SIGKILL to pid %u"), pid));

        if (::kill(static_cast<pid_t>(pid), SIGKILL) != 0) {
            const int err = errno;
            SYSLOG(Logging::Error,
                (_T("ProcessHandler::Kill: kill(%u) failed: %s"), pid, ::strerror(err)));
            return (err == ESRCH) ? Core::ERROR_UNKNOWN_KEY : Core::ERROR_GENERAL;
        }

        return Core::ERROR_NONE;
    }

    // =========================================================================
    // Top
    // =========================================================================

    Core::hresult ProcessHandlerImplementation::Top(string& output)
    {
        output.clear();

        // -b  batch mode (non-interactive)
        // -n1 one iteration then exit
        FILE* pipe = ::popen("top -b -n1", "r");
        if (pipe == nullptr) {
            SYSLOG(Logging::Error, (_T("ProcessHandler::Top: popen failed: %s"), ::strerror(errno)));
            return Core::ERROR_GENERAL;
        }

        char buffer[512];
        while (::fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
        }

        ::pclose(pipe);

        SYSLOG(Logging::Startup,
            (_T("ProcessHandler::Top: captured %zu bytes"), output.size()));

        return Core::ERROR_NONE;
    }

} // namespace Plugin
} // namespace WPEFramework
