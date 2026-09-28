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
 * @file PlaybinPluginImplementation.h
 * @brief Out-of-process implementation of IPlaybinPlugin.
 *
 * This class runs in a separate WPEProcess host.  Unlike GStreamerPlayerImplementation,
 * it does not wire up individual source/demux/parse/decode/sink elements. It owns a
 * single GStreamer `playbin` element, which internally selects and builds the
 * source -> demux -> decode -> sink chain for whatever URI/codec combination the
 * target's installed GStreamer plugins support.
 *
 * The GMainLoop runs in its own thread so that GStreamer can dispatch bus
 * messages (errors, EOS, state-changes) without blocking the COM-RPC thread.
 */

#pragma once

#include "Module.h"
#include <interfaces/IPlaybinPlugin.h>
#include "UtilsLogging.h"

#include <com/com.h>
#include <core/core.h>

#include <list>
#include <string>
#include <thread>

#include <gst/gst.h>

namespace WPEFramework {
    namespace Plugin {

        class PlaybinPluginImplementation : public Exchange::IPlaybinPlugin {
        public:
            PlaybinPluginImplementation();
            ~PlaybinPluginImplementation() override;

            PlaybinPluginImplementation(const PlaybinPluginImplementation&)            = delete;
            PlaybinPluginImplementation& operator=(const PlaybinPluginImplementation&) = delete;

            BEGIN_INTERFACE_MAP(PlaybinPluginImplementation)
            INTERFACE_ENTRY(Exchange::IPlaybinPlugin)
            END_INTERFACE_MAP

            // ----- IPlaybinPlugin -----
            Core::hresult Register(IPlaybinPlugin::INotification* sink) override;
            Core::hresult Unregister(IPlaybinPlugin::INotification* sink) override;

            Core::hresult Configure(const string& media) override;
            Core::hresult Play() override;
            Core::hresult Pause() override;
            Core::hresult Stop() override;

        private:
            // Normalizes a bare local file path (no "://" scheme) to a file:// URI.
            // Any location that already contains a scheme is returned unchanged.
            // Returns false if media is empty.
            static bool NormalizeToUri(const string& media, string& uriOut);

            // GStreamer bus watch: dispatches pipeline messages (ASYNC_DONE, ERROR, EOS)
            // from the GMainLoop thread to the appropriate notification handler.
            static gboolean OnBusMessage(GstBus* bus, GstMessage* message, gpointer userData);

            // Bring the pipeline to GST_STATE_NULL, unref it, and stop the GMainLoop
            // thread. Does NOT clear _configuredUri. Safe to call even if no pipeline
            // has been created yet.
            void DestroyPipeline();

            // Helpers that iterate _notificationClients and fire the named event.
            void FirePlayerInitialized();
            void FirePlayerStopped();
            void FirePlayerError(const string& message);

        private:
            mutable Core::CriticalSection                     _adminLock;
            std::list<Exchange::IPlaybinPlugin::INotification*> _notificationClients;

            // Media configured via Configure(). Preserved across Stop() so that a
            // subsequent Play() can recreate a pipeline without re-configuring.
            std::string _configuredUri;
            bool        _hasConfiguredMedia;

            // --- GStreamer pipeline element ---
            // Single playbin element; playbin owns and manages its internal
            // source/demux/decode/sink chain.
            GstElement* _pipeline;

            // Set once PLAYING has been reached for the current pipeline instance,
            // so OnPlayerInitialized fires only once per Play()/pipeline lifetime.
            bool _playerInitializedFired;

            // --- GMainLoop for bus messages ---
            // GStreamer dispatches errors, EOS and state-change messages on this loop.
            GMainLoop*  _mainLoop;
            std::thread _mainLoopThread;
            guint       _busWatchId; // ID returned by gst_bus_add_watch(); 0 when inactive
        };

    } // namespace Plugin
} // namespace WPEFramework
