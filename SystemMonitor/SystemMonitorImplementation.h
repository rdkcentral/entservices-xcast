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
#include <interfaces/Ids.h>
#include <interfaces/ISystemMonitor.h>
#include <interfaces/IConfiguration.h>
#include "tracing/Logging.h"
#include <plugins/plugins.h>
#include <core/core.h>
#include <com/com.h>
#include <list>


namespace WPEFramework{
    namespace Plugin{
    class SystemMonitorImplementation : public Exchange::ISystemMonitor{

    private:
    mutable Core::CriticalSection _adminLock;
    PluginHost::IShell* _service;
    double _cpuHighThreshold;
    uint32_t _memoryHighUsageThreshold;
    bool  _cpuHigh;
    bool _memoryLow;
     std::list<Exchange::ISystemMonitor::INotification*> _notificationSinks;
                      
    public:
    SystemMonitorImplementation();
    virtual ~SystemMonitorImplementation();
    SystemMonitorImplementation(const SystemMonitorImplementation& other) = delete;
    SystemMonitorImplementation& operator=(const SystemMonitorImplementation& other) = delete;

    BEGIN_INTERFACE_MAP(SystemMonitorImplementation)
        INTERFACE_ENTRY(Exchange::ISystemMonitor)
    END_INTERFACE_MAP
        
    

     public:

	virtual Core::hresult Register(Exchange::ISystemMonitor::INotification* notification) = 0;
   	virtual Core::hresult Unregister(Exchange::ISystemMonitor::INotification* notification) = 0;

// ISystemMonitor APIs
    Core::hresult Uptime(uint64_t& uptime ) const override;
    Core::hresult TotalMemory(uint64_t& memory ) const override;
    Core::hresult FreeMemory(uint64_t&  memory ) const override;
    Core::hresult CpuLoad(double&  load ) const override;
	
    Core::hresult TriggerMemoryNotification() override;
    Core::hresult TriggerCpuNotification() override;
    

	   /*
     	 * Common event entry points.
     	 */
	
	void NotifyLowMemory(const uint64_t freeMemory, const double threshold);
    void NotifyHighCpuUsage(const double cpuUsage,const double threshold);
   

    };

                                        } // plugin
} //WPEFramework