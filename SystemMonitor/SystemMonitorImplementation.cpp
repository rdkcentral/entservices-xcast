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

 #include "SystemMonitorImplementation.h"
#include <fstream>
#include <sstream>
#include <string>
#include <sys/sysinfo.h>

 namespace WPEFramework {
    namespace Plugin {
        SERVICE_REGISTRATION(SystemMonitorImplementation, 1, 0);
 
        SystemMonitorImplementation::SystemMonitorImplementation(): 
        _service(nullptr),
	    _notificationSinks(),
	    _cpuHighThreshold(90.0),
	    _memoryHighUsageThreshold(90),
	    _adminLock(),
        _cpuHigh(false),
        _memoryLow(false)
        {
            LOGINFO("SystemMonitorImplementation constructed");
        }
    SystemMonitorImplementation::~SystemMonitorImplementation(){
         LOGINFO("SystemMonitorImplementation destroyed");
        _adminLock.Lock();
        for (auto* notification : _notificationSinks) {
            notification->Release();
        }
        _notificationSinks.clear();
        _adminLock.Unlock();
    }
Core::hresult SystemMonitorImplementation::Register(Exchange::ISystemMonitor::INotification* notification)
    {
        
	     ASSERT(notification != nullptr);
	    Core::hresult result = Core::ERROR_NONE;

	    _adminLock.Lock();

	    const auto index = std::find(
			    _notificationSinks.begin(),
			    _notificationSinks.end(),
			    notification);

	    if (index == _notificationSinks.end()) {
		    /*
		     * SystemMonitor stores the interface after this method
		     * returns, so it must hold its own reference.
		     */
		    notification->AddRef();

		    _notificationSinks.push_back(notification);

		    TRACE(Trace::Information,(_T("SystemMonitor notification client registered")));
	    } else {
		    result = Core::ERROR_ALREADY_CONNECTED;

		    TRACE(Trace::Warning,(_T("SystemMonitor client already registered")));
	    }

	    _adminLock.Unlock();

	    return result;
    }

    Core::hresult SystemMonitorImplementation::Unregister(Exchange::ISystemMonitor::INotification* notification)
    {
	    ASSERT(notification != nullptr);
        Core::hresult status = Core::ERROR_GENERAL;


	    Exchange::ISystemMonitor::INotification* removedSink = nullptr;

	    _adminLock.Lock();

	    const auto index = std::find(
			    _notificationSinks.begin(),
			    _notificationSinks.end(),
			    notification);

	    if (index != _notificationSinks.end()) {
		    removedSink = *index;

		    _notificationSinks.erase(index);
	    }

	    _adminLock.Unlock();

	    if (removedSink == nullptr) {
		    TRACE(Trace::Warning,(_T("SystemMonitor client was not registered")));

		    return Core::ERROR_NOT_EXIST;
	    }

	    removedSink->Release();

	    TRACE(Trace::Information,(_T("SystemMonitor notification client unregistered")));

	    return Core::ERROR_NONE;
    }

   

void SystemMonitorImplementation::NotifyLowMemory(const uint64_t freeMemory, const double threshold)
{
     
     std::vector<Exchange::ISystemMonitor::INotification*> sinks;

    {
        Core::SafeSyncType<Core::CriticalSection> lock(_adminLock);

        sinks.reserve(_notificationSinks.size());

        for (auto* sink : _notificationSinks) {
            if (sink != nullptr) {
                sink->AddRef();
                sinks.push_back(sink);
            }
        }
    }
    TRACE(
        Trace::Information,
        (_T("Low memory detected: available=%llu KiB, threshold=%llu KiB"),
         static_cast<unsigned long long>(freeMemory),
         static_cast<unsigned long long>(threshold)));

    /*
     * Do not hold _adminLock while invoking external callbacks.
     *
     * The callback could cause Unregister(), COM-RPC traffic or another
     * call into this implementation.
     */
    for (auto* sink : sinks) {
        sink->OnLowMemory(
            freeMemory,
            threshold);

        sink->Release();
    }
}

void SystemMonitorImplementation::NotifyHighCpuUsage(const double cpuUsage,const double threshold)
{
     std::vector<Exchange::ISystemMonitor::INotification*> sinks;

    {
        Core::SafeSyncType<Core::CriticalSection> lock(_adminLock);

        sinks.reserve(_notificationSinks.size());

        for (auto* sink : _notificationSinks) {
            if (sink != nullptr) {
                sink->AddRef();
                sinks.push_back(sink);
            }
        }
    }

    TRACE(
        Trace::Information,
        (_T("High CPU usage detected: usage=%.2f%% threshold=%.2f%%"),
         cpuUsage,
         threshold));

    // Don't hold _adminLock while calling external notification sinks.
    for (auto* sink : sinks) {
        sink->OnHighCpuUsage(cpuUsage, threshold);
        sink->Release();
    }
}


    Core::hresult SystemMonitorImplementation::Uptime(uint64_t&  uptime) const {
         struct sysinfo info {};

    if (sysinfo(&info) != 0) {
        return Core::ERROR_GENERAL;
    }

    uptime = static_cast<uint64_t>(info.uptime);
        return Core::ERROR_NONE;
    }
    Core::hresult SystemMonitorImplementation::TotalMemory(uint64_t&  memory ) const {
        struct sysinfo info {};

    if (sysinfo(&info) != 0) {
        return Core::ERROR_GENERAL;
    }

    memory = static_cast<uint64_t>(info.totalram) *
             static_cast<uint64_t>(info.mem_unit);
        return Core::ERROR_NONE;
    } 
    Core::hresult SystemMonitorImplementation::FreeMemory(uint64_t&  memory) const {
        struct sysinfo info {};

    if (sysinfo(&info) != 0) {
        return Core::ERROR_GENERAL;
    }

    memory = static_cast<uint64_t>(info.freeram) *
             static_cast<uint64_t>(info.mem_unit);
    //NotifyLowMemory(memory,_memoryHighUsageThreshold);
        return Core::ERROR_NONE;
    }
    Core::hresult SystemMonitorImplementation::CpuLoad(double&  load ) const {
        std::ifstream loadAverage("/proc/loadavg");

    if (!loadAverage.is_open()) {
        return Core::ERROR_GENERAL;
    }

    loadAverage >> load;

    if (loadAverage.fail()) {
        return Core::ERROR_GENERAL;
    }
   // NotifyCpuHighUsage(load,_cpuHighThreshold);
        return Core::ERROR_NONE;
    }
#if 0

void SystemMonitorImplementation::ProcessCpuSample()
{
    double currentCpuUsage = 0.0;

    const Core::hresult result = CPULoad(currentCpuUsage);

    if (result != Core::ERROR_NONE) {
        TRACE(Trace::Error,(_T("Failed to retrieve CPU usage: %u"), result));

        return;
    }

    TRACE(Trace::Information,(_T("CPU sample: %.2f%% high=%s"),
         currentCpuUsage,
         _cpuHigh ? _T("true") : _T("false")));

    /*
     * Transition from normal to high.
     */
    if ((_cpuHigh == false) && (currentCpuUsage >= _cpuHighThreshold)) {

        _cpuHigh = true;

        NotifyCpuHighUsage(currentCpuUsage,_cpuHighThreshold);
    }

    /*
     * Transition from high to normal.
     *
     * This implementation sends the same event with state=NORMAL.
     */
    else if ((_cpuHigh == true) && (currentCpuUsage <= _cpuRecoveryThreshold)) {

        _cpuHigh = false;

        NotifyCpuHighUsage(currentCpuUsage,_cpuHighThreshold);
    }
}

void SystemMonitorImplementation::ProcessMemorySample()
{
    uint64_t totalMemory = 0;
    uint64_t freeMemory = 0;

    if (TotalMemory(totalMemory) != Core::ERROR_NONE) {

        TRACE(Trace::Error,(_T("Failed to retrieve total memory")));

        return;
    }

    if (FreeMemory(freeMemory) != Core::ERROR_NONE) {

        TRACE(Trace::Error,(_T("Failed to retrieve free memory")));

        return;
    }

    if (totalMemory == 0) {
        return;
    }

    const uint64_t usedMemory = totalMemory >= freeMemory ? totalMemory - freeMemory : 0;

    const uint32_t usedPercentage = static_cast<uint32_t>((usedMemory * 100ULL) / totalMemory);

    /*
     * Normal to low-memory transition.
     */
    if ((_memoryLow == false) && (usedPercentage >= _memoryHighUsageThreshold)) {

        _memoryLow = true;

        NotifyLowMemory(freeMemory,_memoryHighUsageThreshold);
    }

    /*
     * Low-memory to normal transition.
     */
    else if ((_memoryLow == true) && (usedPercentage <= _memoryRecoveryThreshold)) {

        _memoryLow = false;

        NotifyLowMemory(freeMemory,_memoryHighUsageThreshold);
    }
}

#endif

Core::hresult SystemMonitorImplementation::TriggerMemoryNotification()
{
    constexpr uint64_t testTotalMemory = 2101071872ULL;

    constexpr uint64_t testFreeMemory = 65122304ULL;

    _memoryLow = true;

    NotifyLowMemory(testFreeMemory,_memoryHighUsageThreshold);

    return Core::ERROR_NONE;
}

Core::hresult SystemMonitorImplementation::TriggerCpuNotification()
{
    constexpr double testCpuUsage = 95.0;

    _cpuHigh = true;

    NotifyCpuHighUsage(testCpuUsage,_cpuHighThreshold);

    return Core::ERROR_NONE;
}
#if 0
void SystemMonitorImplementation::ReleaseNotificationSinks()
{
	std::list<Exchange::ISystemMonitor::INotification*> subscribers;

	_adminLock.Lock();

	subscribers.swap(_notificationSinks);

	_adminLock.Unlock();

	for (Exchange::ISystemMonitor::INotification* subscriber : subscribers) {

		if (subscriber != nullptr){

		subscriber->Release();
		}
	}
}
#endif

} // plugin
} // WPEFramework
