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

#include "GStreamerPlayerImplementation.h"

namespace WPEFramework {
    namespace Plugin {

        // Register this class so Thunder's COM-RPC layer can create it when
        // the proxy (GStreamerPlayer.cpp) calls _service->Root<>().
        SERVICE_REGISTRATION(GStreamerPlayerImplementation, 1, 0);

        // =====================================================================
        // Constructor / Destructor
        // =====================================================================

        GStreamerPlayerImplementation::GStreamerPlayerImplementation()
            : _adminLock()
            , _notificationClients()
            , _pipeline(nullptr)
            , _uridecodebin(nullptr)
            , _audioQueue(nullptr)
            , _audioSink(nullptr)
            , _videoQueue(nullptr)
            , _videoSink(nullptr)
            , _mainLoop(nullptr)
            , _mainLoopThread()
        {
            // Initialise GStreamer once for this process.
            gst_init(nullptr, nullptr);
            SYSLOG(Logging::Startup, (_T("GStreamerPlayerImplementation: GStreamer initialised")));
        }

        GStreamerPlayerImplementation::~GStreamerPlayerImplementation()
        {
            // Make sure the pipeline is torn down cleanly before we die.
            if (_pipeline != nullptr) {
                DestroyPipeline();
            }
            SYSLOG(Logging::Shutdown, (_T("GStreamerPlayerImplementation Destructor")));
        }

        // =====================================================================
        // Register / Unregister notification clients
        // =====================================================================

        Core::hresult GStreamerPlayerImplementation::Register(IGStreamerPlayer::INotification* sink)
        {
            ASSERT(sink != nullptr);

            _adminLock.Lock();
            // Only add if not already registered.
            auto it = std::find(_notificationClients.begin(), _notificationClients.end(), sink);
            if (it == _notificationClients.end()) {
                sink->AddRef();
                _notificationClients.push_back(sink);
            }
            _adminLock.Unlock();

            return Core::ERROR_NONE;
        }

        Core::hresult GStreamerPlayerImplementation::Unregister(IGStreamerPlayer::INotification* sink)
        {
            ASSERT(sink != nullptr);

            _adminLock.Lock();
            auto it = std::find(_notificationClients.begin(), _notificationClients.end(), sink);
            if (it != _notificationClients.end()) {
                (*it)->Release();
                _notificationClients.erase(it);
            }
            _adminLock.Unlock();

            return Core::ERROR_NONE;
        }

        // =====================================================================
        // Play
        // =====================================================================

        Core::hresult GStreamerPlayerImplementation::Play(const string& uri)
        {
            LOGINFO("GStreamerPlayer::Play uri=%s", uri.c_str());

            // If a pipeline is already running, stop it first.
            if (_pipeline != nullptr) {
                DestroyPipeline();
            }

            // -----------------------------------------------------------------
            // Build the pipeline manually (not playbin).
            //
            // Pipeline topology:
            //
            //   uridecodebin --[pad-added signal]--+
            //                                      +--> videoQueue --> westerossink
            //                                      +--> audioQueue --> autoaudiosink
            //
            // uridecodebin handles downloading, demuxing and decoding.
            // It exposes new "src" pads dynamically when it knows the stream
            // type, so we connect to the "pad-added" signal and link there.
            // -----------------------------------------------------------------

            _pipeline     = gst_pipeline_new("gstreamer-player");
            _uridecodebin = gst_element_factory_make("uridecodebin",  "source");
            _audioQueue   = gst_element_factory_make("queue",         "audioqueue");
            _audioSink    = gst_element_factory_make("autoaudiosink", "audiosink");
            _videoQueue   = gst_element_factory_make("queue",         "videoqueue");
            _videoSink    = gst_element_factory_make("westerossink",  "videosink");

            if (!_pipeline || !_uridecodebin || !_audioQueue || !_audioSink
                           || !_videoQueue   || !_videoSink) {
                LOGERR("GStreamerPlayer::Play: Failed to create one or more GStreamer elements");
                DestroyPipeline();
                return Core::ERROR_GENERAL;
            }

            // Add every element into the pipeline bin so it manages their lifetime.
            gst_bin_add_many(GST_BIN(_pipeline),
                             _uridecodebin,
                             _audioQueue, _audioSink,
                             _videoQueue, _videoSink,
                             nullptr);

            // Link the static parts of the pipeline (queue -> sink).
            // The uridecodebin -> queue links are made in OnPadAdded() below.
            gst_element_link(_audioQueue, _audioSink);
            gst_element_link(_videoQueue, _videoSink);

            // Tell uridecodebin which content to fetch.
            g_object_set(_uridecodebin, "uri", uri.c_str(), nullptr);

            // When uridecodebin has decoded pads ready, OnPadAdded() will
            // link them to the correct queue.
            g_signal_connect(_uridecodebin, "pad-added",
                             G_CALLBACK(GStreamerPlayerImplementation::OnPadAdded), this);

            // Start a GMainLoop in a background thread.
            // GStreamer needs a running GLib main loop to dispatch bus messages
            // (errors, EOS, state-change notifications) asynchronously.
            _mainLoop = g_main_loop_new(nullptr, FALSE);
            _mainLoopThread = std::thread([this]() {
                g_main_loop_run(_mainLoop);
            });

            // Start playback.
            GstStateChangeReturn ret = gst_element_set_state(_pipeline, GST_STATE_PLAYING);
            if (ret == GST_STATE_CHANGE_FAILURE) {
                LOGERR("GStreamerPlayer::Play: Pipeline failed to transition to PLAYING");
                DestroyPipeline();
                return Core::ERROR_GENERAL;
            }

            LOGINFO("GStreamerPlayer::Play: pipeline started successfully");
            FirePlayerInitialized();
            return Core::ERROR_NONE;
        }

        // =====================================================================
        // Pause
        // =====================================================================

        Core::hresult GStreamerPlayerImplementation::Pause()
        {
            if (_pipeline == nullptr) {
                LOGERR("GStreamerPlayer::Pause: No pipeline is running");
                return Core::ERROR_ILLEGAL_STATE;
            }

            LOGINFO("GStreamerPlayer::Pause");
            gst_element_set_state(_pipeline, GST_STATE_PAUSED);
            return Core::ERROR_NONE;
        }

        // =====================================================================
        // SetResolution
        // =====================================================================

        Core::hresult GStreamerPlayerImplementation::SetResolution(
            const uint32_t x, const uint32_t y,
            const uint32_t width, const uint32_t height)
        {
            if (_videoSink == nullptr) {
                LOGERR("GStreamerPlayer::SetResolution: Video sink is not ready");
                return Core::ERROR_ILLEGAL_STATE;
            }

            LOGINFO("GStreamerPlayer::SetResolution x=%u y=%u width=%u height=%u",
                    x, y, width, height);

            // Move and resize the video window on the Westeros compositor.
            // "window-set" must be TRUE for position/size properties to take effect.
            g_object_set(_videoSink,
                         "window-set", TRUE,
                         "x",          static_cast<gint>(x),
                         "y",          static_cast<gint>(y),
                         "width",      static_cast<gint>(width),
                         "height",     static_cast<gint>(height),
                         nullptr);

            return Core::ERROR_NONE;
        }

        // =====================================================================
        // Stop
        // =====================================================================

        Core::hresult GStreamerPlayerImplementation::Stop()
        {
            if (_pipeline == nullptr) {
                LOGERR("GStreamerPlayer::Stop: No pipeline is running");
                return Core::ERROR_ILLEGAL_STATE;
            }

            LOGINFO("GStreamerPlayer::Stop: stopping pipeline");
            DestroyPipeline();
            FirePlayerStopped();
            return Core::ERROR_NONE;
        }

        // =====================================================================
        // Private helpers
        // =====================================================================

        /**
         * Called by GStreamer on the streaming thread whenever uridecodebin
         * exposes a newly decoded pad.
         *
         * We check the pad's media type:
         *  - "audio/x-raw" -> link to audioQueue sink pad
         *  - "video/x-raw" -> link to videoQueue sink pad
         */
        /* static */
        void GStreamerPlayerImplementation::OnPadAdded(
            GstElement* /* src */, GstPad* newPad, gpointer userData)
        {
            GStreamerPlayerImplementation* self =
                static_cast<GStreamerPlayerImplementation*>(userData);

            // Inspect the capabilities of the new pad to determine stream type.
            GstCaps*      caps      = gst_pad_get_current_caps(newPad);
            GstStructure* structure = gst_caps_get_structure(caps, 0);
            const gchar*  mediaType = gst_structure_get_name(structure);

            GstElement* targetQueue = nullptr;

            if (g_str_has_prefix(mediaType, "video/x-raw")) {
                targetQueue = self->_videoQueue;
                LOGINFO("GStreamerPlayer::OnPadAdded: linking video pad");
            } else if (g_str_has_prefix(mediaType, "audio/x-raw")) {
                targetQueue = self->_audioQueue;
                LOGINFO("GStreamerPlayer::OnPadAdded: linking audio pad");
            } else {
                // Unknown / unsupported pad type – skip it.
                LOGINFO("GStreamerPlayer::OnPadAdded: skipping pad with type '%s'", mediaType);
                gst_caps_unref(caps);
                return;
            }

            // Get the queue's sink pad and link newPad to it (unless already linked).
            GstPad* sinkPad = gst_element_get_static_pad(targetQueue, "sink");
            if (!gst_pad_is_linked(sinkPad)) {
                GstPadLinkReturn linkRet = gst_pad_link(newPad, sinkPad);
                if (linkRet != GST_PAD_LINK_OK) {
                    LOGERR("GStreamerPlayer::OnPadAdded: pad link failed for type '%s' (ret=%d)",
                           mediaType, static_cast<int>(linkRet));
                }
            }

            gst_object_unref(sinkPad);
            gst_caps_unref(caps);
        }

        /**
         * Bring the pipeline down to GST_STATE_NULL, release all GStreamer
         * resources, and stop the GMainLoop thread.
         */
        void GStreamerPlayerImplementation::DestroyPipeline()
        {
            if (_pipeline != nullptr) {
                // GST_STATE_NULL causes the pipeline to release device handles,
                // file descriptors and network connections.
                gst_element_set_state(_pipeline, GST_STATE_NULL);

                // gst_object_unref on the pipeline releases the pipeline and all
                // child elements that were added with gst_bin_add_many.
                gst_object_unref(_pipeline);
                _pipeline     = nullptr;

                // These pointers were owned by the pipeline – null them out so
                // we don't accidentally dereference them.
                _uridecodebin = nullptr;
                _audioQueue   = nullptr;
                _audioSink    = nullptr;
                _videoQueue   = nullptr;
                _videoSink    = nullptr;
            }

            // Stop the GMainLoop and wait for the thread to exit.
            if (_mainLoop != nullptr) {
                g_main_loop_quit(_mainLoop);
                if (_mainLoopThread.joinable()) {
                    _mainLoopThread.join();
                }
                g_main_loop_unref(_mainLoop);
                _mainLoop = nullptr;
            }
        }

        void GStreamerPlayerImplementation::FirePlayerInitialized()
        {
            _adminLock.Lock();
            for (auto* client : _notificationClients) {
                client->OnPlayerInitialized();
            }
            _adminLock.Unlock();
        }

        void GStreamerPlayerImplementation::FirePlayerStopped()
        {
            _adminLock.Lock();
            for (auto* client : _notificationClients) {
                client->OnPlayerStopped();
            }
            _adminLock.Unlock();
        }

    } // namespace Plugin
} // namespace WPEFramework
