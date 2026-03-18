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
 * @file GStreamerPlayer.h
 * @brief Thunder Plugin based Implementation for GStreamer Player service API's.
 */

/**
  @mainpage GStreamerPlayer

  <b>GStreamerPlayer</b> Thunder Service provides APIs for media playback
  * using GStreamer pipeline with play, pause, stop controls.
  */

#pragma once

#include "Module.h"
#include <interfaces/IGStreamerPlayer.h>
#include <interfaces/json/JsonData_GStreamerPlayer.h>
#include <interfaces/json/JGStreamerPlayer.h>
#include <interfaces/IConfiguration.h>
#include "UtilsLogging.h"
#include "tracing/Logging.h"

namespace WPEFramework {

    namespace Plugin {
			
		class GStreamerPlayer : public PluginHost::IPlugin, public PluginHost::JSONRPC
		{
			private:
            	class Notification : public RPC::IRemoteConnection::INotification, public Exchange::IGStreamerPlayer::INotification
                {
					private:
			        	Notification() = delete;
			            Notification(const Notification&) = delete;
			            Notification& operator=(const Notification&) = delete;
						
					public:
						explicit Notification(GStreamerPlayer *parent)
							: _parent(*parent)
						{
							ASSERT(parent != nullptr);
						}
		
						virtual ~Notification()
						{
						}
					
						BEGIN_INTERFACE_MAP(Notification)
						INTERFACE_ENTRY(Exchange::IGStreamerPlayer::INotification)
						INTERFACE_ENTRY(RPC::IRemoteConnection::INotification)
						END_INTERFACE_MAP

						virtual void OnPipelineStateChanged(const Exchange::IGStreamerPlayer::PipelineState& state, const Exchange::IGStreamerPlayer::ErrorCode& error) override
						{
							LOGINFO("[EVENT] Pipeline State Changed: state=%d, error=%d", state, error);
							Exchange::JGStreamerPlayer::Event::OnPipelineStateChanged(_parent, state, error);
						}
							
						virtual void Activated(RPC::IRemoteConnection *connection) final
						{
							if(_parent._connectionId == connection->Id())
							{
								LOGINFO("GStreamerPlayer Notification Activated");
							}
						}
		
						virtual void Deactivated(RPC::IRemoteConnection *connection) final
						{
							if(_parent._connectionId == connection->Id())
							{
								LOGINFO("GStreamerPlayer Notification Deactivated");
								_parent.Deactivated(connection);
							}
						}

						private:
							GStreamerPlayer &_parent;
				};
			
			public:
				GStreamerPlayer(const GStreamerPlayer &) = delete;
				GStreamerPlayer &operator=(const GStreamerPlayer &) = delete;
				
				GStreamerPlayer();
				virtual ~GStreamerPlayer();
			
				BEGIN_INTERFACE_MAP(GStreamerPlayer)
				INTERFACE_ENTRY(PluginHost::IPlugin)
				INTERFACE_ENTRY(PluginHost::IDispatcher)
				INTERFACE_AGGREGATE(Exchange::IGStreamerPlayer, _gstreamerPlayer)
				END_INTERFACE_MAP
				
				//  IPlugin methods
				// -------------------------------------------------------------------------------------------------------
				const string Initialize(PluginHost::IShell* service) override;
				void Deinitialize(PluginHost::IShell* service) override;
				string Information() const override;
				
				private:
                	void Deactivated(RPC::IRemoteConnection* connection);
			
				private:
					PluginHost::IShell *_service{};
					uint32_t _connectionId{};
					Exchange::IGStreamerPlayer *_gstreamerPlayer{};
					Exchange::IConfiguration *_configuration{};
					Core::Sink<Notification> _gstreamerPlayerNotification;
		};
	} // namespace Plugin
} // namespace WPEFramework
