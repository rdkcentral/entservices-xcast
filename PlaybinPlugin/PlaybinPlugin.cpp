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

#include "PlaybinPlugin.h"

#define API_VERSION_NUMBER_MAJOR 1
#define API_VERSION_NUMBER_MINOR 0
#define API_VERSION_NUMBER_PATCH 0

namespace WPEFramework {
    namespace {
        static Plugin::Metadata<Plugin::PlaybinPlugin> metadata(
            API_VERSION_NUMBER_MAJOR,
            API_VERSION_NUMBER_MINOR,
            API_VERSION_NUMBER_PATCH,
            {}, // Preconditions
            {}, // Terminations
            {}  // Controls
        );
    }

    namespace Plugin {

        SERVICE_REGISTRATION(PlaybinPlugin,
            API_VERSION_NUMBER_MAJOR,
            API_VERSION_NUMBER_MINOR,
            API_VERSION_NUMBER_PATCH);

        PlaybinPlugin::PlaybinPlugin()
            : _service(nullptr)
            , _connectionId(0)
            , _playbinPlugin(nullptr)
            , _notification(this)
        {
            SYSLOG(Logging::Startup, (_T("PlaybinPlugin Constructor")));
        }

        PlaybinPlugin::~PlaybinPlugin()
        {
            SYSLOG(Logging::Shutdown, (string(_T("PlaybinPlugin Destructor"))));
        }

        const string PlaybinPlugin::Initialize(PluginHost::IShell* service)
        {
            string message{};

            ASSERT(nullptr != service);
            ASSERT(nullptr == _service);
            ASSERT(nullptr == _playbinPlugin);
            ASSERT(0 == _connectionId);

            SYSLOG(Logging::Startup, (_T("PlaybinPlugin::Initialize: PID=%u"), getpid()));

            _service = service;
            _service->AddRef();
            _service->Register(&_notification);

            // Instantiate the out-of-process part. Thunder spawns a separate
            // WPEProcess host that loads libWPEFrameworkPlaybinPluginImplementation.so
            // and returns a proxy to its IPlaybinPlugin interface.
            _playbinPlugin = _service->Root<Exchange::IPlaybinPlugin>(
                _connectionId, 5000, _T("PlaybinPluginImplementation"));

            if (nullptr != _playbinPlugin) {
                _playbinPlugin->Register(&_notification);
                Exchange::JPlaybinPlugin::Register(*this, _playbinPlugin);
                LOGINFO("PlaybinPlugin initialized successfully");
            } else {
                SYSLOG(Logging::Startup,
                    (_T("PlaybinPlugin::Initialize: Failed to instantiate PlaybinPluginImplementation")));
                message = _T("PlaybinPluginImplementation could not be instantiated");
            }

            if (!message.empty()) {
                LOGERR("'%s'", message.c_str());
                Deinitialize(service);
            }

            return message;
        }

        void PlaybinPlugin::Deinitialize(PluginHost::IShell* service)
        {
            ASSERT(_service == service);
            SYSLOG(Logging::Shutdown, (string(_T("PlaybinPlugin::Deinitialize"))));

            if (nullptr != _playbinPlugin) {
                _playbinPlugin->Unregister(&_notification);
                Exchange::JPlaybinPlugin::Unregister(*this);

                // Terminate the out-of-process host.
                RPC::IRemoteConnection* connection = service->RemoteConnection(_connectionId);
                VARIABLE_IS_NOT_USED uint32_t result = _playbinPlugin->Release();
                ASSERT(result == Core::ERROR_DESTRUCTION_SUCCEEDED);

                if (nullptr != connection) {
                    connection->Terminate();
                    connection->Release();
                }

                _playbinPlugin = nullptr;
            }

            if (nullptr != _service) {
                _service->Unregister(&_notification);
                _service->Release();
                _service = nullptr;
            }

            _connectionId = 0;
            SYSLOG(Logging::Shutdown, (string(_T("PlaybinPlugin de-initialized"))));
        }

        string PlaybinPlugin::Information() const
        {
            return ("PlaybinPlugin: plays media from a URI using a GStreamer playbin pipeline");
        }

        void PlaybinPlugin::Deactivated(RPC::IRemoteConnection* connection)
        {
            if (connection->Id() == _connectionId) {
                ASSERT(nullptr != _service);
                // Ask Thunder to deactivate this plugin on the next worker-pool cycle.
                Core::IWorkerPool::Instance().Submit(
                    PluginHost::IShell::Job::Create(
                        _service,
                        PluginHost::IShell::DEACTIVATED,
                        PluginHost::IShell::FAILURE));
            }
        }

    } // namespace Plugin
} // namespace WPEFramework
