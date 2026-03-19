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

#include "HelloPlugin.h"

#define API_VERSION_NUMBER_MAJOR 1
#define API_VERSION_NUMBER_MINOR 0
#define API_VERSION_NUMBER_PATCH 0

namespace WPEFramework {
    namespace {
        static Plugin::Metadata<Plugin::HelloPlugin> metadata(
            API_VERSION_NUMBER_MAJOR,
            API_VERSION_NUMBER_MINOR,
            API_VERSION_NUMBER_PATCH,
            {}, // Preconditions
            {}, // Terminations
            {}  // Controls
        );
    }

    namespace Plugin {

        SERVICE_REGISTRATION(HelloPlugin, API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH);

        HelloPlugin::HelloPlugin()
            : _service(nullptr)
            , _connectionId(0)
            , _helloPlugin(nullptr)
            , _helloPluginNotification(this)
        {
            SYSLOG(Logging::Startup, (_T("HelloPlugin Constructor")));
        }

        HelloPlugin::~HelloPlugin()
        {
            SYSLOG(Logging::Shutdown, (string(_T("HelloPlugin Destructor"))));
        }

        const string HelloPlugin::Initialize(PluginHost::IShell* service)
        {
            string message{};

            ASSERT(nullptr != service);
            ASSERT(nullptr == _service);
            ASSERT(nullptr == _helloPlugin);
            ASSERT(0 == _connectionId);

            SYSLOG(Logging::Startup, (_T("HelloPlugin::Initialize: PID=%u"), getpid()));

            _service = service;
            _service->AddRef();
            _service->Register(&_helloPluginNotification);

            _helloPlugin = _service->Root<Exchange::IHelloPlugin>(_connectionId, 5000, _T("HelloPluginImplementation"));

            if (nullptr != _helloPlugin) {
                _helloPlugin->Register(&_helloPluginNotification);
                Exchange::JHelloPlugin::Register(*this, _helloPlugin);
                LOGINFO("HelloPlugin initialized successfully");
            } else {
                SYSLOG(Logging::Startup, (_T("HelloPlugin::Initialize: Failed to instantiate HelloPluginImplementation")));
                message = _T("HelloPlugin implementation could not be instantiated");
            }

            if (!message.empty()) {
                LOGERR("'%s'", message.c_str());
                Deinitialize(service);
            }

            return message;
        }

        void HelloPlugin::Deinitialize(PluginHost::IShell* service)
        {
            ASSERT(_service == service);
            SYSLOG(Logging::Shutdown, (string(_T("HelloPlugin::Deinitialize"))));

            if (nullptr != _helloPlugin) {
                _helloPlugin->Unregister(&_helloPluginNotification);
                Exchange::JHelloPlugin::Unregister(*this);

                // Terminate the out-of-process side
                RPC::IRemoteConnection* connection = service->RemoteConnection(_connectionId);
                VARIABLE_IS_NOT_USED uint32_t result = _helloPlugin->Release();

                ASSERT(result == Core::ERROR_DESTRUCTION_SUCCEEDED);

                if (nullptr != connection) {
                    connection->Terminate();
                    connection->Release();
                }

                _helloPlugin = nullptr;
            }

            if (nullptr != _service) {
                _service->Unregister(&_helloPluginNotification);
                _service->Release();
                _service = nullptr;
            }

            _connectionId = 0;
            SYSLOG(Logging::Shutdown, (string(_T("HelloPlugin de-initialized"))));
        }

        string HelloPlugin::Information() const
        {
            return ("HelloPlugin: outputs a greeting message from an out-of-process implementation");
        }

        void HelloPlugin::Deactivated(RPC::IRemoteConnection* connection)
        {
            if (connection->Id() == _connectionId) {
                ASSERT(nullptr != _service);
                Core::IWorkerPool::Instance().Submit(
                    PluginHost::IShell::Job::Create(_service, PluginHost::IShell::DEACTIVATED, PluginHost::IShell::FAILURE));
            }
        }

    } // namespace Plugin
} // namespace WPEFramework
