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

namespace WPEFramework
{
    namespace Plugin
    {
        bool GStreamerPlayerImplementation::_gstreamerInitialized = false;

        // Plugin internal class implementation
        SERVICE_REGISTRATION(GStreamerPlayerImplementation, 1, 0, 0);

        GStreamerPlayerImplementation::GStreamerPlayerImplementation()
            : _service(nullptr)
            , _pipeline(nullptr)
            , _bus(nullptr)
            , _currentUrl("")
            , _currentState(Exchange::IGStreamerPlayer::PipelineState::STOPPED)
        {
            LOGINFO("GStreamerPlayerImplementation Constructor");
        }

        GStreamerPlayerImplementation::~GStreamerPlayerImplementation()
        {
            LOGINFO("GStreamerPlayerImplementation Destructor");
            cleanupGStreamer();
        }

        bool GStreamerPlayerImplementation::initializeGStreamer()
        {
            LOGINFO("Initializing GStreamer");
            
            if (!_gstreamerInitialized)
            {
                GError* error = nullptr;
                if (!gst_init_check(nullptr, nullptr, &error))
                {
                    LOGERR("Failed to initialize GStreamer: %s", error ? error->message : "Unknown error");
                    if (error)
                        g_error_free(error);
                    return false;
                }
                _gstreamerInitialized = true;
                LOGINFO("GStreamer initialized successfully");
            }

            // Create playbin pipeline
            _pipeline = gst_element_factory_make("playbin", "player");
            if (!_pipeline)
            {
                LOGERR("Failed to create playbin pipeline");
                return false;
            }

            // Get the bus
            _bus = gst_element_get_bus(_pipeline);
            if (_bus)
            {
                gst_bus_set_sync_handler(_bus, bus_sync_handler, this, nullptr);
            }

            LOGINFO("GStreamer pipeline created successfully");
            return true;
        }

        void GStreamerPlayerImplementation::cleanupGStreamer()
        {
            LOGINFO("Cleaning up GStreamer");

            if (_pipeline)
            {
                gst_element_set_state(_pipeline, GST_STATE_NULL);
                gst_object_unref(_pipeline);
                _pipeline = nullptr;
            }

            if (_bus)
            {
                gst_object_unref(_bus);
                _bus = nullptr;
            }

            _currentUrl = "";
            _currentState = Exchange::IGStreamerPlayer::PipelineState::STOPPED;
        }

        GstBusSyncReply GStreamerPlayerImplementation::bus_sync_handler(GstBus* bus, GstMessage* msg, gpointer user_data)
        {
            GStreamerPlayerImplementation* impl = static_cast<GStreamerPlayerImplementation*>(user_data);
            if (impl)
            {
                impl->handleBusMessage(msg);
            }
            return GST_BUS_PASS;
        }

        void GStreamerPlayerImplementation::handleBusMessage(GstMessage* msg)
        {
            switch (GST_MESSAGE_TYPE(msg))
            {
                case GST_MESSAGE_STATE_CHANGED:
                {
                    if (GST_MESSAGE_SRC(msg) == GST_OBJECT(_pipeline))
                    {
                        GstState oldState, newState, pendingState;
                        gst_message_parse_state_changed(msg, &oldState, &newState, &pendingState);
                        
                        LOGINFO("Pipeline state changed: %s -> %s",
                                gst_element_state_get_name(oldState),
                                gst_element_state_get_name(newState));
                        
                        Exchange::IGStreamerPlayer::PipelineState pipelineState = gstStateToPipelineState(newState);
                        if (pipelineState != _currentState)
                        {
                            _currentState = pipelineState;
                            notifyStateChange(pipelineState);
                        }
                    }
                    break;
                }
                case GST_MESSAGE_ERROR:
                {
                    GError* error = nullptr;
                    gchar* debug = nullptr;
                    gst_message_parse_error(msg, &error, &debug);
                    
                    LOGERR("GStreamer Error: %s (Debug: %s)", 
                           error ? error->message : "Unknown", 
                           debug ? debug : "None");
                    
                    _currentState = Exchange::IGStreamerPlayer::PipelineState::ERROR;
                    notifyStateChange(Exchange::IGStreamerPlayer::PipelineState::ERROR, 
                                    Exchange::IGStreamerPlayer::ErrorCode::PLAYBACK_ERROR);
                    
                    if (error)
                        g_error_free(error);
                    if (debug)
                        g_free(debug);
                    break;
                }
                case GST_MESSAGE_EOS:
                {
                    LOGINFO("End of stream reached");
                    _currentState = Exchange::IGStreamerPlayer::PipelineState::STOPPED;
                    notifyStateChange(Exchange::IGStreamerPlayer::PipelineState::STOPPED);
                    break;
                }
                default:
                    break;
            }
        }

        Exchange::IGStreamerPlayer::PipelineState GStreamerPlayerImplementation::gstStateToPipelineState(GstState gstState)
        {
            switch (gstState)
            {
                case GST_STATE_NULL:
                case GST_STATE_READY:
                    return Exchange::IGStreamerPlayer::PipelineState::STOPPED;
                case GST_STATE_PAUSED:
                    return Exchange::IGStreamerPlayer::PipelineState::PAUSED;
                case GST_STATE_PLAYING:
                    return Exchange::IGStreamerPlayer::PipelineState::PLAYING;
                default:
                    return Exchange::IGStreamerPlayer::PipelineState::ERROR;
            }
        }

        void GStreamerPlayerImplementation::notifyStateChange(Exchange::IGStreamerPlayer::PipelineState newState, 
                                                              Exchange::IGStreamerPlayer::ErrorCode error)
        {
            LOGINFO("Notifying state change: state=%d, error=%d", newState, error);
            
            _adminLock.Lock();
            std::list<Exchange::IGStreamerPlayer::INotification*> notifications = _notifications;
            _adminLock.Unlock();

            for (auto* notification : notifications)
            {
                notification->OnPipelineStateChanged(newState, error);
            }
        }

        uint32_t GStreamerPlayerImplementation::Configure(PluginHost::IShell* service)
        {
            LOGINFO("GStreamerPlayerImplementation::Configure");
            
            if (service == nullptr)
            {
                // Deinitialize
                LOGINFO("Deinitializing GStreamerPlayer");
                cleanupGStreamer();
                _service = nullptr;
                return Core::ERROR_NONE;
            }

            _service = service;
            
            // Initialize GStreamer
            if (!initializeGStreamer())
            {
                LOGERR("Failed to initialize GStreamer");
                return Core::ERROR_GENERAL;
            }

            return Core::ERROR_NONE;
        }

        Core::hresult GStreamerPlayerImplementation::Register(Exchange::IGStreamerPlayer::INotification* sink)
        {
            ASSERT(sink != nullptr);
            
            _adminLock.Lock();
            
            // Check if already registered
            auto it = std::find(_notifications.begin(), _notifications.end(), sink);
            if (it == _notifications.end())
            {
                _notifications.push_back(sink);
                sink->AddRef();
                LOGINFO("Notification registered successfully");
            }
            
            _adminLock.Unlock();
            
            return Core::ERROR_NONE;
        }

        Core::hresult GStreamerPlayerImplementation::Unregister(Exchange::IGStreamerPlayer::INotification* sink)
        {
            ASSERT(sink != nullptr);
            
            _adminLock.Lock();
            
            auto it = std::find(_notifications.begin(), _notifications.end(), sink);
            if (it != _notifications.end())
            {
                (*it)->Release();
                _notifications.erase(it);
                LOGINFO("Notification unregistered successfully");
            }
            
            _adminLock.Unlock();
            
            return Core::ERROR_NONE;
        }

        Core::hresult GStreamerPlayerImplementation::SetURL(const string& url, Exchange::IGStreamerPlayer::PlayerSuccess& success)
        {
            LOGINFO("SetURL: %s", url.c_str());

            if (url.empty())
            {
                LOGERR("Invalid URL provided");
                success.success = false;
                return Core::ERROR_NONE;
            }

            if (!_pipeline)
            {
                LOGERR("Pipeline not initialized");
                success.success = false;
                return Core::ERROR_NONE;
            }

            // Stop current playback if any
            gst_element_set_state(_pipeline, GST_STATE_NULL);

            // Set the URI
            g_object_set(G_OBJECT(_pipeline), "uri", url.c_str(), nullptr);
            _currentUrl = url;
            
            LOGINFO("URL set successfully: %s", url.c_str());
            success.success = true;
            return Core::ERROR_NONE;
        }

        Core::hresult GStreamerPlayerImplementation::Play(Exchange::IGStreamerPlayer::PlayerSuccess& success)
        {
            LOGINFO("Play");

            if (!_pipeline)
            {
                LOGERR("Pipeline not initialized");
                success.success = false;
                return Core::ERROR_NONE;
            }

            if (_currentUrl.empty())
            {
                LOGERR("No URL set");
                success.success = false;
                return Core::ERROR_NONE;
            }

            GstStateChangeReturn ret = gst_element_set_state(_pipeline, GST_STATE_PLAYING);
            if (ret == GST_STATE_CHANGE_FAILURE)
            {
                LOGERR("Failed to set pipeline to PLAYING state");
                success.success = false;
                notifyStateChange(Exchange::IGStreamerPlayer::PipelineState::ERROR, 
                                Exchange::IGStreamerPlayer::ErrorCode::PLAYBACK_ERROR);
                return Core::ERROR_NONE;
            }

            LOGINFO("Playback started successfully");
            success.success = true;
            return Core::ERROR_NONE;
        }

        Core::hresult GStreamerPlayerImplementation::Pause(Exchange::IGStreamerPlayer::PlayerSuccess& success)
        {
            LOGINFO("Pause");

            if (!_pipeline)
            {
                LOGERR("Pipeline not initialized");
                success.success = false;
                return Core::ERROR_NONE;
            }

            GstStateChangeReturn ret = gst_element_set_state(_pipeline, GST_STATE_PAUSED);
            if (ret == GST_STATE_CHANGE_FAILURE)
            {
                LOGERR("Failed to set pipeline to PAUSED state");
                success.success = false;
                return Core::ERROR_NONE;
            }

            LOGINFO("Playback paused successfully");
            success.success = true;
            return Core::ERROR_NONE;
        }

        Core::hresult GStreamerPlayerImplementation::Stop(Exchange::IGStreamerPlayer::PlayerSuccess& success)
        {
            LOGINFO("Stop");

            if (!_pipeline)
            {
                LOGERR("Pipeline not initialized");
                success.success = false;
                return Core::ERROR_NONE;
            }

            GstStateChangeReturn ret = gst_element_set_state(_pipeline, GST_STATE_NULL);
            if (ret == GST_STATE_CHANGE_FAILURE)
            {
                LOGERR("Failed to set pipeline to NULL state");
                success.success = false;
                return Core::ERROR_NONE;
            }

            _currentState = Exchange::IGStreamerPlayer::PipelineState::STOPPED;
            LOGINFO("Playback stopped successfully");
            success.success = true;
            return Core::ERROR_NONE;
        }

        Core::hresult GStreamerPlayerImplementation::GetState(Exchange::IGStreamerPlayer::PipelineState& state, bool& success)
        {
            LOGINFO("GetState");

            if (!_pipeline)
            {
                LOGERR("Pipeline not initialized");
                state = Exchange::IGStreamerPlayer::PipelineState::STOPPED;
                success = false;
                return Core::ERROR_NONE;
            }

            GstState gstState;
            GstStateChangeReturn ret = gst_element_get_state(_pipeline, &gstState, nullptr, GST_CLOCK_TIME_NONE);
            
            if (ret == GST_STATE_CHANGE_FAILURE)
            {
                LOGERR("Failed to get pipeline state");
                state = Exchange::IGStreamerPlayer::PipelineState::ERROR;
                success = false;
                return Core::ERROR_NONE;
            }

            state = gstStateToPipelineState(gstState);
            success = true;
            LOGINFO("Current state: %d", state);
            return Core::ERROR_NONE;
        }

    } // namespace Plugin
} // namespace WPEFramework
