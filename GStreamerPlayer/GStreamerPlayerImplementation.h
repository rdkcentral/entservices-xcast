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
 * @file GStreamerPlayerImplementation.h
 * @brief Out-of-process implementation of IGStreamerPlayer.
 *
 * This class runs in a separate WPEProcess host.  It owns a manually built
 * GStreamer pipeline:
 *
 *   uridecodebin --[pad-added]--> videoqueue --> westerossink
 *                              -> audioqueue --> autoaudiosink
 *
 * The GMainLoop runs in its own thread so that GStreamer can dispatch bus
 * messages (errors, EOS, state-changes) without blocking the COM-RPC thread.
 */

#pragma once

#include "Module.h"
#include <interfaces/IGStreamerPlayer.h>

#include <com/com.h>
#include <core/core.h>

#include <list>
#include <thread>

#include <gst/gst.h>

namespace WPEFramework {
    namespace Plugin {

        class GStreamerPlayerImplementation : public Exchange::IGStreamerPlayer {
        public:
            GStreamerPlayerImplementation();
            ~GStreamerPlayerImplementation() override;

            GStreamerPlayerImplementation(const GStreamerPlayerImplementation&)            = delete;
            GStreamerPlayerImplementation& operator=(const GStreamerPlayerImplementation&) = delete;

            BEGIN_INTERFACE_MAP(GStreamerPlayerImplementation)
            INTERFACE_ENTRY(Exchange::IGStreamerPlayer)
            END_INTERFACE_MAP

            // ----- IGStreamerPlayer -----
            Core::hresult Register(IGStreamerPlayer::INotification* sink) override;
            Core::hresult Unregister(IGStreamerPlayer::INotification* sink) override;

            Core::hresult Play(const string& uri) override;
            Core::hresult Pause() override;
            Core::hresult SetResolution(const uint32_t x, const uint32_t y,
                                        const uint32_t width, const uint32_t height) override;
            Core::hresult Stop() override;

        private:
            // GStreamer callback: called whenever uridecodebin exposes a new decoded pad.
            // We inspect the pad caps and link it to either the video or audio queue.
            static void OnPadAdded(GstElement* src, GstPad* newPad, gpointer userData);

            // Bring the pipeline to GST_STATE_NULL, unref all elements, and stop the
            // GMainLoop thread.  Safe to call even if no pipeline has been created yet.
            void DestroyPipeline();

            // Helpers that iterate _notificationClients and fire the named event.
            void FirePlayerInitialized();
            void FirePlayerStopped();

        private:
            mutable Core::CriticalSection                       _adminLock;
            std::list<Exchange::IGStreamerPlayer::INotification*> _notificationClients;

            // --- GStreamer pipeline elements ---
            // All element pointers below are owned by _pipeline (via gst_bin_add_many).
            // After gst_object_unref(_pipeline) they become dangling; we NULL them out
            // immediately in DestroyPipeline().
            GstElement* _pipeline;      // top-level GstPipeline
            GstElement* _uridecodebin;  // decodes any URI; emits pad-added signals
            GstElement* _audioQueue;    // small queue buffer before the audio sink
            GstElement* _audioSink;     // autoaudiosink: picks the best audio output
            GstElement* _videoQueue;    // small queue buffer before the video sink
            GstElement* _videoSink;     // westerossink: renders video via Westeros

            // --- GMainLoop for bus messages ---
            // GStreamer dispatches errors, EOS and state-change messages on this loop.
            GMainLoop*  _mainLoop;
            std::thread _mainLoopThread;
        };

    } // namespace Plugin
} // namespace WPEFramework
