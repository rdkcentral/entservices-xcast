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

#include "ProcessHandler.h"

#define API_VERSION_NUMBER_MAJOR 1
#define API_VERSION_NUMBER_MINOR 0
#define API_VERSION_NUMBER_PATCH 0

namespace WPEFramework {
    namespace {
        static Plugin::Metadata<Plugin::ProcessHandler> metadata(
            API_VERSION_NUMBER_MAJOR,
            API_VERSION_NUMBER_MINOR,
            API_VERSION_NUMBER_PATCH,
            {}, // Preconditions
            {}, // Terminations
            {}  // Controls
        );
    }

    namespace Plugin {

        SERVICE_REGISTRATION(ProcessHandler,
            API_VERSION_NUMBER_MAJOR,
            API_VERSION_NUMBER_MINOR,
            API_VERSION_NUMBER_PATCH);

        ProcessHandler::ProcessHandler()
            : _service(nullptr)
            , _connectionId(0)
            , _impl(nullptr)
            , _notification(this)
        {
            SYSLOG(Logging::Startup, (_T("ProcessHandler Constructor")));
        }

        ProcessHandler::~ProcessHandler()
        {
            SYSLOG(Logging::Shutdown, (_T("ProcessHandler Destructor")));
        }

        const string ProcessHandler::Initialize(PluginHost::IShell* service)
        {
            string message{};

            ASSERT(nullptr != service);
            ASSERT(nullptr == _service);
            ASSERT(nullptr == _impl);
            ASSERT(0 == _connectionId);

            SYSLOG(Logging::Startup, (_T("ProcessHandler::Initialize: PID=%u"), getpid()));

            _service = service;
            _service->AddRef();
            _service->Register(&_notification);

            // Spawn the out-of-process host; Thunder loads
            // libWPEFrameworkProcessHandlerImplementation.so in a separate WPEProcess.
            _impl = _service->Root<Exchange::IProcessHandler>(
                _connectionId, 5000, _T("ProcessHandlerImplementation"));

            if (nullptr != _impl) {
                // Register JSON-RPC methods manually (no auto-generated J* file).
                Register<JsonObject, JsonObject>(
                    "kill",
                    [this](const JsonObject& params, JsonObject& /* response */) -> uint32_t
                    {
                        if (!params.HasLabel("pid")) {
                            return Core::ERROR_BAD_REQUEST;
                        }
                        const uint32_t pid = params["pid"].Number();
                        return _impl->Kill(pid);
                    });

                Register<JsonObject, JsonObject>(
                    "top",
                    [this](const JsonObject& /* params */, JsonObject& response) -> uint32_t
                    {
                        string output;
                        const uint32_t result = _impl->Top(output);
                        if (result == Core::ERROR_NONE) {
                            response["output"] = output;
                        }
                        return result;
                    });

                SYSLOG(Logging::Startup, (_T("ProcessHandler initialized successfully")));
            } else {
                SYSLOG(Logging::Startup,
                    (_T("ProcessHandler::Initialize: Failed to instantiate ProcessHandlerImplementation")));
                message = _T("ProcessHandlerImplementation could not be instantiated");
            }

            if (!message.empty()) {
                Deinitialize(service);
            }

            return message;
        }

        void ProcessHandler::Deinitialize(PluginHost::IShell* service)
        {
            ASSERT(_service == service);
            SYSLOG(Logging::Shutdown, (_T("ProcessHandler::Deinitialize")));

            if (nullptr != _impl) {
                Unregister("kill");
                Unregister("top");

                RPC::IRemoteConnection* connection = service->RemoteConnection(_connectionId);
                VARIABLE_IS_NOT_USED uint32_t result = _impl->Release();
                ASSERT(result == Core::ERROR_DESTRUCTION_SUCCEEDED);

                if (nullptr != connection) {
                    connection->Terminate();
                    connection->Release();
                }

                _impl = nullptr;
            }

            if (nullptr != _service) {
                _service->Unregister(&_notification);
                _service->Release();
                _service = nullptr;
            }

            _connectionId = 0;
            SYSLOG(Logging::Shutdown, (_T("ProcessHandler de-initialized")));
        }

        string ProcessHandler::Information() const
        {
            return ("ProcessHandler: kill a process by PID or capture top command output");
        }

        void ProcessHandler::Deactivated(RPC::IRemoteConnection* connection)
        {
            if (connection->Id() == _connectionId) {
                ASSERT(nullptr != _service);
                Core::IWorkerPool::Instance().Submit(
                    PluginHost::IShell::Job::Create(
                        _service,
                        PluginHost::IShell::DEACTIVATED,
                        PluginHost::IShell::FAILURE));
            }
        }

    } // namespace Plugin
} // namespace WPEFramework
