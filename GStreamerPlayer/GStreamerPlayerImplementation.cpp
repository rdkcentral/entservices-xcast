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
            , _audioConvert(nullptr)
            , _audioResample(nullptr)
            , _audioSink(nullptr)
            , _videoQueue(nullptr)
            , _videoConvert(nullptr)
            , _videoSink(nullptr)
            , _mainLoop(nullptr)
            , _mainLoopThread()
            , _busWatchId(0)
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
            //   uridecodebin --[pad-added]--+--> videoQueue --> videoconvert --> westerossink
            //                              \--> audioQueue --> audioconvert --> audioresample --> autoaudiosink
            //
            // WHY queues?
            //   uridecodebin pushes decoded buffers on its own streaming thread.
            //   westerossink and autoaudiosink run on separate sink threads.
            //   Without a queue between them, gst_pad_link() called from the
            //   pad-added callback (streaming thread) races with the sink thread
            //   and causes silent frame drops or deadlocks.
            //   queue provides a thread-safe buffer between the two sides.
            //
            // WHY videoconvert / audioconvert / audioresample?
            //   uridecodebin outputs whatever format the decoder produces
            //   (e.g. I420, NV12 for video; S16LE at 44100 Hz for audio).
            //   westerossink and autoaudiosink advertise their own preferred
            //   caps.  Without conversion elements, gst_pad_link() returns
            //   GST_PAD_LINK_NOFORMAT (ret=-4) when the formats don't match.
            // -----------------------------------------------------------------

            _pipeline      = gst_pipeline_new("gstreamer-player");
            _uridecodebin  = gst_element_factory_make("uridecodebin",  "source");
            _videoQueue    = gst_element_factory_make("queue",         "videoqueue");
            _videoConvert  = gst_element_factory_make("videoconvert",  "videoconvert");
            _videoSink     = gst_element_factory_make("waylandsink",  "videosink");
            _audioQueue    = gst_element_factory_make("queue",         "audioqueue");
            _audioConvert  = gst_element_factory_make("audioconvert",  "audioconvert");
            _audioResample = gst_element_factory_make("audioresample", "audioresample");
            _audioSink     = gst_element_factory_make("autoaudiosink", "audiosink");

            if (!_pipeline || !_uridecodebin
                           || !_videoQueue || !_videoConvert || !_videoSink
                           || !_audioQueue || !_audioConvert || !_audioResample || !_audioSink) {
                LOGERR("GStreamerPlayer::Play: Failed to create one or more GStreamer elements");
                DestroyPipeline();
                return Core::ERROR_GENERAL;
            }

            // Add every element into the pipeline bin so it manages their lifetime.
            gst_bin_add_many(GST_BIN(_pipeline),
                             _uridecodebin,
                             _videoQueue, _videoConvert, _videoSink,
                             _audioQueue, _audioConvert, _audioResample, _audioSink,
                             nullptr);

            // Link the static chains.
            // The uridecodebin -> queue links are made dynamically in OnPadAdded().
            if (!gst_element_link_many(_videoQueue, _videoConvert, _videoSink, nullptr)) {
                LOGERR("GStreamerPlayer::Play: Failed to link videoQueue -> videoconvert -> videosink");
                DestroyPipeline();
                return Core::ERROR_GENERAL;
            }
            LOGINFO("GStreamerPlayer::Play: videoQueue -> videoconvert -> videosink linked successfully");

            if (!gst_element_link_many(_audioQueue, _audioConvert, _audioResample, _audioSink, nullptr)) {
                LOGERR("GStreamerPlayer::Play: Failed to link audioQueue -> audioconvert -> audioresample -> autoaudiosink");
                DestroyPipeline();
                return Core::ERROR_GENERAL;
            }
            LOGINFO("GStreamerPlayer::Play: audioQueue -> audioconvert -> audioresample -> autoaudiosink linked successfully");

            // Tell uridecodebin which content to fetch.
            g_object_set(_uridecodebin, "uri", uri.c_str(), nullptr);

            // When uridecodebin has decoded pads ready, OnPadAdded() will
            // link them to the correct queue.
            g_signal_connect(_uridecodebin, "pad-added",
                             G_CALLBACK(GStreamerPlayerImplementation::OnPadAdded), this);

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
         * We check the pad's media type and link to the correct queue:
         *  - "video/x-raw" -> videoQueue sink pad
         *  - "audio/x-raw" -> audioQueue sink pad
         *
         * Linking to the queue (not directly to the converter) is essential:
         * it keeps the uridecodebin streaming thread decoupled from the sink
         * threads, preventing deadlocks and silent frame drops.
         */
        /* static */
        void GStreamerPlayerImplementation::OnPadAdded(
            GstElement* /* src */, GstPad* newPad, gpointer userData)
        {
            GStreamerPlayerImplementation* self =
                static_cast<GStreamerPlayerImplementation*>(userData);

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

            GstElement* targetQueue = nullptr;
            const gchar* targetName = nullptr;

            if (g_str_has_prefix(mediaType, "video/x-raw")) {
                targetQueue = self->_videoQueue;
                targetName  = "videoQueue";
            } else if (g_str_has_prefix(mediaType, "audio/x-raw")) {
                targetQueue = self->_audioQueue;
                targetName  = "audioQueue";
            } else {
                // Unknown / unsupported pad type – skip it.
                LOGINFO("GStreamerPlayer::OnPadAdded: skipping unsupported pad type '%s'", mediaType);
                gst_caps_unref(caps);
                return;
            }

            // Link newPad to the queue's sink pad (unless already linked).
            GstPad* sinkPad = gst_element_get_static_pad(targetQueue, "sink");
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
                } else {
                    LOGERR("GStreamerPlayer::OnPadAdded: FAILED to link '%s' pad to %s (ret=%d) – "
                           "ret meanings: -1=wrong hierarchy, -2=was linked, -3=wrong direction, "
                           "-4=no format match, -5=no peer, -6=refused",
                           mediaType, targetName, static_cast<int>(linkRet));
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
                _uridecodebin  = nullptr;
                _videoQueue    = nullptr;
                _videoConvert  = nullptr;
                _videoSink     = nullptr;
                _audioQueue    = nullptr;
                _audioConvert  = nullptr;
                _audioResample = nullptr;
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
