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
            , _source(nullptr)
            , _decodebin(nullptr)
            , _h264parse(nullptr)
            , _videoQueue(nullptr)
            , _videoSink(nullptr)
            , _audioQueue(nullptr)
            , _audioConvert(nullptr)
            , _audioResample(nullptr)
            , _audioSink(nullptr)
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
            //   urisourcebin --[pad-added]--+--> h264parse --> videoQueue --> westerossink
            //                              \--> decodebin --[pad-added]--> audioQueue --> audioconvert --> audioresample --> autoaudiosink
            //
            // WHY urisourcebin instead of uridecodebin?
            //   urisourcebin exposes elementary streams (H.264, AAC, etc.) without
            //   decoding them.  westerossink performs hardware H.264 decode internally,
            //   so we feed it the compressed H.264 stream directly via h264parse.
            //   The audio branch uses a software decodebin for AAC → PCM conversion.
            //
            // WHY h264parse?
            //   Parses and bytestream-converts raw H.264 NAL units from urisourcebin
            //   into the format westerossink expects.
            //
            // WHY queues?
            //   - videoQueue: decouples h264parse streaming thread from westerossink render thread.
            //   - audioQueue: decouples decodebin streaming thread from autoaudiosink thread.
            //   Without queues, dynamic pad linking from the pad-added callback can
            //   race with sink threads, causing deadlocks or silent frame drops.
            //
            // WHY audioconvert / audioresample?
            //   decodebin outputs whatever PCM format the audio decoder produces.
            //   audioconvert and audioresample adapt the format/rate so autoaudiosink
            //   can accept the stream without a caps mismatch.
            // -----------------------------------------------------------------

            _pipeline      = gst_pipeline_new("gstreamer-player");
            _source        = gst_element_factory_make("urisourcebin",  "source");
            _decodebin     = gst_element_factory_make("decodebin",     "decodebin");
            _h264parse     = gst_element_factory_make("h264parse",     "h264parse");
            _videoQueue    = gst_element_factory_make("queue",         "videoqueue");
            _videoSink     = gst_element_factory_make("westerossink",  "videosink");
            _audioQueue    = gst_element_factory_make("queue",         "audioqueue");
            _audioConvert  = gst_element_factory_make("audioconvert",  "audioconvert");
            _audioResample = gst_element_factory_make("audioresample", "audioresample");
            _audioSink     = gst_element_factory_make("autoaudiosink", "audiosink");

            if (!_pipeline || !_source || !_decodebin || !_h264parse
                           || !_videoQueue || !_videoSink
                           || !_audioQueue || !_audioConvert || !_audioResample || !_audioSink) {
                LOGERR("GStreamerPlayer::Play: Failed to create one or more GStreamer elements");
                DestroyPipeline();
                return Core::ERROR_GENERAL;
            }

            // Add every element into the pipeline bin so it manages their lifetime.
            gst_bin_add_many(GST_BIN(_pipeline),
                             _source,
                             _decodebin,
                             _h264parse,
                             _videoQueue, _videoSink,
                             _audioQueue, _audioConvert, _audioResample, _audioSink,
                             nullptr);

            // Link the static chains.
            // Dynamic links (source→h264parse and source→decodebin) are made in OnPadAdded().
            // Dynamic link (decodebin→audioQueue) is made in OnDecodebinPadAdded().
            if (!gst_element_link_many(_h264parse, _videoQueue, _videoSink, nullptr)) {
                LOGERR("GStreamerPlayer::Play: Failed to link h264parse -> videoQueue -> westerossink");
                DestroyPipeline();
                return Core::ERROR_GENERAL;
            }
            LOGINFO("GStreamerPlayer::Play: h264parse -> videoQueue -> westerossink linked successfully");

            if (!gst_element_link_many(_audioQueue, _audioConvert, _audioResample, _audioSink, nullptr)) {
                LOGERR("GStreamerPlayer::Play: Failed to link audioQueue -> audioconvert -> audioresample -> autoaudiosink");
                DestroyPipeline();
                return Core::ERROR_GENERAL;
            }
            LOGINFO("GStreamerPlayer::Play: audioQueue -> audioconvert -> audioresample -> autoaudiosink linked successfully");

            // Tell urisourcebin which content to fetch.
            g_object_set(_source, "uri", uri.c_str(), nullptr);

                        // Tell urisourcebin which content to fetch, and enable its internal
            // parsebin so it demuxes the container and exposes elementary-stream
            // pads (video/x-h264, audio/mpeg, etc.) rather than the raw container
            // stream (video/webm, video/quicktime, etc.).
            //
            // Without parse-streams=TRUE, urisourcebin outputs a single pad carrying
            // the whole container bytestream (e.g. "video/webm").  OnPadAdded skips
            // unknown types, leaving that source pad unlinked.  GstSoupHTTPSrc then
            // fails with "Internal data stream error" because it has nowhere to push
            // its buffers.  parse-streams=TRUE routes data through a GstParsebin
            // child which absorbs all produced pads, so partial linking (e.g. only
            // audio when video is VP8) no longer stalls the source.
            //g_object_set(_source, "uri", uri.c_str(), "parse-streams", TRUE, nullptr);

            // When urisourcebin exposes a new elementary-stream pad, OnPadAdded()
            // routes video/x-h264 to h264parse and audio/* to decodebin.
            g_signal_connect(_source, "pad-added",
                             G_CALLBACK(GStreamerPlayerImplementation::OnPadAdded), this);

            // When decodebin finishes decoding an audio pad, OnDecodebinPadAdded()
            // links audio/x-raw to audioQueue.
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

            // render-rectangle expects a GstValueArray of four gint values: <x, y, width, height>.
            // Build the array value and set it via g_object_set_property().
            GValue rect = G_VALUE_INIT;
            g_value_init(&rect, GST_TYPE_ARRAY);

            const gint coords[4] = {
                static_cast<gint>(x),
                static_cast<gint>(y),
                static_cast<gint>(width),
                static_cast<gint>(height)
            };

            GValue item = G_VALUE_INIT;
            g_value_init(&item, G_TYPE_INT);
            for (int i = 0; i < 4; ++i) {
                g_value_set_int(&item, coords[i]);
                gst_value_array_append_value(&rect, &item);
            }
            g_value_unset(&item);

            g_object_set_property(G_OBJECT(_videoSink), "render-rectangle", &rect);
            g_value_unset(&rect);

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
        /**
         * Called by urisourcebin on its streaming thread whenever it exposes a new
         * elementary-stream pad (before decoding).
         *
         * Routing:
         *   video/x-h264  -->  h264parse  (westerossink performs hardware decode)
         *   audio/...     -->  decodebin  (software decode to audio/x-raw)
         */
        /* static */
        void GStreamerPlayerImplementation::OnPadAdded(
            GstElement* /* src */, GstPad* newPad, gpointer userData)
        {
            GStreamerPlayerImplementation* self =
                static_cast<GStreamerPlayerImplementation*>(userData);

            gchar* padName = gst_pad_get_name(newPad);
            LOGINFO("GStreamerPlayer::OnPadAdded: pad added: %s", padName);

            // Try linking to h264parse first (video path).
            GstPad* h264SinkPad = gst_element_get_static_pad(self->_h264parse, "sink");
            GstPadLinkReturn ret = gst_pad_link(newPad, h264SinkPad);

            if (ret == GST_PAD_LINK_OK) {
                LOGINFO("GStreamerPlayer::OnPadAdded: linked pad '%s' to h264parse", padName);
            } else {
                // Fall back to audio path via decodebin.
                GstPad* audioSinkPad = gst_element_get_static_pad(self->_decodebin, "sink");
                ret = gst_pad_link(newPad, audioSinkPad);
                if (ret == GST_PAD_LINK_OK) {
                    LOGINFO("GStreamerPlayer::OnPadAdded: linked pad '%s' to decodebin", padName);
                } else {
                    LOGERR("GStreamerPlayer::OnPadAdded: failed to link pad '%s'", padName);
                }
                gst_object_unref(audioSinkPad);
            }

            gst_object_unref(h264SinkPad);
            g_free(padName);
        }

        /**
         * Called by decodebin on its streaming thread whenever it finishes decoding
         * a pad and exposes raw audio.
         *
         * Links audio/x-raw to audioQueue, which decouples decodebin’s streaming
         * thread from the autoaudiosink thread.
         */
        /* static */
        void GStreamerPlayerImplementation::OnDecodebinPadAdded(
            GstElement* /* src */, GstPad* newPad, gpointer userData)
        {
            GStreamerPlayerImplementation* self =
                static_cast<GStreamerPlayerImplementation*>(userData);

            GstCaps* caps = gst_pad_get_current_caps(newPad);
            if (!caps) {
                caps = gst_pad_query_caps(newPad, nullptr);
            }
            if (!caps) {
                LOGERR("GStreamerPlayer::OnDecodebinPadAdded: could not determine caps");
                return;
            }

            GstStructure* structure = gst_caps_get_structure(caps, 0);
            const gchar*  mediaType = gst_structure_get_name(structure);

            if (g_str_has_prefix(mediaType, "audio/x-raw")) {
                LOGINFO("GStreamerPlayer::OnDecodebinPadAdded: linking audio/x-raw to audioQueue");
                GstPad* sinkPad = gst_element_get_static_pad(self->_audioQueue, "sink");
                if (sinkPad && !gst_pad_is_linked(sinkPad)) {
                    GstPadLinkReturn linkRet = gst_pad_link(newPad, sinkPad);
                    if (linkRet != GST_PAD_LINK_OK) {
                        LOGERR("GStreamerPlayer::OnDecodebinPadAdded: audio pad link failed (ret=%d)",
                               static_cast<int>(linkRet));
                    }
                }
                if (sinkPad) gst_object_unref(sinkPad);
            } else {
                LOGINFO("GStreamerPlayer::OnDecodebinPadAdded: skipping pad with type '%s'", mediaType);
            }

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
                _source        = nullptr;
                _decodebin     = nullptr;
                _h264parse     = nullptr;
                _videoQueue    = nullptr;
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
