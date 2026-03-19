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
 * @file HelloPlugin.h
 * @brief Thunder Out-of-Process Plugin: HelloPlugin
 *
 * A minimal out-of-process plugin that outputs a greeting message.
 */

#pragma once

#include "Module.h"
#include <interfaces/IHelloPlugin.h>
#include <interfaces/json/JsonData_HelloPlugin.h>
#include <interfaces/json/JHelloPlugin.h>
#include "UtilsLogging.h"
#include "tracing/Logging.h"

namespace WPEFramework {
    namespace Plugin {

        class HelloPlugin : public PluginHost::IPlugin, public PluginHost::JSONRPC {
        private:
            class Notification : public RPC::IRemoteConnection::INotification,
                                  public Exchange::IHelloPlugin::INotification {
            private:
                Notification() = delete;
                Notification(const Notification&) = delete;
                Notification& operator=(const Notification&) = delete;

            public:
                explicit Notification(HelloPlugin* parent)
                    : _parent(*parent)
                {
                    ASSERT(parent != nullptr);
                }

                ~Notification() override = default;

                BEGIN_INTERFACE_MAP(Notification)
                INTERFACE_ENTRY(Exchange::IHelloPlugin::INotification)
                INTERFACE_ENTRY(RPC::IRemoteConnection::INotification)
                END_INTERFACE_MAP

                // IHelloPlugin::INotification
                void OnGreetingChanged(const string& message) override
                {
                    LOGINFO("[EVENT] OnGreetingChanged: %s", message.c_str());
                    Exchange::JHelloPlugin::Event::OnGreetingChanged(_parent, message);
                }

                // RPC::IRemoteConnection::INotification
                void Activated(RPC::IRemoteConnection* /* connection */) override
                {
                }

                void Deactivated(RPC::IRemoteConnection* connection) override
                {
                    if (_parent._connectionId == connection->Id()) {
                        _parent.Deactivated(connection);
                    }
                }

            private:
                HelloPlugin& _parent;
            };

        public:
            HelloPlugin(const HelloPlugin&) = delete;
            HelloPlugin& operator=(const HelloPlugin&) = delete;

            HelloPlugin();
            ~HelloPlugin() override;

            BEGIN_INTERFACE_MAP(HelloPlugin)
            INTERFACE_ENTRY(PluginHost::IPlugin)
            INTERFACE_ENTRY(PluginHost::IDispatcher)
            INTERFACE_AGGREGATE(Exchange::IHelloPlugin, _helloPlugin)
            END_INTERFACE_MAP

            // IPlugin methods
            const string Initialize(PluginHost::IShell* service) override;
            void Deinitialize(PluginHost::IShell* service) override;
            string Information() const override;

        private:
            void Deactivated(RPC::IRemoteConnection* connection);

        private:
            PluginHost::IShell* _service{};
            uint32_t _connectionId{};
            Exchange::IHelloPlugin* _helloPlugin{};
            Core::Sink<Notification> _helloPluginNotification;
        };

    } // namespace Plugin
} // namespace WPEFramework
