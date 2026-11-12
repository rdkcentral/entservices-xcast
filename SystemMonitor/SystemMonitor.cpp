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

#include "SystemMonitor.h"
#include "Module.h"
#include "tracing/Logging.h"
#include <fstream>
#include <sstream>
#include <string>
#include <sys/sysinfo.h>

#include <interfaces/IConfiguration.h> 

#define API_VERSION_NUMBER_MAJOR 1
#define API_VERSION_NUMBER_MINOR 0
#define API_VERSION_NUMBER_PATCH 0

namespace WPEFramework  {


    namespace {

        static Metadata<SystemMonitor> metadata(
        // Version
	API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH,
        // Preconditions
        {},
        // Terminations
        {},
        // Controls
        {}
        );
    }
namespace Plugin {
    SERVICE_REGISTRATION(SystemMonitor, API_VERSION_NUMBER_MAJOR, API_VERSION_NUMBER_MINOR, API_VERSION_NUMBER_PATCH);
   
    SystemMonitor::SystemMonitor(): 
	    PluginHost::IPlugin(),
	    PluginHost::JSONRPC(),
	    Exchange::ISystemMonitor(),
	    _service(nullptr),
	    _systemMonitor(nullptr),
	    _systemMonitorNotification(this)

    {
	    SYSLOG(Logging::Startup, (_T("SystemMonitor Constructor")));

    } 
    SystemMonitor::~SystemMonitor(){
	   SYSLOG(Logging::Shutdown, (string(_T("SystemMonitor Destructor"))));
    }

    const string SystemMonitor::Initialize(PluginHost::IShell* service) {
        string message;
	    ASSERT(service != nullptr);
	    ASSERT(_service == nullptr);

	    _service = service;
	    _service->AddRef();
	    Exchange::JSystemMonitor::Register(*this, this);
        _service->Register(&_systemMonitorNotification);

        _systemMonitor = _service->Root<Exchange::ISystemMonitor>(_connectionId, 5000, _T("SystemMonitorImplementation"));

        if (_systemMonitor != nullptr) {

            auto configConnection = _systemMonitor->QueryInterface<Exchange::IConfiguration>();
            if (configConnection != nullptr) {
                configConnection->Configure(service);
                configConnection->Release();
            }
                // Subscribe to COM events from the implementation
                _systemMonitor->Register(&_systemMonitorNotification);
                // Invoking Plugin API register to wpeframework
                Exchange::JSystemMonitor::Register(*this, _systemMonitor );
                LOGINFO("SystemMonitor::Initialize: out-of-process impl ready, connectionId=%u",
                        _connectionId);
            } else {
                SYSLOG(Logging::Startup,
                       (_T("SystemMonitor::Initialize: Root<ISystemMonitor> returned null")));
                       message = _T("SystemMonitor plugin could not be initialised");
            }
            if (0 != message.length())
            {
                LOGERR("'%s'", message.c_str());
                Deinitialize(service);
            }

	    TRACE(Trace::Information, (_T("SystemMonitor monitoring started")));

	    return message;
    }

    void SystemMonitor::Deinitialize(PluginHost::IShell* service) {
        ASSERT(service != nullptr);
        ASSERT(_service == service);

        SYSLOG(Logging::Shutdown, (string(_T("SystemMonitor::Deinitialize"))));
        // Make sure the Activated and Deactivated are no longer called before we start cleaning up..
        if (_service != nullptr)
        {
            _service->Unregister(&_systemMonitorNotification);
        }
        if (_systemMonitor != nullptr) {
            _systemMonitor->Unregister(&_systemMonitorNotification);
            Exchange::JSystemMonitor::Unregister(*this);
            // Stop processing:
            RPC::IRemoteConnection* connection = service->RemoteConnection(_connectionId);
            VARIABLE_IS_NOT_USED uint32_t result = _systemMonitor->Release();

            _systemMonitor = nullptr;

            // It should have been the last reference we are releasing,
            // so it should endup in a DESTRUCTION_SUCCEEDED, if not we
            // are leaking...
            ASSERT(result == Core::ERROR_DESTRUCTION_SUCCEEDED);
             // If this was running in a (container) process...
            if (nullptr != connection)
            {
               // Lets trigger the cleanup sequence for
               // out-of-process code. Which will guard
               // that unwilling processes, get shot if
               // not stopped friendly :-)
               connection->Terminate();
               connection->Release();
            }
        }
          _connectionId = 0;

        if (_service != nullptr) {
            _service->Release();
            _service = nullptr;
        }
	  SYSLOG(Logging::Shutdown, (string(_T("SystemMonitor de-initialised"))));
    }

    string SystemMonitor::Information() const {
        return (string());
    }

    // ---------------------------------------------------------------------
        // Deactivated — called by Notification::Deactivated() when the
        // out-of-process impl crashes.  We schedule a plugin deactivation
        // through the worker pool so it happens on a Thunder-owned thread.
        // ---------------------------------------------------------------------
        void SystemMonitor::Deactivated(RPC::IRemoteConnection* connection)
        {
            if (connection->Id() == _connectionId) {
                ASSERT(_service != nullptr);
                Core::IWorkerPool::Instance().Submit(
                    PluginHost::IShell::Job::Create(
                        _service, PluginHost::IShell::DEACTIVATED, PluginHost::IShell::FAILURE));
            }
        }

    
} // Plugin
} // Thunder
