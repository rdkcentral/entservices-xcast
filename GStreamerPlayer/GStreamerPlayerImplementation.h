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
 *   urisourcebin --[pad-added]--+--> h264parse --> videoQueue --> westerossink
 *                               \--> decodebin --[pad-added]--> audioQueue --> audioconvert --> audioresample --> autoaudiosink
 *
 * The GMainLoop runs in its own thread so that GStreamer can dispatch bus
 * messages (errors, EOS, state-changes) without blocking the COM-RPC thread.
 *
 * Pipeline topology:
 *
 *   urisourcebin --[pad-added]--+--> h264parse --> videoQueue --> westerossink
 *                               \--> decodebin --[pad-added]--> audioQueue --> audioconvert --> audioresample --> autoaudiosink
 *
 * urisourcebin exposes elementary streams (H.264, AAC) without decoding.
 * westerossink performs hardware H.264 decode; h264parse feeds it the bitstream.
 * decodebin software-decodes compressed audio to PCM (audio/x-raw).
 * Queue elements decouple streaming threads from sink threads, preventing
 * deadlocks during dynamic pad linking.
 */

#pragma once

#include "Module.h"
#include <interfaces/IGStreamerPlayer.h>
#include "UtilsLogging.h"

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
            // GStreamer callback: called by urisourcebin when it exposes a new elementary-stream
            // pad.  Routes video/x-h264 to h264parse and audio/* to decodebin.
            static void OnPadAdded(GstElement* src, GstPad* newPad, gpointer userData);

            // GStreamer callback: called by decodebin when it exposes a decoded audio pad.
            // Links audio/x-raw to audioQueue.
            static void OnDecodebinPadAdded(GstElement* src, GstPad* newPad, gpointer userData);

            // GStreamer bus watch: dispatches pipeline messages (ASYNC_DONE, ERROR, EOS)
            // from the GMainLoop thread to the appropriate notification handler.
            static gboolean OnBusMessage(GstBus* bus, GstMessage* message, gpointer userData);

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
            GstElement* _pipeline;       // top-level GstPipeline
            GstElement* _source;         // urisourcebin: fetches URI; emits pad-added for elementary streams
            GstElement* _decodebin;      // decodebin: software-decodes compressed audio; emits pad-added for audio/x-raw
            GstElement* _h264parse;      // h264parse: parses H.264 bitstream for westerossink
            GstElement* _videoQueue;     // decouples h264parse streaming thread from westerossink thread
            GstElement* _videoSink;      // westerossink: hardware H.264 decode + render via Westeros
            GstElement* _audioQueue;     // decouples decodebin streaming thread from audio sink thread
            GstElement* _audioConvert;   // converts PCM format for autoaudiosink
            GstElement* _audioResample;  // resamples to the rate autoaudiosink requires
            GstElement* _audioSink;      // autoaudiosink: picks the best audio output

            // --- GMainLoop for bus messages ---
            // GStreamer dispatches errors, EOS and state-change messages on this loop.
            GMainLoop*  _mainLoop;
            std::thread _mainLoopThread;
            guint       _busWatchId;    // ID returned by gst_bus_add_watch(); 0 when inactive
        };

    } // namespace Plugin
} // namespace WPEFramework
