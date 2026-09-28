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

#include "PlaybinPluginImplementation.h"
#include <algorithm>

namespace WPEFramework {
    namespace Plugin {

        // Register this class so Thunder's COM-RPC layer can create it when
        // the proxy (PlaybinPlugin.cpp) calls _service->Root<>().
        SERVICE_REGISTRATION(PlaybinPluginImplementation, 1, 0);

        // =====================================================================
        // Constructor / Destructor
        // =====================================================================

        PlaybinPluginImplementation::PlaybinPluginImplementation()
            : _adminLock()
            , _notificationClients()
            , _configuredUri()
            , _hasConfiguredMedia(false)
            , _pipeline(nullptr)
            , _playerInitializedFired(false)
            , _mainLoop(nullptr)
            , _mainLoopThread()
            , _busWatchId(0)
        {
            // Initialise GStreamer once for this process.
            gst_init(nullptr, nullptr);
            SYSLOG(Logging::Startup, (_T("PlaybinPluginImplementation: GStreamer initialised")));
        }

        PlaybinPluginImplementation::~PlaybinPluginImplementation()
        {
            // Make sure the pipeline is torn down cleanly before we die.
            if (_pipeline != nullptr) {
                DestroyPipeline();
            }
            SYSLOG(Logging::Shutdown, (_T("PlaybinPluginImplementation Destructor")));
        }

        // =====================================================================
        // Register / Unregister notification clients
        // =====================================================================

        Core::hresult PlaybinPluginImplementation::Register(IPlaybinPlugin::INotification* sink)
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

        Core::hresult PlaybinPluginImplementation::Unregister(IPlaybinPlugin::INotification* sink)
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
        // URI normalization helper
        // =====================================================================

        bool PlaybinPluginImplementation::NormalizeToUri(const string& media, string& uriOut)
        {
            if (media.empty()) {
                return false;
            }

            if (media.find("://") != string::npos) {
                // Already a URI (file://, http://, https://, etc.) - use as-is.
                uriOut = media;
                return true;
            }

            // Bare local file path: convert to a file:// URI.
            gchar* uri = gst_filename_to_uri(media.c_str(), nullptr);
            if (uri == nullptr) {
                return false;
            }

            uriOut = uri;
            g_free(uri);
            return true;
        }

        // =====================================================================
        // Configure
        // =====================================================================

        Core::hresult PlaybinPluginImplementation::Configure(const string& media)
        {
            string uri;
            if (!NormalizeToUri(media, uri)) {
                LOGERR("PlaybinPlugin::Configure: media location is empty or could not be resolved to a URI");
                return Core::ERROR_GENERAL;
            }

            if (_pipeline != nullptr) {
                // A previous Play() left the pipeline PLAYING/PAUSED. Stop and
                // release it before applying the new configuration.
                LOGINFO("PlaybinPlugin::Configure: releasing existing pipeline before reconfiguring");
                DestroyPipeline();
            }

            _configuredUri      = uri;
            _hasConfiguredMedia = true;

            LOGINFO("PlaybinPlugin::Configure: configured media uri=%s", uri.c_str());
            return Core::ERROR_NONE;
        }

        // =====================================================================
        // Play
        // =====================================================================

        Core::hresult PlaybinPluginImplementation::Play()
        {
            if (!_hasConfiguredMedia) {
                LOGERR("PlaybinPlugin::Play: No media has been configured");
                return Core::ERROR_ILLEGAL_STATE;
            }

            if (_pipeline == nullptr) {
                // First Play() since Configure()/Stop(): build the playbin pipeline.
                // playbin owns and manages its own internal source/demux/decode/sink
                // chain - it is not constructed element-by-element here.
                _pipeline = gst_element_factory_make("playbin", "playbin-pipeline");
                if (_pipeline == nullptr) {
                    LOGERR("PlaybinPlugin::Play: Failed to create playbin element");
                    return Core::ERROR_GENERAL;
                }

                g_object_set(_pipeline, "uri", _configuredUri.c_str(), nullptr);
                LOGINFO("PlaybinPlugin::Play: created playbin pipeline uri=%s", _configuredUri.c_str());

                // Attach a bus watch so that GStreamer messages (ASYNC_DONE, ERROR, EOS)
                // are dispatched on the GMainLoop thread to OnBusMessage(). This is the
                // correct place to fire OnPlayerInitialized - only after the pipeline
                // actually reaches PLAYING (ASYNC_DONE), not immediately after
                // gst_element_set_state() which returns ASYNC for network URIs.
                GstBus* bus = gst_pipeline_get_bus(GST_PIPELINE(_pipeline));
                _busWatchId = gst_bus_add_watch(bus, PlaybinPluginImplementation::OnBusMessage, this);
                gst_object_unref(bus);

                // Start a GMainLoop in a background thread so GStreamer can dispatch
                // bus messages asynchronously.
                _playerInitializedFired = false;
                _mainLoop = g_main_loop_new(nullptr, FALSE);
                _mainLoopThread = std::thread([this]() {
                    g_main_loop_run(_mainLoop);
                });
            }

            // Handles both "already PLAYING" (no-op success) and "PAUSED -> PLAYING"
            // (resume without recreating the pipeline) via the same idempotent call.
            GstStateChangeReturn ret = gst_element_set_state(_pipeline, GST_STATE_PLAYING);
            if (ret == GST_STATE_CHANGE_FAILURE) {
                LOGERR("PlaybinPlugin::Play: Pipeline failed to transition to PLAYING");
                DestroyPipeline();
                return Core::ERROR_GENERAL;
            }

            LOGINFO("PlaybinPlugin::Play: pipeline set to PLAYING");
            return Core::ERROR_NONE;
        }

        // =====================================================================
        // Pause
        // =====================================================================

        Core::hresult PlaybinPluginImplementation::Pause()
        {
            if (_pipeline == nullptr) {
                LOGERR("PlaybinPlugin::Pause: No pipeline is running");
                return Core::ERROR_ILLEGAL_STATE;
            }

            LOGINFO("PlaybinPlugin::Pause");
            gst_element_set_state(_pipeline, GST_STATE_PAUSED);
            return Core::ERROR_NONE;
        }

        // =====================================================================
        // Stop
        // =====================================================================

        Core::hresult PlaybinPluginImplementation::Stop()
        {
            if (_pipeline == nullptr) {
                LOGERR("PlaybinPlugin::Stop: No pipeline is running");
                return Core::ERROR_ILLEGAL_STATE;
            }

            LOGINFO("PlaybinPlugin::Stop: stopping pipeline");
            DestroyPipeline();
            FirePlayerStopped();
            return Core::ERROR_NONE;
        }

        // =====================================================================
        // Pipeline teardown (does not clear _configuredUri / _hasConfiguredMedia)
        // =====================================================================

        void PlaybinPluginImplementation::DestroyPipeline()
        {
            if (_mainLoop != nullptr) {
                g_main_loop_quit(_mainLoop);
            }
            if (_mainLoopThread.joinable()) {
                _mainLoopThread.join();
            }
            if (_busWatchId != 0) {
                g_source_remove(_busWatchId);
                _busWatchId = 0;
            }
            if (_mainLoop != nullptr) {
                g_main_loop_unref(_mainLoop);
                _mainLoop = nullptr;
            }
            if (_pipeline != nullptr) {
                gst_element_set_state(_pipeline, GST_STATE_NULL);
                gst_object_unref(_pipeline);
                _pipeline = nullptr;
            }
            _playerInitializedFired = false;
        }

        // =====================================================================
        // Bus message handling
        // =====================================================================

        gboolean PlaybinPluginImplementation::OnBusMessage(GstBus* /* bus */, GstMessage* message, gpointer userData)
        {
            auto* self = static_cast<PlaybinPluginImplementation*>(userData);

            switch (GST_MESSAGE_TYPE(message)) {
            case GST_MESSAGE_ASYNC_DONE:
                // Pipeline has fully transitioned to PLAYING and decoded pads have
                // been linked internally by playbin.
                if (!self->_playerInitializedFired) {
                    self->_playerInitializedFired = true;
                    self->FirePlayerInitialized();
                }
                break;

            case GST_MESSAGE_ERROR: {
                GError* error     = nullptr;
                gchar*  debugInfo = nullptr;
                gst_message_parse_error(message, &error, &debugInfo);

                std::string errorMessage = (error != nullptr && error->message != nullptr)
                    ? error->message
                    : "Unknown GStreamer error";
                LOGERR("PlaybinPlugin::OnBusMessage: GST_MESSAGE_ERROR: %s", errorMessage.c_str());
                self->FirePlayerError(errorMessage);

                if (error != nullptr) {
                    g_error_free(error);
                }
                if (debugInfo != nullptr) {
                    g_free(debugInfo);
                }
                break;
            }

            case GST_MESSAGE_EOS:
                LOGINFO("PlaybinPlugin::OnBusMessage: GST_MESSAGE_EOS");
                break;

            default:
                break;
            }

            return TRUE; // keep watching the bus
        }

        // =====================================================================
        // Notification helpers
        // =====================================================================

        void PlaybinPluginImplementation::FirePlayerInitialized()
        {
            _adminLock.Lock();
            std::list<Exchange::IPlaybinPlugin::INotification*> clients = _notificationClients;
            _adminLock.Unlock();

            for (auto* client : clients) {
                client->OnPlayerInitialized();
            }
        }

        void PlaybinPluginImplementation::FirePlayerStopped()
        {
            _adminLock.Lock();
            std::list<Exchange::IPlaybinPlugin::INotification*> clients = _notificationClients;
            _adminLock.Unlock();

            for (auto* client : clients) {
                client->OnPlayerStopped();
            }
        }

        void PlaybinPluginImplementation::FirePlayerError(const string& message)
        {
            _adminLock.Lock();
            std::list<Exchange::IPlaybinPlugin::INotification*> clients = _notificationClients;
            _adminLock.Unlock();

            for (auto* client : clients) {
                client->OnPlayerError(message);
            }
        }

    } // namespace Plugin
} // namespace WPEFramework
