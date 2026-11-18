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
 #include "SystemMonitorSubscriber.h"
 


namespace WPEFramework {
namespace Plugin {

SERVICE_REGISTRATION(
    SystemMonitorSubscriber,
    1,
    0
);

SystemMonitorSubscriber::SystemMonitorSubscriber()
    : _service(nullptr)
    , _systemMonitor(nullptr)
    , _notification(*this)
{
}

SystemMonitorSubscriber::~SystemMonitorSubscriber()
{
}

const string SystemMonitorSubscriber::Initialize(
    PluginHost::IShell* service)
{
    ASSERT(service != nullptr);

    _service = service;
    _service->AddRef();

    SYSLOG(Logging::Startup,
           (_T("SystemMonitorSubscriber::Initialize")));

    /*
     * Obtain the SystemMonitor COM-RPC interface.
     *
     * Adjust the callsign if your actual SystemMonitor
     * callsign is different.
     */
    _systemMonitor =
        service->QueryInterfaceByCallsign<Exchange::ISystemMonitor>(
            _T("org.rdk.SystemMonitor"));

    if (_systemMonitor == nullptr) {

        SYSLOG(
            Logging::Error,
            (_T("Unable to obtain ISystemMonitor")));

        _service->Release();
        _service = nullptr;

        return _T("SystemMonitor interface unavailable");
    }

    /*
     * SUBSCRIBE
     */
    const Core::hresult result =
        _systemMonitor->Register(&_notification);

    if (result != Core::ERROR_NONE) {

        SYSLOG(
            Logging::Error,
            (_T("Failed to register SystemMonitor notification")));

        _systemMonitor->Release();
        _systemMonitor = nullptr;

        _service->Release();
        _service = nullptr;

        return _T("Failed to subscribe to SystemMonitor");
    }

    SYSLOG(
        Logging::Notification,
        (_T("Subscribed to SystemMonitor notifications")));

    return string();
}


void SystemMonitorSubscriber::Deinitialize(
    PluginHost::IShell* service)
{
    ASSERT(service == _service);

    SYSLOG(
        Logging::Shutdown,
        (_T("SystemMonitorSubscriber::Deinitialize")));

    if (_systemMonitor != nullptr) {

        /*
         * UNSUBSCRIBE first.
         */
        const Core::hresult result =
            _systemMonitor->Unregister(&_notification);

        if (result != Core::ERROR_NONE) {
            SYSLOG(
                Logging::Error,
                (_T("Failed to unregister SystemMonitor notification")));
        }

        _systemMonitor->Release();
        _systemMonitor = nullptr;
    }

    if (_service != nullptr) {

        _service->Release();
        _service = nullptr;
    }
}


string SystemMonitorSubscriber::Information() const
{
    return _T("Subscriber plugin for SystemMonitor events");
}


void SystemMonitorSubscriber::OnLowMemory(
    const uint64_t freeMemory,
    const uint64_t threshold)
{
    SYSLOG(
        Logging::Notification,
        (_T("SystemMonitor EVENT: Low Memory")));

    TRACE(
        Trace::Information,
        (_T("Low Memory: free=%llu threshold=%llu"),
         static_cast<unsigned long long>(freeMemory),
         static_cast<unsigned long long>(threshold)));

    /*
     * Add subscriber action here.
     *
     * Example:
     *   - generate telemetry
     *   - notify another plugin
     *   - perform cleanup
     *   - write diagnostic information
     */
}


void SystemMonitorSubscriber::OnHighCpuUsage(
    const double cpuUsage,
    const double threshold)
{
    SYSLOG(
        Logging::Notification,
        (_T("SystemMonitor EVENT: High CPU")));

    TRACE(
        Trace::Information,
        (_T("High CPU: usage=%.2f threshold=%.2f"),
         cpuUsage,
         threshold));

    /*
     * Add subscriber action here.
     */
}

} // namespace Plugin
} // namespace WPEFramework