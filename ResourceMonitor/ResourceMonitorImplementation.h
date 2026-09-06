#pragma once

#include "Module.h"
#include "UtilsLogging.h"
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
        Core::hresult Register(Exchange::IResourceMonitor::IInitializationNotification* notification) override;
        Core::hresult Unregister(const Exchange::IResourceMonitor::IInitializationNotification* notification) override;

        // IResourceMonitor APIs
        Core::hresult GetApiVersionNumber(int& version) override;
        Core::hresult GetState() override;
        Core::hresult GetSystemResourceInfo(string& topresult) override;
        Core::hresult KillProcess(int PID, bool& result) override;

        BEGIN_INTERFACE_MAP(ResourceMonitorImplementation)
        INTERFACE_ENTRY(Exchange::IResourceMonitor)
        END_INTERFACE_MAP

    private:
        // Fires OnProcessKilled to all registered listeners
        void NotifyProcessKilled(const string& processName, int pid, int exitCode);
        // Fires OnInitialized to listeners registered before initialization completes.
        void NotifyInitialized();

        // //Internal method to handle GetsystemResourceInfo
        // Core::hresult GetSystemResourceInfoInternal(const string& topresult);
        
        mutable Core::CriticalSection _adminLock;
        std::list<Exchange::IResourceMonitor::IProcessKilledNotification*> _processKilledNotifications;
        std::list<Exchange::IResourceMonitor::IInitializationNotification*> _initializationNotifications;
        bool _initialized;
    };

} // namespace Plugin
} // namespace WPEFramework

