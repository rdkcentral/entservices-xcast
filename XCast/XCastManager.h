/**
 * If not stated otherwise in this file or this component's LICENSE
 * file the following copyright and licenses apply:
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
 **/
#include <string>
#include <thread>
#include <mutex>
#include <iostream>
#include <list>
#include <fstream>
#include "Module.h"
#include "tptimer.h"
#include "XCastNotifier.h"
#include "XCastCommon.h"
#include <gdialservicecommon.h>
#include <gdialservice.h>

using namespace std;

// Forward declaration
namespace WPEFramework {
    namespace PluginHost {
        class IShell;
    }
}

/**
 * This is the Manager class for interacting with gdial library.
 */
class XCastManager : public GDialNotifier
{
protected:
    XCastManager() : m_observer(nullptr) {}
public:
    virtual ~XCastManager();
    /**
     * Initialize gdialService to communication with gdial server
     */
    bool initialize(WPEFramework::PluginHost::IShell* pluginService, const std::string& gdial_interface_name, bool networkStandbyMode );
    void deinitialize();

    /** Shutdown gdialService connectivity */
    void shutdown();
    /**
     *The application state change function . This is invoked from application side.
     *   @param app - The application name
     *   @param state - The state of the application
     *   @param id - The application identifier
     *   @param error - The error string if the requested application is not available or due to other errors
     *   @return indicates whether state is properly communicated to rtdial server.
     */
    int applicationStateChanged( const string& app, const string& state, const string& id, const string& error);
    /**
     *This function will enable cast service by default.
     *@param friendlyname - friendlyname
     *@param enableService - Enable/Disable the SSDP discovery of Dial server
     */
    void enableCastService(const string& friendlyname,bool enableService = true);

    void registerApplications (std::vector<DynamicAppConfig*>& appConfigList);
    string  getProtocolVersion(void);
    void setNetworkStandbyMode(bool nwStandbymode);

    int setManufacturerName( string manufacturer);
    string getManufacturerName(void);
    int setModelName( string model);
    string getModelName(void);

    /**
     *Request the single instance of this class
     */
    static  XCastManager * getInstance();

    virtual void onApplicationLaunchRequest(string appName, string parameter) override;
    virtual void onApplicationLaunchRequestWithLaunchParam (string appName,string strPayLoad, string strQuery, string strAddDataUrl) override;
    virtual void onApplicationStopRequest(string appName, string appID) override;
    virtual void onApplicationHideRequest(string appName, string appID) override;
    virtual void onApplicationResumeRequest(string appName, string appID) override;
    virtual void onApplicationStateRequest(string appName, string appID) override;
    virtual void updatePowerState(string powerState) override;

    /**
     *Call back function for rtConnection
     */
    int isGDialStarted();

    void setService(XCastNotifier * service){
        m_observer = service;
    }
private:
    //Internal methods
    XCastNotifier * m_observer;

    void getWiFiInterface(std::string& WiFiInterfaceName);
    void getGDialInterfaceName(std::string& interfaceName);
    std::string getReceiverID(WPEFramework::PluginHost::IShell* pluginService);
    bool envGetValue(const char *key, std::string &value);
    /**
     * Retrieves the device serial number from the deviceInfo plugin using on-demand acquisition.
     * @param pluginService The IShell service to query DeviceInfo plugin from
     * @param serialNumber [out] Contains serial number string on success.
     * @return true if the serial number was successfully retrieved, false otherwise.
     */
    bool getSerialNumberFromDeviceInfo(WPEFramework::PluginHost::IShell* pluginService, std::string& serialNumber);

    /**
     * Generates a UUID version 5 using the DNS namespace and SHA-1 hashing according to RFC 4122,
     * based on the provided serial number string.
     * @param serialNumber The serial number string to use as the basis for the UUID.
     * @return A string containing the generated UUID v5 on success; empty string on any failure.
     */
    std::string generateUUIDv5FromSerialNumber(const std::string& serialNumber);

    // Class level contracts
    // Singleton instance
    static XCastManager * _instance;
    std::recursive_mutex m_mutexSync;
};
