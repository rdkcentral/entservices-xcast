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

#include "ProcessWatcher.h"
#define API_VERSION_NUMBER_MAJOR 1
#define API_VERSION_NUMBER_MINOR 0
#define API_VERSION_NUMBER_PATCH 0

namespace WPEFramework {
    namespace {
        static Plugin::Metadata<Plugin::ProcessWatcher> metadata(
            API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH,
            /* Preconditions  */ {},
            /* Terminations   */ {},
            /* Controls       */ {}
        );
    }

    namespace Plugin {

        SERVICE_REGISTRATION(ProcessWatcher,
            API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH);

        // ---------------------------------------------------------------------
        // Constructor / Destructor
        // ---------------------------------------------------------------------

        ProcessWatcher::ProcessWatcher()
            : _service(nullptr)
            , _connectionId(0)
            , _processWatcher(nullptr)
            , _notification(this)
            , _resourceMonitorSink(this)
            , _resourceMonitorService(nullptr)
        {
            SYSLOG(Logging::Startup, (_T("ProcessWatcher Constructor")));
        }

        ProcessWatcher::~ProcessWatcher()
        {
            SYSLOG(Logging::Shutdown, (string(_T("ProcessWatcher Destructor"))));
        }

        // ---------------------------------------------------------------------
        // IPlugin::Initialize
        //
        // 1. Instantiates ProcessWatcherImplementation in-process via Root<>.
        // 2. Registers JSON-RPC endpoints; overrides killprocess to delegate
        //    to ResourceMonitor via COM-RPC.
        // 3. Subscribes to ResourceMonitor kill events via ResourceMonitorSink.
        // ---------------------------------------------------------------------
        const string ProcessWatcher::Initialize(PluginHost::IShell* service)
        {
            string message;

            ASSERT(service != nullptr);
            ASSERT(_service == nullptr);
            ASSERT(_processWatcher == nullptr);
            ASSERT(_connectionId == 0);

            SYSLOG(Logging::Startup, (_T("ProcessWatcher::Initialize: PID=%u"), getpid()));

            _service = service;
            _service->AddRef();

            // Watch for out-of-process crash/activation events
            _service->Register(&_notification);

            // Instantiate ProcessWatcherImplementation in-process via Root<>.
            _processWatcher = _service->Root<Exchange::IProcessWatcher>(
                _connectionId, 5000 /*ms*/, _T("ProcessWatcherImplementation"));

            if (_processWatcher != nullptr) {
                // Register JSON-RPC endpoints on this JSONRPC instance
                Exchange::JProcessWatcher::Register(*this, _processWatcher);

                // Override killprocess to delegate to ResourceMonitor via COM-RPC
                Unregister(_T("killprocess"));
                Register<Core::JSON::DecSInt32, Core::JSON::Boolean>(
                    _T("killprocess"),
                    [this](const Core::JSON::DecSInt32& pid,
                           Core::JSON::Boolean&         result) -> uint32_t {
                        result = KillProcessViaResourceMonitor(pid.Value());
                        return Core::ERROR_NONE;
                    });

                LOGINFO("ProcessWatcher::Initialize: impl ready, connectionId=%u",
                        _connectionId);
            } else {
                SYSLOG(Logging::Startup,
                       (_T("ProcessWatcher::Initialize: Root<IProcessWatcher> returned null")));
                message = _T("ProcessWatcher out-of-process implementation could not be instantiated");
            }

            // Subscribe to ResourceMonitor kill events
            Exchange::IResourceMonitor* rm =
                service->QueryInterfaceByCallsign<Exchange::IResourceMonitor>("org.rdk.ResourceMonitor");
            if (rm != nullptr) {
                rm->Register(&_resourceMonitorSink);
                _resourceMonitorService = rm;  // keep ref; released after Unregister in Deinitialize
            } else {
                LOGINFO("ProcessWatcher::Initialize: ResourceMonitor not available, skipping subscription");
            }

            if (!message.empty()) {
                LOGERR("'%s'", message.c_str());
            }
            return message;
        }

        // ---------------------------------------------------------------------
        // IPlugin::Deinitialize
        //
        // Tear down in reverse order of Initialize.
        // ---------------------------------------------------------------------
        void ProcessWatcher::Deinitialize(PluginHost::IShell* service)
        {
            ASSERT(_service == service);
            SYSLOG(Logging::Shutdown, (string(_T("ProcessWatcher::Deinitialize"))));

            if (_processWatcher != nullptr) {
                // Remove JSON-RPC endpoints first
                Exchange::JProcessWatcher::Unregister(*this);

                RPC::IRemoteConnection* connection = service->RemoteConnection(_connectionId);

                VARIABLE_IS_NOT_USED uint32_t result = _processWatcher->Release();
                ASSERT(result == Core::ERROR_DESTRUCTION_SUCCEEDED);
                _processWatcher = nullptr;

                if (connection != nullptr) {
                    connection->Terminate();
                    connection->Release();
                }
            }

            if (_service != nullptr) {
                if (_resourceMonitorService != nullptr) {
                    _resourceMonitorService->Unregister(&_resourceMonitorSink);
                    _resourceMonitorService->Release();
                    _resourceMonitorService = nullptr;
                }
                _service->Unregister(&_notification);
                _service->Release();
                _service = nullptr;
            }
            _connectionId = 0;

            SYSLOG(Logging::Shutdown, (string(_T("ProcessWatcher de-initialised"))));
        }

        // ---------------------------------------------------------------------
        // IPlugin::Information
        // ---------------------------------------------------------------------
        string ProcessWatcher::Information() const
        {
            return ("ProcessWatcher plugin monitors system resources and process lifecycle events "
                    "(JSON-RPC + COM-RPC dual transport)");
        }

        // ---------------------------------------------------------------------
        // Deactivated — called by Notification when the out-of-process impl
        // crashes; schedules plugin deactivation on a Thunder-owned thread.
        // ---------------------------------------------------------------------
        void ProcessWatcher::Deactivated(RPC::IRemoteConnection* connection)
        {
            if (connection->Id() == _connectionId) {
                ASSERT(_service != nullptr);
                Core::IWorkerPool::Instance().Submit(
                    PluginHost::IShell::Job::Create(
                        _service, PluginHost::IShell::DEACTIVATED, PluginHost::IShell::FAILURE));
            }
        }

        bool ProcessWatcher::KillProcessViaResourceMonitor(int pid)
        {
            ASSERT(_service != nullptr);

            Exchange::IResourceMonitor* rm =
                _service->QueryInterfaceByCallsign<Exchange::IResourceMonitor>("org.rdk.ResourceMonitor");

            if (rm == nullptr) {
                LOGERR("KillProcessViaResourceMonitor: ResourceMonitor not available");
                return false;
            }

            bool result = false;
            rm->KillProcess(pid, result);
            rm->Release();

            LOGINFO("KillProcessViaResourceMonitor: pid=%d result=%s", pid, result ? "success" : "failed");
            return result;
        }

    } // namespace Plugin
} // namespace WPEFramework
