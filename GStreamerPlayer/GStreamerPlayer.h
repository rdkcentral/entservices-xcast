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

/**
 * @file GStreamerPlayer.h
 * @brief Thunder out-of-process plugin: GStreamerPlayer
 *
 * This is the in-process plugin shell.  It lives in the Thunder framework
 * process and forwards every method call (and receives every notification)
 * across the COM-RPC boundary to / from GStreamerPlayerImplementation, which
 * runs in its own separate host process and owns the real GStreamer pipeline.
 */

#pragma once

#include "Module.h"
#include <interfaces/IGStreamerPlayer.h>
#include <interfaces/json/JsonData_GStreamerPlayer.h>
#include <interfaces/json/JGStreamerPlayer.h>
#include "UtilsLogging.h"
#include "tracing/Logging.h"

namespace WPEFramework {
    namespace Plugin {

        class GStreamerPlayer : public PluginHost::IPlugin, public PluginHost::JSONRPC {
        private:
            // ---------------------------------------------------------------
            // Notification sink: bridges COM-RPC events to JSON-RPC events
            // and monitors the remote connection lifecycle.
            // ---------------------------------------------------------------
            class Notification : public RPC::IRemoteConnection::INotification,
                                  public Exchange::IGStreamerPlayer::INotification {
            private:
                Notification()                          = delete;
                Notification(const Notification&)       = delete;
                Notification& operator=(const Notification&) = delete;

            public:
                explicit Notification(GStreamerPlayer* parent)
                    : _parent(*parent)
                {
                    ASSERT(parent != nullptr);
                }

                ~Notification() override = default;

                BEGIN_INTERFACE_MAP(Notification)
                INTERFACE_ENTRY(Exchange::IGStreamerPlayer::INotification)
                INTERFACE_ENTRY(RPC::IRemoteConnection::INotification)
                END_INTERFACE_MAP

                // ----- IGStreamerPlayer::INotification -----

                void OnPlayerInitialized() override
                {
                    LOGINFO("[EVENT] OnPlayerInitialized");
                    Exchange::JGStreamerPlayer::Event::OnPlayerInitialized(_parent);
                }

                void OnPlayerStopped() override
                {
                    LOGINFO("[EVENT] OnPlayerStopped");
                    Exchange::JGStreamerPlayer::Event::OnPlayerStopped(_parent);
                }

                // ----- RPC::IRemoteConnection::INotification -----

                void Activated(RPC::IRemoteConnection* /* connection */) override
                {
                }

                void Deactivated(RPC::IRemoteConnection* connection) override
                {
                    // If our out-of-process host has crashed, ask Thunder to
                    // deactivate this plugin so it can be restarted cleanly.
                    if (_parent._connectionId == connection->Id()) {
                        _parent.Deactivated(connection);
                    }
                }

            private:
                GStreamerPlayer& _parent;
            };

        public:
            GStreamerPlayer(const GStreamerPlayer&)            = delete;
            GStreamerPlayer& operator=(const GStreamerPlayer&) = delete;

            GStreamerPlayer();
            ~GStreamerPlayer() override;

            BEGIN_INTERFACE_MAP(GStreamerPlayer)
            INTERFACE_ENTRY(PluginHost::IPlugin)
            INTERFACE_ENTRY(PluginHost::IDispatcher)
            INTERFACE_AGGREGATE(Exchange::IGStreamerPlayer, _gstreamerPlayer)
            END_INTERFACE_MAP

            // ----- IPlugin -----
            const string Initialize(PluginHost::IShell* service) override;
            void         Deinitialize(PluginHost::IShell* service) override;
            string       Information() const override;

        private:
            // Called when the out-of-process host reports a fatal disconnect.
            void Deactivated(RPC::IRemoteConnection* connection);

        private:
            PluginHost::IShell*        _service{};
            uint32_t                   _connectionId{};
            Exchange::IGStreamerPlayer* _gstreamerPlayer{};
            Core::Sink<Notification>   _notification;
        };

    } // namespace Plugin
} // namespace WPEFramework
