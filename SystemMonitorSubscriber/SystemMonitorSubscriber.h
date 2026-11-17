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
#include "UtilsLogging.h"
#include "tracing/Logging.h"


namespace WPEFramework {
namespace Plugin {

class SystemMonitorSubscriber : public PluginHost::IPlugin {

private:

    class Notification : public Exchange::ISystemMonitor::INotification {
    public:
        explicit Notification(SystemMonitorSubscriber& parent)
            : _parent(parent)
        {
        }

        ~Notification() override = default;

        Notification(const Notification&) = delete;
        Notification& operator=(const Notification&) = delete;

        void OnLowMemory(
            const uint64_t freeMemory,
            const uint64_t threshold) override
        {
            _parent.OnLowMemory(freeMemory, threshold);
        }

        void OnHighCpuUsage(
            const double cpuUsage,
            const double threshold) override
        {
            _parent.OnHighCpuUsage(cpuUsage, threshold);
        }

        BEGIN_INTERFACE_MAP(Notification)
            INTERFACE_ENTRY(Exchange::ISystemMonitor::INotification)
        END_INTERFACE_MAP

    private:
        SystemMonitorSubscriber& _parent;
    };

public:

    SystemMonitorSubscriber();

    ~SystemMonitorSubscriber() override;

    SystemMonitorSubscriber(const SystemMonitorSubscriber&) = delete;
    SystemMonitorSubscriber& operator=(const SystemMonitorSubscriber&) = delete;

    // PluginHost::IPlugin
    const string Initialize(
        PluginHost::IShell* service) override;

    void Deinitialize(
        PluginHost::IShell* service) override;

    string Information() const override;

    BEGIN_INTERFACE_MAP(SystemMonitorSubscriber)
        INTERFACE_ENTRY(PluginHost::IPlugin)
    END_INTERFACE_MAP

private:

    void OnLowMemory(
        const uint64_t freeMemory,
        const uint64_t threshold);

    void OnHighCpuUsage(
        const double cpuUsage,
        const double threshold);

private:

    PluginHost::IShell* _service;

    Exchange::ISystemMonitor* _systemMonitor;

    Core::SinkType<Notification> _notification;
};

} // namespace Plugin
} // namespace WPEFramework