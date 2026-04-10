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
#include <cstdlib>
#include <cstring>

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
            , _source(nullptr)
            , _demuxer(nullptr)
            , _h264parser(nullptr)
            , _videoQueue(nullptr)
            , _videoSink(nullptr)
            , _decodebin(nullptr)
            , _audioConvert(nullptr)
            , _audioResample(nullptr)
            , _audioQueue(nullptr)
            , _audioSink(nullptr)
            , _mainLoop(nullptr)
            , _mainLoopThread()
            , _busWatchId(0)
        {
            // Check and log current environment variables before setting
            const char* xdgBefore = getenv("XDG_RUNTIME_DIR");
            const char* waylandBefore = getenv("WAYLAND_DISPLAY");
            SYSLOG(Logging::Startup, (_T("GStreamerPlayerImplementation: BEFORE - XDG_RUNTIME_DIR=%s, WAYLAND_DISPLAY=%s"), 
                xdgBefore ? xdgBefore : "NULL", waylandBefore ? waylandBefore : "NULL"));

            // Set environment variables for Wayland/Westeros
            int xdgResult = setenv("XDG_RUNTIME_DIR", "/tmp", 1);
            int waylandResult = setenv("WAYLAND_DISPLAY", "main0", 1);
            
            // Verify environment variables were actually set
            const char* xdgAfter = getenv("XDG_RUNTIME_DIR");
            const char* waylandAfter = getenv("WAYLAND_DISPLAY");
            
            if (xdgResult == 0 && xdgAfter != nullptr && strcmp(xdgAfter, "/tmp") == 0) {
                SYSLOG(Logging::Startup, (_T("GStreamerPlayerImplementation:  XDG_RUNTIME_DIR set successfully to: %s"), xdgAfter));
            } else {
                SYSLOG(Logging::Error, (_T("GStreamerPlayerImplementation:  Failed to set XDG_RUNTIME_DIR (setenv result=%d, actual value=%s)"), 
                    xdgResult, xdgAfter ? xdgAfter : "NULL"));
            }
            
            if (waylandResult == 0 && waylandAfter != nullptr && strcmp(waylandAfter, "main0") == 0) {
                SYSLOG(Logging::Startup, (_T("GStreamerPlayerImplementation: ✓ WAYLAND_DISPLAY set successfully to: %s"), waylandAfter));
            } else {
                SYSLOG(Logging::Error, (_T("GStreamerPlayerImplementation: ✗ Failed to set WAYLAND_DISPLAY (setenv result=%d, actual value=%s)"), 
                    waylandResult, waylandAfter ? waylandAfter : "NULL"));
            }

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
            //   filesrc -> qtdemux --[pad-added]--+--> h264parse -> video_queue -> westerossink
            //                                     \--> decodebin --[pad-added]--> audioconvert -> audioresample -> audio_queue -> autoaudiosink
            //
            // WHY queues?
            //   qtdemux and decodebin push buffers on their own streaming threads.
            //   westerossink and autoaudiosink run on separate sink threads.
            //   Without a queue between them, gst_pad_link() called from the
            //   pad-added callback (streaming thread) races with the sink thread
            //   and causes silent frame drops or deadlocks.
            //   queue provides a thread-safe buffer between the two sides.
            // -----------------------------------------------------------------

            _pipeline      = gst_pipeline_new("gstreamer-player");
            _source        = gst_element_factory_make("filesrc",        "source");
            _demuxer       = gst_element_factory_make("qtdemux",        "demuxer");
            _h264parser    = gst_element_factory_make("h264parse",      "h264parser");
            _videoQueue    = gst_element_factory_make("queue",          "video_queue");
            _videoSink     = gst_element_factory_make("westerossink",   "videosink");
            _decodebin     = gst_element_factory_make("decodebin",      "decodebin");
            _audioConvert  = gst_element_factory_make("audioconvert",   "audioconvert");
            _audioResample = gst_element_factory_make("audioresample",  "audioresample");
            _audioQueue    = gst_element_factory_make("queue",          "audio_queue");
            _audioSink     = gst_element_factory_make("autoaudiosink",  "audiosink");

            if (!_pipeline || !_source || !_demuxer
                           || !_h264parser || !_videoQueue || !_videoSink
                           || !_decodebin || !_audioConvert || !_audioResample || !_audioQueue || !_audioSink) {
                LOGERR("GStreamerPlayer::Play: Failed to create one or more GStreamer elements");
                DestroyPipeline();
                return Core::ERROR_GENERAL;
            }

            // Add every element into the pipeline bin so it manages their lifetime.
            gst_bin_add_many(GST_BIN(_pipeline),
                             _source, _demuxer,
                             _h264parser, _videoQueue, _videoSink,
                             _decodebin, _audioConvert, _audioResample, _audioQueue, _audioSink,
                             nullptr);

            // Link the static chains.
            // filesrc -> qtdemux (qtdemux dynamic pads linked in OnPadAdded)
            if (!gst_element_link(_source, _demuxer)) {
                LOGERR("GStreamerPlayer::Play: Failed to link filesrc -> qtdemux");
                DestroyPipeline();
                return Core::ERROR_GENERAL;
            }
            LOGINFO("GStreamerPlayer::Play: filesrc -> qtdemux linked successfully");

            // Link static video elements: h264parse -> video_queue -> westerossink
            if (!gst_element_link_many(_h264parser, _videoQueue, _videoSink, nullptr)) {
                LOGERR("GStreamerPlayer::Play: Failed to link h264parse -> video_queue -> westerossink");
                DestroyPipeline();
                return Core::ERROR_GENERAL;
            }
            LOGINFO("GStreamerPlayer::Play: h264parse -> video_queue -> westerossink linked successfully");

            // Link static audio elements: audioconvert -> audioresample -> audio_queue -> autoaudiosink
            if (!gst_element_link_many(_audioConvert, _audioResample, _audioQueue, _audioSink, nullptr)) {
                LOGERR("GStreamerPlayer::Play: Failed to link audioconvert -> audioresample -> audio_queue -> autoaudiosink");
                DestroyPipeline();
                return Core::ERROR_GENERAL;
            }
            LOGINFO("GStreamerPlayer::Play: audioconvert -> audioresample -> audio_queue -> autoaudiosink linked successfully");

            // Set file location on filesrc - extract path from URI
            // If URI starts with "file://", strip it; otherwise use as-is
            std::string filePath = uri;
            if (filePath.find("file://") == 0) {
                filePath = filePath.substr(7);  // Remove "file://" prefix
            }
            g_object_set(_source, "location", filePath.c_str(), nullptr);
            LOGINFO("GStreamerPlayer::Play: Set file location to %s", filePath.c_str());

            // When qtdemux has demuxed pads ready, OnPadAdded() will
            // link them to h264parse or decodebin.
            g_signal_connect(_demuxer, "pad-added",
                             G_CALLBACK(GStreamerPlayerImplementation::OnPadAdded), this);

            // When decodebin has decoded audio pads ready, OnDecodebinPadAdded() will
            // link them to audioconvert.
            g_signal_connect(_decodebin, "pad-added",
                             G_CALLBACK(GStreamerPlayerImplementation::OnDecodebinPadAdded), this);

            // Attach a bus watch so that GStreamer messages (ASYNC_DONE, ERROR, EOS)
            // are dispatched on the GMainLoop thread to OnBusMessage().
            // This is the correct place to fire OnPlayerInitialized – only after the
            // pipeline actually reaches PLAYING (ASYNC_DONE), not immediately after
            // gst_element_set_state() which returns ASYNC for network URIs.
            GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(_pipeline));
            _busWatchId = gst_bus_add_watch(bus, GStreamerPlayerImplementation::OnBusMessage, this);
            gst_object_unref(bus);

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

            // OnPlayerInitialized will be fired by OnBusMessage() when the bus posts
            // GST_MESSAGE_ASYNC_DONE, which means the pipeline has fully transitioned
            // to PLAYING and decoded pads have been linked.
            LOGINFO("GStreamerPlayer::Play: pipeline started, awaiting ASYNC_DONE");
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
            // westerossink expects window-set property in format: "x,y,width,height"
            // Format: "x,y,width,height" as a string (e.g., "0,0,1280,720")
            char windowSetValue[64];
            snprintf(windowSetValue, sizeof(windowSetValue), "%u,%u,%u,%u", x, y, width, height);
            
            g_object_set(_videoSink, "window-set", windowSetValue, nullptr);
            LOGINFO("GStreamerPlayer::SetResolution: Set window-set=\"%s\"", windowSetValue);

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
         * Called by GStreamer on the streaming thread whenever qtdemux
         * exposes a newly demuxed pad.
         *
         * We check the pad's media type and link to the correct element:
         *  - "video/x-h264" -> h264parser sink pad
         *  - "audio/*" -> decodebin sink pad
         */
        /* static */
        void GStreamerPlayerImplementation::OnPadAdded(
            GstElement* /* src */, GstPad* newPad, gpointer userData)
        {
            GStreamerPlayerImplementation* self =
                static_cast<GStreamerPlayerImplementation*>(userData);

            LOGINFO("\n=== QTDEMUX PAD ADDED HANDLER ===");

            // gst_pad_get_current_caps() can return NULL if caps are not yet
            // finalized when pad-added fires; fall back to querying allowed caps.
            GstCaps* caps = gst_pad_get_current_caps(newPad);
            if (!caps) {
                caps = gst_pad_query_caps(newPad, nullptr);
            }
            if (!caps) {
                LOGERR("GStreamerPlayer::OnPadAdded: could not determine caps for new pad");
                return;
            }

            GstStructure* structure = gst_caps_get_structure(caps, 0);
            const gchar*  mediaType = gst_structure_get_name(structure);

            // Log the full caps string so we can see the exact format/resolution/framerate.
            gchar* capsStr = gst_caps_to_string(caps);
            LOGINFO("GStreamerPlayer::OnPadAdded: new pad type='%s' full-caps='%s'",
                    mediaType, capsStr ? capsStr : "(null)");
            g_free(capsStr);

            GstElement* targetElement = nullptr;
            const gchar* targetName = nullptr;

            // Handle H.264 Video
            if (g_str_has_prefix(mediaType, "video/x-h264")) {
                targetElement = self->_h264parser;
                targetName    = "h264parse";
                LOGINFO("GStreamerPlayer::OnPadAdded: Detected H.264 video pad!");
            }
            // Handle other video formats (log warning)
            else if (g_str_has_prefix(mediaType, "video/")) {
                LOGERR("GStreamerPlayer::OnPadAdded: Detected video pad with format '%s', but only H.264 is supported. Ignoring.",
                       mediaType);
                gst_caps_unref(caps);
                return;
            }
            // Handle audio
            else if (g_str_has_prefix(mediaType, "audio/")) {
                targetElement = self->_decodebin;
                targetName    = "decodebin";
                LOGINFO("GStreamerPlayer::OnPadAdded: Detected audio pad '%s'.", mediaType);
            }
            // Unknown pad type
            else {
                LOGINFO("GStreamerPlayer::OnPadAdded: Unknown/unsupported pad type '%s'. Ignoring.", mediaType);
                gst_caps_unref(caps);
                return;
            }

            // Link newPad to the target element's sink pad (unless already linked).
            GstPad* sinkPad = gst_element_get_static_pad(targetElement, "sink");
            if (!sinkPad) {
                LOGERR("GStreamerPlayer::OnPadAdded: could not get sink pad from %s", targetName);
                gst_caps_unref(caps);
                return;
            }

            if (gst_pad_is_linked(sinkPad)) {
                LOGINFO("GStreamerPlayer::OnPadAdded: %s sink pad already linked, skipping", targetName);
            } else {
                GstPadLinkReturn linkRet = gst_pad_link(newPad, sinkPad);
                if (linkRet == GST_PAD_LINK_OK) {
                    LOGINFO("GStreamerPlayer::OnPadAdded: successfully linked '%s' pad to %s (ret=%d)",
                            mediaType, targetName, static_cast<int>(linkRet));
                    if (targetElement == self->_h264parser) {
                        LOGINFO("GStreamerPlayer::OnPadAdded: Video pipeline: qtdemux -> h264parse -> queue -> westerossink");
                    } else {
                        LOGINFO("GStreamerPlayer::OnPadAdded: Audio pipeline: qtdemux -> decodebin -> (will link to audioconvert)");
                    }
                } else {
                    LOGERR("GStreamerPlayer::OnPadAdded: FAILED to link '%s' pad to %s (ret=%d) – "
                           "ret meanings: -1=wrong hierarchy, -2=was linked, -3=wrong direction, "
                           "-4=no format match, -5=no peer, -6=refused",
                           mediaType, targetName, static_cast<int>(linkRet));
                }
            }

            gst_object_unref(sinkPad);
            gst_caps_unref(caps);
            LOGINFO("=== QTDEMUX PAD ADDED HANDLER EXIT ===\n");
        }

        /**
         * Called by GStreamer on the streaming thread whenever decodebin
         * exposes a newly decoded pad.
         *
         * We check the pad's media type and link to audioconvert:
         *  - "audio/x-raw" -> audioconvert sink pad
         */
        /* static */
        void GStreamerPlayerImplementation::OnDecodebinPadAdded(
            GstElement* /* src */, GstPad* newPad, gpointer userData)
        {
            GStreamerPlayerImplementation* self =
                static_cast<GStreamerPlayerImplementation*>(userData);

            LOGINFO("\n=== DECODEBIN PAD ADDED HANDLER ===");

            GstCaps* caps = gst_pad_get_current_caps(newPad);
            if (!caps) {
                caps = gst_pad_query_caps(newPad, nullptr);
            }
            if (!caps) {
                LOGERR("GStreamerPlayer::OnDecodebinPadAdded: could not determine caps for new pad");
                return;
            }

            GstStructure* structure = gst_caps_get_structure(caps, 0);
            const gchar*  mediaType = gst_structure_get_name(structure);

            gchar* capsStr = gst_caps_to_string(caps);
            LOGINFO("GStreamerPlayer::OnDecodebinPadAdded: new pad type='%s' full-caps='%s'",
                    mediaType, capsStr ? capsStr : "(null)");
            g_free(capsStr);

            // Handle raw audio
            if (g_str_has_prefix(mediaType, "audio/x-raw")) {
                LOGINFO("GStreamerPlayer::OnDecodebinPadAdded: Detected raw audio pad from decodebin!");

                GstPad* sinkPad = gst_element_get_static_pad(self->_audioConvert, "sink");
                if (!sinkPad) {
                    LOGERR("GStreamerPlayer::OnDecodebinPadAdded: could not get sink pad from audioconvert");
                    gst_caps_unref(caps);
                    return;
                }

                if (gst_pad_is_linked(sinkPad)) {
                    LOGINFO("GStreamerPlayer::OnDecodebinPadAdded: audioconvert sink pad already linked, skipping");
                } else {
                    GstPadLinkReturn linkRet = gst_pad_link(newPad, sinkPad);
                    if (linkRet == GST_PAD_LINK_OK) {
                        LOGINFO("GStreamerPlayer::OnDecodebinPadAdded: successfully linked decoded audio pad to audioconvert!");
                        LOGINFO("GStreamerPlayer::OnDecodebinPadAdded: Complete audio pipeline: decodebin -> audioconvert -> audioresample -> queue -> autoaudiosink");
                    } else {
                        LOGERR("GStreamerPlayer::OnDecodebinPadAdded: FAILED to link audio pad to audioconvert (ret=%d)",
                               static_cast<int>(linkRet));
                    }
                }
                gst_object_unref(sinkPad);
            } else {
                LOGINFO("GStreamerPlayer::OnDecodebinPadAdded: Ignoring non-raw-audio pad type '%s' from decodebin.", mediaType);
            }

            gst_caps_unref(caps);
            LOGINFO("=== DECODEBIN PAD ADDED HANDLER EXIT ===\n");
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

                // Remove the bus watch before unreffing the pipeline so it cannot
                // fire after the pipeline is gone.
                if (_busWatchId != 0) {
                    g_source_remove(_busWatchId);
                    _busWatchId = 0;
                }

                // gst_object_unref on the pipeline releases the pipeline and all
                // child elements that were added with gst_bin_add_many.
                gst_object_unref(_pipeline);
                _pipeline     = nullptr;

                // These pointers were owned by the pipeline – null them out so
                // we don't accidentally dereference them.
                _source        = nullptr;
                _demuxer       = nullptr;
                _h264parser    = nullptr;
                _videoQueue    = nullptr;
                _videoSink     = nullptr;
                _decodebin     = nullptr;
                _audioConvert  = nullptr;
                _audioResample = nullptr;
                _audioQueue    = nullptr;
                _audioSink     = nullptr;
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

        /**
         * GStreamer bus message handler – runs on the GMainLoop thread.
         *
         * GST_MESSAGE_ASYNC_DONE : the pipeline reached PLAYING and all pads
         *   are linked; safe to advertise that playback has started.
         * GST_MESSAGE_ERROR      : an unrecoverable pipeline error occurred;
         *   tear down the pipeline so resources are released.
         * GST_MESSAGE_EOS        : end-of-stream; notify clients and clean up.
         */
        /* static */
        gboolean GStreamerPlayerImplementation::OnBusMessage(
            GstBus* /* bus */, GstMessage* message, gpointer userData)
        {
            GStreamerPlayerImplementation* self =
                static_cast<GStreamerPlayerImplementation*>(userData);

            switch (GST_MESSAGE_TYPE(message)) {
                case GST_MESSAGE_ASYNC_DONE:
                    LOGINFO("GStreamerPlayer::OnBusMessage: ASYNC_DONE – pipeline is now PLAYING");
                    self->FirePlayerInitialized();
                    break;

                case GST_MESSAGE_ERROR: {
                    GError* err   = nullptr;
                    gchar*  debug = nullptr;
                    gst_message_parse_error(message, &err, &debug);
                    LOGERR("GStreamerPlayer::OnBusMessage: ERROR – %s (%s)",
                           err ? err->message : "unknown",
                           debug ? debug : "no debug info");
                    g_clear_error(&err);
                    g_free(debug);
                    self->DestroyPipeline();
                    break;
                }

                case GST_MESSAGE_EOS:
                    LOGINFO("GStreamerPlayer::OnBusMessage: EOS");
                    self->DestroyPipeline();
                    self->FirePlayerStopped();
                    break;

                default:
                    break;
            }

            // Returning TRUE keeps the watch active; FALSE would remove it.
            return TRUE;
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
