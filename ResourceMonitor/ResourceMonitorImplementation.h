#pragma once

#include "Module.h"
#include <interfaces/Ids.h>
#include <interfaces/IResourceMonitor.h>

#include <com/com.h>
#include <core/core.h>
#include <mutex>
#include <list>
#include <string>

namespace WPEFramework {
namespace Plugin {

    class ResourceMonitorImplementation : public Exchange::IResourceMonitor {
    public:
        ResourceMonitorImplementation();
        ~ResourceMonitorImplementation() override;

        ResourceMonitorImplementation(const ResourceMonitorImplementation&) = delete;
        ResourceMonitorImplementation& operator=(const ResourceMonitorImplementation&) = delete;

    public:
        // IResourceMonitor registration
        Core::hresult Register(Exchange::IResourceMonitor::IProcessKilledNotification* notification) override;
        Core::hresult Unregister(const Exchange::IResourceMonitor::IProcessKilledNotification* notification) override;

        // IResourceMonitor APIs
        Core::hresult GetApiVersionNumber(int& version) override;
        Core::hresult GetState() override;
        Core::hresult GetSystemResourceInfo(const string& topresult) override;
        Core::hresult KillProcess(int PID, bool& result) override;

        BEGIN_INTERFACE_MAP(ResourceMonitorImplementation)
        INTERFACE_ENTRY(Exchange::IResourceMonitor)
        END_INTERFACE_MAP

    private:
        // Fires OnProcessKilled to all registered listeners
        void NotifyProcessKilled(const string& processName, int pid, int exitCode);

        mutable Core::CriticalSection _adminLock;
        std::list<Exchange::IResourceMonitor::IProcessKilledNotification*> _processKilledNotifications;
    };

} // namespace Plugin
} // namespace WPEFramework

