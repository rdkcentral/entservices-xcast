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

#pragma once

#include "Module.h"
#include <interfaces/Ids.h>
#include <interfaces/IGStreamerPlayer.h>
#include <interfaces/IConfiguration.h>
 
#include <com/com.h>
#include <core/core.h>
#include <mutex>
#include <glib.h>
#include <gst/gst.h>

#include "UtilsLogging.h"

namespace WPEFramework
{
    namespace Plugin
    {
        class GStreamerPlayerImplementation : public Exchange::IGStreamerPlayer, public Exchange::IConfiguration
        {
        public:
            GStreamerPlayerImplementation();
            ~GStreamerPlayerImplementation() override;

            // We do not allow this plugin to be copied
            GStreamerPlayerImplementation(const GStreamerPlayerImplementation&) = delete;
            GStreamerPlayerImplementation& operator=(const GStreamerPlayerImplementation&) = delete;

            // IGStreamerPlayer methods
            Core::hresult Register(Exchange::IGStreamerPlayer::INotification* sink) override;
            Core::hresult Unregister(Exchange::IGStreamerPlayer::INotification* sink) override;
            
            Core::hresult SetURL(const string& url, Exchange::IGStreamerPlayer::PlayerSuccess& success) override;
            Core::hresult Play(Exchange::IGStreamerPlayer::PlayerSuccess& success) override;
            Core::hresult Pause(Exchange::IGStreamerPlayer::PlayerSuccess& success) override;
            Core::hresult Stop(Exchange::IGStreamerPlayer::PlayerSuccess& success) override;
            Core::hresult GetState(Exchange::IGStreamerPlayer::PipelineState& state, bool& success) override;

            // IConfiguration methods
            uint32_t Configure(PluginHost::IShell* service) override;

            BEGIN_INTERFACE_MAP(GStreamerPlayerImplementation)
            INTERFACE_ENTRY(Exchange::IGStreamerPlayer)
            INTERFACE_ENTRY(Exchange::IConfiguration)
            END_INTERFACE_MAP

        private:
            // GStreamer callbacks
            static GstBusSyncReply bus_sync_handler(GstBus* bus, GstMessage* msg, gpointer user_data);
            void handleBusMessage(GstMessage* msg);
            
            // State management
            void notifyStateChange(Exchange::IGStreamerPlayer::PipelineState newState, Exchange::IGStreamerPlayer::ErrorCode error = Exchange::IGStreamerPlayer::ErrorCode::NONE);
            Exchange::IGStreamerPlayer::PipelineState gstStateToPipelineState(GstState gstState);
            
            // Initialization and cleanup
            bool initializeGStreamer();
            void cleanupGStreamer();

        private:
            PluginHost::IShell* _service;
            mutable Core::CriticalSection _adminLock;
            std::list<Exchange::IGStreamerPlayer::INotification*> _notifications;
            
            // GStreamer pipeline elements
            GstElement* _pipeline;
            GstBus* _bus;
            string _currentUrl;
            Exchange::IGStreamerPlayer::PipelineState _currentState;
            
            // GStreamer initialized flag
            static bool _gstreamerInitialized;
        };
    } // namespace Plugin
} // namespace WPEFramework
