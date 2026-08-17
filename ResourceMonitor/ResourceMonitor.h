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

#pragma once

#include "Module.h"
#include <interfaces/IResourceMonitor.h>
#include "UtilsLogging.h"
#include "tracing/Logging.h"

namespace WPEFramework {
namespace Plugin {

    class ResourceMonitor : public PluginHost::IPlugin {
    private:
        // --------------------------------------------------------------------
        // Inner Notification class
        //
        // Implements two interfaces:
        //   1. IResourceMonitor::IProcessKilledNotification
        //      → receives COM events from ResourceMonitorImplementation
        //      → translates them into JSON-RPC events (JResourceMonitor::Event::*)
        //
        //   2. RPC::IRemoteConnection::INotification
        //      → watches the out-of-process connection; if the impl crashes,
        //        Deactivated() fires and we schedule a plugin deactivation
        // --------------------------------------------------------------------
        class Notification : public Exchange::IResourceMonitor::IProcessKilledNotification,
                             public RPC::IRemoteConnection::INotification {
        private:
            Notification() = delete;
            Notification(const Notification&) = delete;
            Notification& operator=(const Notification&) = delete;

        public:
            explicit Notification(ResourceMonitor* parent)
                : _parent(*parent)
            {
                ASSERT(parent != nullptr);
            }
            ~Notification() override = default;

            // IProcessKilledNotification — log and relay to any outer-shell logic
            void OnProcessKilled(const string& processName, const int pid, const int exitCode) override
            {
                LOGINFO("[EVENT] processName[%s] pid[%d] exitCode[%d]",
                        processName.c_str(), pid, exitCode);
            }

            // -----------------------------------------------------------------
            // RPC::IRemoteConnection::INotification — out-of-process lifecycle
            // -----------------------------------------------------------------
            void Activated(RPC::IRemoteConnection* connection) override
            {
                if (_parent._connectionId == connection->Id()) {
                    LOGINFO("ResourceMonitor out-of-process impl Activated");
                }
            }

            void Deactivated(RPC::IRemoteConnection* connection) override
            {
                if (_parent._connectionId == connection->Id()) {
                    LOGINFO("ResourceMonitor out-of-process impl Deactivated");
                    _parent.Deactivated(connection);
                }
            }

            BEGIN_INTERFACE_MAP(Notification)
            INTERFACE_ENTRY(Exchange::IResourceMonitor::IProcessKilledNotification)
            INTERFACE_ENTRY(RPC::IRemoteConnection::INotification)
            END_INTERFACE_MAP

        private:
            ResourceMonitor& _parent;
        };

    public:
        ResourceMonitor(const ResourceMonitor&) = delete;
        ResourceMonitor& operator=(const ResourceMonitor&) = delete;

        ResourceMonitor();
        ~ResourceMonitor() override;

        // COM-RPC only: no IDispatcher — native callers reach IResourceMonitor via INTERFACE_AGGREGATE
        BEGIN_INTERFACE_MAP(ResourceMonitor)
        INTERFACE_ENTRY(PluginHost::IPlugin)
        INTERFACE_AGGREGATE(Exchange::IResourceMonitor, _resourceMonitor)
        END_INTERFACE_MAP

        // IPlugin methods
        const string Initialize(PluginHost::IShell* service) override;
        void Deinitialize(PluginHost::IShell* service) override;
        string Information() const override;

    private:
        void Deactivated(RPC::IRemoteConnection* connection);

    private:
        PluginHost::IShell*           _service{};
        uint32_t                      _connectionId{};
        Exchange::IResourceMonitor*   _resourceMonitor{};  // proxy to out-of-process impl
        Core::Sink<Notification>      _notification;        // subscriber + remote-connection watcher
        bool                          _notificationRegistered{false};

        friend class Notification;
    };

} // namespace Plugin
} // namespace WPEFramework
