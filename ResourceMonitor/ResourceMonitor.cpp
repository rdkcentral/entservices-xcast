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

#include "ResourceMonitor.h"

#define API_VERSION_NUMBER_MAJOR 1
#define API_VERSION_NUMBER_MINOR 0
#define API_VERSION_NUMBER_PATCH 0

namespace WPEFramework {
    namespace {
        static Plugin::Metadata<Plugin::ResourceMonitor> metadata(
            API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH,
            /* Preconditions  */ {},
            /* Terminations   */ {},
            /* Controls       */ {}
        );
    }

    namespace Plugin {

        SERVICE_REGISTRATION(ResourceMonitor,
            API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH);

        // ---------------------------------------------------------------------
        // Constructor / Destructor
        // ---------------------------------------------------------------------

        ResourceMonitor::ResourceMonitor()
            : _service(nullptr)
            , _connectionId(0)
            , _resourceMonitor(nullptr)
            , _notification(this)
            , _notificationRegistered(false)
        {
            SYSLOG(Logging::Startup, (_T("ResourceMonitor Constructor")));
        }

        ResourceMonitor::~ResourceMonitor()
        {
            SYSLOG(Logging::Shutdown, (string(_T("ResourceMonitor Destructor"))));
        }

        // ---------------------------------------------------------------------
        // IPlugin::Initialize — spin up out-of-process impl and register sink
        // ---------------------------------------------------------------------
        const string ResourceMonitor::Initialize(PluginHost::IShell* service)
        {
            string message;

            ASSERT(service != nullptr);
            ASSERT(_service == nullptr);
            ASSERT(_resourceMonitor == nullptr);
            ASSERT(_connectionId == 0);

            SYSLOG(Logging::Startup, (_T("ResourceMonitor::Initialize: PID=%u"), getpid()));

            _service = service;
            _service->AddRef();

            // Watch for out-of-process crash/activation events
            _service->Register(&_notification);

            // Instantiate ResourceMonitorImplementation in a separate process.
            // Root<> returns a proxy; all method calls go over COM-RPC.
            _resourceMonitor = _service->Root<Exchange::IResourceMonitor>(
                _connectionId, 5000 /*ms timeout*/, _T("ResourceMonitorImplementation"));

            if (_resourceMonitor != nullptr) {
                // Subscribe to COM events from the implementation
                _resourceMonitor->Register(&_notification);
                _notificationRegistered = true;

                LOGINFO("ResourceMonitor::Initialize: out-of-process impl ready, connectionId=%u",
                        _connectionId);
            } else {
                SYSLOG(Logging::Startup,
                       (_T("ResourceMonitor::Initialize: Root<IResourceMonitor> returned null")));
                message = _T("ResourceMonitor out-of-process implementation could not be instantiated");
            }

            if (!message.empty()) {
                LOGERR("'%s'", message.c_str());
            }
            return message;
        }

        // ---------------------------------------------------------------------
        // IPlugin::Deinitialize
        //
        // Called by Thunder when the plugin is deactivated.
        // Tear down in the reverse order of Initialize.
        // ---------------------------------------------------------------------
        void ResourceMonitor::Deinitialize(PluginHost::IShell* service)
        {
            ASSERT(_service == service);
            SYSLOG(Logging::Shutdown, (string(_T("ResourceMonitor::Deinitialize"))));

            if (_resourceMonitor != nullptr) {
                if (_notificationRegistered) {
                    // Unregister COM event sink
                    _resourceMonitor->Unregister(&_notification);
                    _notificationRegistered = false;
                }

                // Retrieve the remote connection so we can terminate the process
                RPC::IRemoteConnection* connection = service->RemoteConnection(_connectionId);

                VARIABLE_IS_NOT_USED uint32_t result = _resourceMonitor->Release();
                // We should have held the last reference — destruction must succeed
                ASSERT(result == Core::ERROR_DESTRUCTION_SUCCEEDED);
                _resourceMonitor = nullptr;

                if (connection != nullptr) {
                    // Forcefully terminate the out-of-process helper if still running
                    connection->Terminate();
                    connection->Release();
                }
            }

            if (_service != nullptr) {
                _service->Unregister(&_notification);
                _service->Release();
                _service = nullptr;
            }
            _connectionId = 0;

            SYSLOG(Logging::Shutdown, (string(_T("ResourceMonitor de-initialised"))));
        }

        // ---------------------------------------------------------------------
        // IPlugin::Information
        // ---------------------------------------------------------------------
        string ResourceMonitor::Information() const
        {
            return ("ResourceMonitor plugin monitors system resources and process lifecycle events");
        }

        // ---------------------------------------------------------------------
        // Deactivated — called by Notification::Deactivated() when the
        // out-of-process impl crashes.  We schedule a plugin deactivation
        // through the worker pool so it happens on a Thunder-owned thread.
        // ---------------------------------------------------------------------
        void ResourceMonitor::Deactivated(RPC::IRemoteConnection* connection)
        {
            if (connection->Id() == _connectionId) {
                ASSERT(_service != nullptr);
                Core::IWorkerPool::Instance().Submit(
                    PluginHost::IShell::Job::Create(
                        _service, PluginHost::IShell::DEACTIVATED, PluginHost::IShell::FAILURE));
            }
        }

    } // namespace Plugin
} // namespace WPEFramework
