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

#include "GStreamerPlayer.h"

#define API_VERSION_NUMBER_MAJOR 1
#define API_VERSION_NUMBER_MINOR 0
#define API_VERSION_NUMBER_PATCH 0

namespace WPEFramework {
    namespace {
        static Plugin::Metadata<Plugin::GStreamerPlayer> metadata(
            API_VERSION_NUMBER_MAJOR,
            API_VERSION_NUMBER_MINOR,
            API_VERSION_NUMBER_PATCH,
            {}, // Preconditions
            {}, // Terminations
            {}  // Controls
        );
    }

    namespace Plugin {

        SERVICE_REGISTRATION(GStreamerPlayer,
            API_VERSION_NUMBER_MAJOR,
            API_VERSION_NUMBER_MINOR,
            API_VERSION_NUMBER_PATCH);

        GStreamerPlayer::GStreamerPlayer()
            : _service(nullptr)
            , _connectionId(0)
            , _gstreamerPlayer(nullptr)
            , _notification(this)
        {
            SYSLOG(Logging::Startup, (_T("GStreamerPlayer Constructor")));
        }

        GStreamerPlayer::~GStreamerPlayer()
        {
            SYSLOG(Logging::Shutdown, (string(_T("GStreamerPlayer Destructor"))));
        }

        const string GStreamerPlayer::Initialize(PluginHost::IShell* service)
        {
            string message{};

            ASSERT(nullptr != service);
            ASSERT(nullptr == _service);
            ASSERT(nullptr == _gstreamerPlayer);
            ASSERT(0 == _connectionId);

            SYSLOG(Logging::Startup, (_T("GStreamerPlayer::Initialize: PID=%u"), getpid()));

            _service = service;
            _service->AddRef();
            _service->Register(&_notification);

            // Instantiate the out-of-process part. Thunder spawns a separate
            // WPEProcess host that loads libWPEFrameworkGStreamerPlayerImplementation.so
            // and returns a proxy to its IGStreamerPlayer interface.
            _gstreamerPlayer = _service->Root<Exchange::IGStreamerPlayer>(
                _connectionId, 5000, _T("GStreamerPlayerImplementation"));

            if (nullptr != _gstreamerPlayer) {
                _gstreamerPlayer->Register(&_notification);
                Exchange::JGStreamerPlayer::Register(*this, _gstreamerPlayer);
                LOGINFO("GStreamerPlayer initialized successfully");
            } else {
                SYSLOG(Logging::Startup,
                    (_T("GStreamerPlayer::Initialize: Failed to instantiate GStreamerPlayerImplementation")));
                message = _T("GStreamerPlayerImplementation could not be instantiated");
            }

            if (!message.empty()) {
                LOGERR("'%s'", message.c_str());
                Deinitialize(service);
            }

            return message;
        }

        void GStreamerPlayer::Deinitialize(PluginHost::IShell* service)
        {
            ASSERT(_service == service);
            SYSLOG(Logging::Shutdown, (string(_T("GStreamerPlayer::Deinitialize"))));

            if (nullptr != _gstreamerPlayer) {
                _gstreamerPlayer->Unregister(&_notification);
                Exchange::JGStreamerPlayer::Unregister(*this);

                // Terminate the out-of-process host.
                RPC::IRemoteConnection* connection = service->RemoteConnection(_connectionId);
                VARIABLE_IS_NOT_USED uint32_t result = _gstreamerPlayer->Release();
                ASSERT(result == Core::ERROR_DESTRUCTION_SUCCEEDED);

                if (nullptr != connection) {
                    connection->Terminate();
                    connection->Release();
                }

                _gstreamerPlayer = nullptr;
            }

            if (nullptr != _service) {
                _service->Unregister(&_notification);
                _service->Release();
                _service = nullptr;
            }

            _connectionId = 0;
            SYSLOG(Logging::Shutdown, (string(_T("GStreamerPlayer de-initialized"))));
        }

        string GStreamerPlayer::Information() const
        {
            return ("GStreamerPlayer: plays media from a URI using a manual GStreamer pipeline");
        }

        void GStreamerPlayer::Deactivated(RPC::IRemoteConnection* connection)
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
