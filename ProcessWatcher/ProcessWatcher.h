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
#include <interfaces/IProcessWatcher.h>
#include <interfaces/IResourceMonitor.h>
#include <interfaces/JProcessWatcher.h>
#include "UtilsLogging.h"
#include "tracing/Logging.h"

namespace WPEFramework {
namespace Plugin {

    // -------------------------------------------------------------------------
    // ProcessWatcher plugin shell
    //
    // Transport: JSON-RPC (PluginHost::JSONRPC) for external callers.
    // COM-RPC: INTERFACE_AGGREGATE exposes IProcessWatcher to other plugins.
    // Mode: in-process (root.mode = "Off") — no WPEProcess spawned for impl.
    //
    // killprocess JSON-RPC method is overridden to delegate to ResourceMonitor
    // via QueryInterfaceByCallsign, so the actual SIGKILL is sent by
    // ResourceMonitorImplementation and observed here via ResourceMonitorSink.
    // -------------------------------------------------------------------------
    class ProcessWatcher : public PluginHost::IPlugin, public PluginHost::JSONRPC {
    private:
        // Watches the OOP connection for crashes; harmless in in-process mode.
        class Notification : public RPC::IRemoteConnection::INotification {
        private:
            Notification() = delete;
            Notification(const Notification&) = delete;
            Notification& operator=(const Notification&) = delete;

        public:
            explicit Notification(ProcessWatcher* parent)
                : _parent(*parent)
            {
                ASSERT(parent != nullptr);
            }
            ~Notification() override = default;

            void Activated(RPC::IRemoteConnection* connection) override
            {
                if (_parent._connectionId == connection->Id()) {
                    LOGINFO("ProcessWatcher out-of-process impl Activated");
                }
            }

            void Deactivated(RPC::IRemoteConnection* connection) override
            {
                if (_parent._connectionId == connection->Id()) {
                    LOGINFO("ProcessWatcher out-of-process impl Deactivated");
                    _parent.Deactivated(connection);
                }
            }

            BEGIN_INTERFACE_MAP(Notification)
            INTERFACE_ENTRY(RPC::IRemoteConnection::INotification)
            END_INTERFACE_MAP

        private:
            ProcessWatcher& _parent;
        };

        // Receives ResourceMonitor kill events; logs them inside ProcessWatcher.
        class ResourceMonitorSink : public Exchange::IResourceMonitor::IProcessKilledNotification {
        private:
            ResourceMonitorSink() = delete;
            ResourceMonitorSink(const ResourceMonitorSink&) = delete;
            ResourceMonitorSink& operator=(const ResourceMonitorSink&) = delete;

        public:
            explicit ResourceMonitorSink(ProcessWatcher* parent)
            {
                ASSERT(parent != nullptr);
            }
            ~ResourceMonitorSink() override = default;

            void OnProcessKilled(const string& processName, const int pid, const int exitCode) override
            {
                LOGINFO("[ProcessWatcher] ResourceMonitor killed process: name[%s] pid[%d] exitCode[%d]",
                        processName.c_str(), pid, exitCode);
            }

            BEGIN_INTERFACE_MAP(ResourceMonitorSink)
            INTERFACE_ENTRY(Exchange::IResourceMonitor::IProcessKilledNotification)
            END_INTERFACE_MAP
        };

    public:
        ProcessWatcher(const ProcessWatcher&) = delete;
        ProcessWatcher& operator=(const ProcessWatcher&) = delete;

        ProcessWatcher();
        ~ProcessWatcher() override;

        // Expose both IDispatcher (JSON-RPC) and IProcessWatcher (COM-RPC)
        BEGIN_INTERFACE_MAP(ProcessWatcher)
        INTERFACE_ENTRY(PluginHost::IPlugin)
        INTERFACE_ENTRY(PluginHost::IDispatcher)
        INTERFACE_AGGREGATE(Exchange::IProcessWatcher, _processWatcher)
        END_INTERFACE_MAP

        // IPlugin
        const string Initialize(PluginHost::IShell* service) override;
        void         Deinitialize(PluginHost::IShell* service) override;
        string       Information() const override;

    private:
        void Deactivated(RPC::IRemoteConnection* connection);
        bool KillProcessViaResourceMonitor(int pid);

    private:
        PluginHost::IShell*               _service{};
        uint32_t                          _connectionId{};
        Exchange::IProcessWatcher*        _processWatcher{};
        Core::Sink<Notification>          _notification;
        Core::Sink<ResourceMonitorSink>   _resourceMonitorSink;
        Exchange::IResourceMonitor*       _resourceMonitorService{};

        friend class Notification;
    };

} // namespace Plugin
} // namespace WPEFramework
