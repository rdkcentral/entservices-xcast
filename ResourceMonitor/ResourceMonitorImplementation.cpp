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

#include "ResourceMonitorImplementation.h"
#include <csignal>    // kill()
#include <cerrno>

namespace WPEFramework {
namespace Plugin {

    SERVICE_REGISTRATION(ResourceMonitorImplementation, 1, 0);

    // -------------------------------------------------------------------------
    // Lifecycle
    // -------------------------------------------------------------------------

    ResourceMonitorImplementation::ResourceMonitorImplementation()
        : _adminLock()
        , _processKilledNotifications()
    {
        LOGINFO("ResourceMonitorImplementation constructed");
    }

    //Release all registered notifications and clear the list
    ResourceMonitorImplementation::~ResourceMonitorImplementation()
    {
        LOGINFO("ResourceMonitorImplementation destroyed");
        _adminLock.Lock();
        for (auto* notification : _processKilledNotifications) {
            notification->Release();
        }
        _processKilledNotifications.clear();
        _adminLock.Unlock();
    }

    // -------------------------------------------------------------------------
    // Notification registration
    // -------------------------------------------------------------------------

    Core::hresult ResourceMonitorImplementation::Register(
        Exchange::IResourceMonitor::IProcessKilledNotification* notification)
    {
        ASSERT(notification != nullptr);

        _adminLock.Lock();
        auto it = std::find(_processKilledNotifications.begin(),
                            _processKilledNotifications.end(), notification);
        if (it == _processKilledNotifications.end()) {
            _processKilledNotifications.push_back(notification);
            notification->AddRef();
            LOGINFO("Registered IProcessKilledNotification %p", notification);
        } else {
            LOGERR("IProcessKilledNotification %p already registered", notification);
        }
        _adminLock.Unlock();
        return Core::ERROR_NONE;
    }

    Core::hresult ResourceMonitorImplementation::Unregister(
        const Exchange::IResourceMonitor::IProcessKilledNotification* notification)
    {
        ASSERT(notification != nullptr);

        Core::hresult status = Core::ERROR_GENERAL;

        _adminLock.Lock();
        auto it = std::find(_processKilledNotifications.begin(),
                            _processKilledNotifications.end(), notification);
        if (it != _processKilledNotifications.end()) {
            (*it)->Release();
            _processKilledNotifications.erase(it);
            LOGINFO("Unregistered IProcessKilledNotification");
            status = Core::ERROR_NONE;
        } else {
            LOGERR("IProcessKilledNotification not found");
        }
        _adminLock.Unlock();
        return status;
    }

    // -------------------------------------------------------------------------
    // Event firing helper
    // -------------------------------------------------------------------------

    void ResourceMonitorImplementation::NotifyProcessKilled(
        const string& processName, int pid, int exitCode)
    {
        _adminLock.Lock();
        auto it = _processKilledNotifications.begin();
        while (it != _processKilledNotifications.end()) {
            (*it)->OnProcessKilled(processName, pid, exitCode);
            ++it;
        }
        _adminLock.Unlock();
    }

    // -------------------------------------------------------------------------
    // API implementations
    // -------------------------------------------------------------------------

    Core::hresult ResourceMonitorImplementation::GetApiVersionNumber(int& version)
    {
        version = 1;
        return Core::ERROR_NONE;
    }

    Core::hresult ResourceMonitorImplementation::GetState()
    {
        // TODO: populate and return current resource-monitor state
        LOGINFO("GetState called");
        return Core::ERROR_NONE;
    }

    Core::hresult ResourceMonitorImplementation::GetSystemResourceInfo(string& topresult)
    {
        // TODO: populate topresult with JSON-formatted system resource data
        LOGINFO("GetSystemResourceInfo called");
        (void)topresult;
        return Core::ERROR_NONE;
    }

    Core::hresult ResourceMonitorImplementation::KillProcess(int PID, bool& result)
    {
        result = false;

        if (PID <= 0) {
            LOGERR("KillProcess: invalid PID %d", PID);
            return Core::ERROR_BAD_REQUEST;
        }

        if (::kill(static_cast<pid_t>(PID), SIGKILL) == 0) {
            LOGINFO("KillProcess: killed PID %d", PID);
            result = true;
            // Notify all listeners; process name is unknown at this point
            NotifyProcessKilled("", PID, SIGKILL);
            return Core::ERROR_NONE;
        }

        LOGERR("KillProcess: kill(%d) failed, errno=%d", PID, errno);
        return Core::ERROR_GENERAL;
    }

} // namespace Plugin
} // namespace WPEFramework
