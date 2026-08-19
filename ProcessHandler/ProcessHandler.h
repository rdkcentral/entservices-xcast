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
 * @file ProcessHandler.h
 * @brief Thunder out-of-process plugin shell: ProcessHandler.
 *
 * Runs inside the Thunder framework process and forwards every JSON-RPC call
 * across the COM-RPC boundary to ProcessHandlerImplementation, which runs in
 * its own WPEProcess host.
 *
 * Supported APIs (registered manually – no J* file):
 *   kill  { "pid": <uint32> }           → kills the given PID via SIGKILL
 *   top   {}                            → returns top -b -n1 output
 *
 * There are no events.
 */

#pragma once

#include "Module.h"
#include <interfaces/IProcessHandler.h>
#include "tracing/Logging.h"

namespace WPEFramework {
namespace Plugin {

    class ProcessHandler : public PluginHost::IPlugin, public PluginHost::JSONRPC {
    private:
        // Monitors the out-of-process connection; triggers clean deactivation on crash.
        class Notification : public RPC::IRemoteConnection::INotification {
        private:
            Notification()                          = delete;
            Notification(const Notification&)       = delete;
            Notification& operator=(const Notification&) = delete;

        public:
            explicit Notification(ProcessHandler* parent)
                : _parent(*parent)
            {
                ASSERT(parent != nullptr);
            }

            ~Notification() override = default;

            BEGIN_INTERFACE_MAP(Notification)
            INTERFACE_ENTRY(RPC::IRemoteConnection::INotification)
            END_INTERFACE_MAP

            void Activated(RPC::IRemoteConnection* /* connection */) override {}

            void Deactivated(RPC::IRemoteConnection* connection) override
            {
                if (_parent._connectionId == connection->Id()) {
                    _parent.Deactivated(connection);
                }
            }

        private:
            ProcessHandler& _parent;
        };

    public:
        ProcessHandler(const ProcessHandler&)            = delete;
        ProcessHandler& operator=(const ProcessHandler&) = delete;

        ProcessHandler();
        ~ProcessHandler() override;

        BEGIN_INTERFACE_MAP(ProcessHandler)
        INTERFACE_ENTRY(PluginHost::IPlugin)
        INTERFACE_ENTRY(PluginHost::IDispatcher)
        INTERFACE_AGGREGATE(Exchange::IProcessHandler, _impl)
        END_INTERFACE_MAP

        // ----- IPlugin -----
        const string Initialize(PluginHost::IShell* service) override;
        void         Deinitialize(PluginHost::IShell* service) override;
        string       Information() const override;

    private:
        void Deactivated(RPC::IRemoteConnection* connection);

    private:
        PluginHost::IShell*         _service{};
        uint32_t                    _connectionId{};
        Exchange::IProcessHandler*  _impl{};
        Core::Sink<Notification>    _notification;
    };

} // namespace Plugin
} // namespace WPEFramework
