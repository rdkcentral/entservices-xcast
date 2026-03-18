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

namespace WPEFramework
{
    namespace {
        static Plugin::Metadata<Plugin::GStreamerPlayer> metadata(
            // Version (Major, Minor, Patch)
            API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH,
            // Preconditions
            {},
            // Terminations
            {},
            // Controls
            {}
        );
    }

    namespace Plugin
    {
        SERVICE_REGISTRATION(GStreamerPlayer, API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH);

        GStreamerPlayer::GStreamerPlayer()
            : _service(nullptr)
            , _connectionId(0)
            , _gstreamerPlayer(nullptr)
            , _configuration(nullptr)
            , _gstreamerPlayerNotification(this)
        {
            SYSLOG(Logging::Startup, (_T("GStreamerPlayer Constructor")));
        }

        GStreamerPlayer::~GStreamerPlayer()
        {
            SYSLOG(Logging::Shutdown, (string(_T("GStreamerPlayer Destructor"))));
        }

        const string GStreamerPlayer::Initialize(PluginHost::IShell *service)
        {
            string message = "";

            ASSERT(nullptr != service);
            ASSERT(nullptr == _service);
            ASSERT(nullptr == _gstreamerPlayer);
            ASSERT(0 == _connectionId);

            SYSLOG(Logging::Startup, (_T("GStreamerPlayer::Initialize: PID=%u"), getpid()));

            _service = service;
            _service->AddRef();
            _service->Register(&_gstreamerPlayerNotification); 

            _gstreamerPlayer = _service->Root<Exchange::IGStreamerPlayer>(_connectionId, 5000, _T("GStreamerPlayerImplementation"));
            
            if (nullptr != _gstreamerPlayer)
            {
                _configuration = _gstreamerPlayer->QueryInterface<Exchange::IConfiguration>();
                if (nullptr != _configuration)
                {
                    uint32_t result = _configuration->Configure(_service);
                    if(result != Core::ERROR_NONE)
                    {
                        message = _T("GStreamerPlayer could not be configured");
                    }
                    else
                    {
                        LOGINFO("GStreamerPlayerImplementation Initialize() successfully");
                        // Register for notifications
                        _gstreamerPlayer->Register(&_gstreamerPlayerNotification);
                        // Invoking Plugin API register to wpeframework
                        Exchange::JGStreamerPlayer::Register(*this, _gstreamerPlayer);
                    }
                }
                else
                {
                    message = _T("GStreamerPlayer implementation did not provide a configuration interface");
                }
            }
            else
            {
                SYSLOG(Logging::Startup, (_T("GStreamerPlayer::Initialize: Failed to initialise GStreamerPlayer plugin")));
                message = _T("GStreamerPlayer plugin could not be initialised");
            }

            if (0 != message.length())
            {
                LOGERR("'%s'", message.c_str());
                Deinitialize(service);
            }

            return message;
        }

        void GStreamerPlayer::Deinitialize(PluginHost::IShell *service)
        {
            ASSERT(_service == service);
            LOGINFO("GStreamerPlayer::Deinitialize: service = %p", service);
            SYSLOG(Logging::Shutdown, (string(_T("GStreamerPlayer::Deinitialize"))));

            if (nullptr != _gstreamerPlayer)
            {
                _gstreamerPlayer->Unregister(&_gstreamerPlayerNotification);
                Exchange::JGStreamerPlayer::Unregister(*this);
                if (nullptr != _configuration)
                {
                    uint32_t result = _configuration->Configure(nullptr);
                    if(result != Core::ERROR_NONE)
                    {
                        LOGERR("Failed to Deinitialize() GStreamerPlayerImplementation");
                    }
                    else
                    {
                        LOGINFO("GStreamerPlayerImplementation Deinitialize() successfully");
                    }
                    _configuration->Release();
                    _configuration = nullptr;
                }
                // Stop processing:
                RPC::IRemoteConnection *connection = service->RemoteConnection(_connectionId);
                VARIABLE_IS_NOT_USED uint32_t result = _gstreamerPlayer->Release();

                // It should have been the last reference we are releasing,
                // so it should endup in a DESTRUCTION_SUCCEEDED, if not we
                // are leaking...
                ASSERT(result == Core::ERROR_DESTRUCTION_SUCCEEDED);

                // If this was running in a (container) process...
                if (nullptr != connection)
                {
                    // Lets trigger the cleanup sequence for out-of-process code. Which will guard that unwilling processes, get shot if not stopped friendly :-)
                    connection->Terminate();
                    connection->Release();
                }
                // Make sure the Activated and Deactivated are no longer called before we start cleaning up..
                if (nullptr != _service)
                {
                    _service->Unregister(&_gstreamerPlayerNotification);
                    _service->Release();
                    _service = nullptr;
                }
                _gstreamerPlayer = nullptr;
            }
            _connectionId = 0;
            SYSLOG(Logging::Shutdown, (string(_T("GStreamerPlayer de-initialised"))));
        }

        string GStreamerPlayer::Information() const
        {
            return ("GStreamerPlayer plugin provides media playback control using GStreamer pipeline");
        }

        void GStreamerPlayer::Deactivated(RPC::IRemoteConnection *connection)
        {
            if (connection->Id() == _connectionId)
            {
                ASSERT(nullptr != _service);
                Core::IWorkerPool::Instance().Submit(PluginHost::IShell::Job::Create(_service, PluginHost::IShell::DEACTIVATED, PluginHost::IShell::FAILURE));
            }
        }
    } // namespace Plugin
} // namespace WPEFramework
