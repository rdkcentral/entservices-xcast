/*
* If not stated otherwise in this file or this component's LICENSE file the
* following copyright and licenses apply:
*
* Copyright 2026 [PLEASE ADD COPYRIGHT NAME!]
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
#include <interfaces/json/JsonData_SystemMonitor.h>
#include <interfaces/json/JSystemMonitor.h>
#include "interfaces/ISystemMonitor.h"
#include <interfaces/IConfiguration.h>
#include "UtilsLogging.h"
#include "tracing/Logging.h"
#include <mutex>
#include<list>



namespace WPEFramework  {
namespace Plugin {

    class SystemMonitor : public PluginHost::IPlugin, 
                          public PluginHost::JSONRPC
    {
         private:
        class Notification : public RPC::IRemoteConnection::INotification,
                             public Exchange::ISystemMonitor::INotification
        {
            private:
                Notification() = delete;
                Notification(const Notification&) = delete;
                Notification& operator=(const Notification&) = delete;
            public:     
                explicit Notification(SystemMonitor* parent)
                    : _parent(parent)
                {
                }
                ~Notification() override = default;
                  void Activated(RPC::IRemoteConnection* connection)                    {
                      // Handle activation
                  }
                void Deactivated(RPC::IRemoteConnection* connection)  
                {
                    _parent->Deactivated(connection);
                }
                     BEGIN_INTERFACE_MAP(Notification)
                    INTERFACE_ENTRY(Exchange::ISystemMonitor::INotification)
                    INTERFACE_ENTRY(RPC::IRemoteConnection::INotification)
                    END_INTERFACE_MAP

                void OnHighCpuUsage(const double cpuUsage,const double threshold)     
                {
                    //if (_parent) {
                     //   _parent->OnHighCpuUsage(cpuUsage, threshold);
                    //}
                }
                void OnLowMemory(const uint64_t freeMemory, const double threshold) 
                {
                   // if (_parent) {
                    //    _parent->OnLowMemory(freeMemory, threshold);
                    //}
                }
                 
            private:
            SystemMonitor* _parent;

        }; // class Notification

    private:
    PluginHost::IShell* _service;
    uint32_t _connectionId{};
    Exchange::ISystemMonitor* _systemMonitor{};
    Core::Sink<Notification> _systemMonitorNotification;
    bool   _notificationRegistered{false};
    public:
        SystemMonitor(const SystemMonitor&) = delete;
        SystemMonitor& operator=(const SystemMonitor&) = delete;
        SystemMonitor(SystemMonitor&&) = delete;
        SystemMonitor& operator=(SystemMonitor&&) = delete;

        SystemMonitor();
         
        
        ~SystemMonitor() override;

        BEGIN_INTERFACE_MAP(SystemMonitor)
            INTERFACE_ENTRY(PluginHost::IPlugin)
            INTERFACE_ENTRY(PluginHost::IDispatcher)
            INTERFACE_AGGREGATE(Exchange::ISystemMonitor,_systemMonitor)
        END_INTERFACE_MAP

        // IPlugin Methods
        const string Initialize(PluginHost::IShell* service) override;
        void Deinitialize(PluginHost::IShell* service) override;
        string Information() const override;
        void Deactivated(RPC::IRemoteConnection* connection);

    };  

} // Plugin
} // Thunder
