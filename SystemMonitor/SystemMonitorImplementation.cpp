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
            _initialized = true;
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
    /*
    const uint64_t usedMemory = totalMemory >= freeMemory ? totalMemory - freeMemory : 0;

    double usedPercentage = 0.0;

    if (totalMemory != 0) {
        usedPercentage = (static_cast<double>(usedMemory) * 100.0) / static_cast<double>(totalMemory);
    }
    TRACE(Trace::Information, (_T("Memory event:total=%llu free=%llu"),
         static_cast<unsigned long long>(
             totalMemory),
         static_cast<unsigned long long>(
             freeMemory)));
    */
    JsonObject parameters;

    parameters[_T("state")] = _memoryLow ? _T("LOW_MEMORY") : _T("NORMAL");

   // parameters[_T("totalMemory")] = totalMemory;

    parameters[_T("freeMemory")] = freeMemory;

    //parameters[_T("usedMemory")] = usedMemory;

    //parameters[_T("usedPercentage")] = usedPercentage;

    parameters[_T("highUsageThreshold")] = _memoryHighUsageThreshold;

   // TRACE(Trace::Information,(_T("Sending JSON-RPC event OnLowMemory:free=%llu ),
     //    static_cast<unsigned long long>(freeMemory));

    Notify(_T("OnLowMemory"),parameters);
}

void SystemMonitorImplementation::NotifyHighCpuUsage(const double cpuUsage,const double threshold)
{
    JsonObject parameters;

    TRACE(Trace::Information,(_T("CPU event: usage=%.2f threshold=%.2f"),
         cpuUsage,
         threshold));

    parameters[_T("state")] = _cpuHigh ? _T("HIGH"): _T("NORMAL");

    parameters[_T("cpuUsage")] = cpuUsage;

    parameters[_T("threshold")] = threshold;

    TRACE(Trace::Information, (_T("Sending JSON-RPC event OnCpuHighUsage: state=%s usage=%.2f threshold=%.2f"),
         _cpuHigh ? _T("HIGH") : _T("NORMAL"),
         cpuUsage,
         threshold));

    Notify(_T("onCpuHighUsage"),parameters);
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
        return Core::ERROR_NONE;
    }

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
/*


Core::hresult SystemMonitorImplementation::TriggerMemoryNotification()
{
    constexpr uint64_t testTotalMemory = 2101071872ULL;

    constexpr uint64_t testFreeMemory = 65122304ULL;

    _memoryLow = true;

    NotifyLowMemory(totalMemory,freeMemory);

    return Core::ERROR_NONE;
}

Core::hresult SystemMonitorImplementation::TriggerCpuNotification()
{
    constexpr double testCpuUsage = 95.0;

    _cpuHigh = true;

    NotifyCpuHighUsage(testCpuUsage,_cpuHighThreshold);

    return Core::ERROR_NONE;
}
*/
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
} // plugin
} // WPEFramework
