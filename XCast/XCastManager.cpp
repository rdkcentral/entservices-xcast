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

#include "XCastManager.h"
#include "UtilsJsonRpc.h"
#include "rfcapi.h"
#include <interfaces/IConfiguration.h>
#include <interfaces/IDeviceInfo.h>
#include <cryptalgo/Hash.h>
#include <sstream>
#include <iomanip>

using namespace std;
using namespace WPEFramework;

#define COMMON_DEVICE_PROPERTIES_FILE   "/etc/device.properties"

#define LOCATE_CAST_FIRST_TIMEOUT_IN_MILLIS  5000  //5 seconds
#define LOCATE_CAST_SECOND_TIMEOUT_IN_MILLIS 15000  //15 seconds
#define LOCATE_CAST_THIRD_TIMEOUT_IN_MILLIS  30000  //30 seconds
#define LOCATE_CAST_FINAL_TIMEOUT_IN_MILLIS  60000  //60 seconds
#define EVENT_LOOP_ITERATION_IN_100MS     100000


static gdialService* gdialCastObj = NULL;
XCastManager * XCastManager::_instance = nullptr;
std::string m_modelName = "";
std::string m_manufacturerName = "";
std::string m_defaultfriendlyName = "";
std::string m_uuid = "";
std::string m_defaultAppList = "";

//XDIALCAST EVENT CALLBACK
/**
 * Callback function for application launch request from an app
 */
void XCastManager::onApplicationLaunchRequestWithLaunchParam(string appName,string strPayLoad, string strQuery, string strAddDataUrl)
{
    if ( nullptr != m_observer )
    {
        m_observer->onXcastApplicationLaunchRequestWithParam(appName,std::move(strPayLoad),strQuery,strAddDataUrl);
    }
}

void XCastManager::onApplicationLaunchRequest(string appName, string parameter)
{
    if ( nullptr != m_observer )
    {
        m_observer->onXcastApplicationLaunchRequest(std::move(appName),std::move(parameter));
    }
}

void XCastManager::onApplicationStopRequest(string appName, string appID)
{
    if ( nullptr != m_observer )
    {
        m_observer->onXcastApplicationStopRequest(std::move(appName),std::move(appID));
    }
}

void XCastManager::onApplicationHideRequest(string appName, string appID)
{
    if ( nullptr != m_observer )
    {
        m_observer->onXcastApplicationHideRequest(std::move(appName),std::move(appID));
    }
}

void XCastManager::onApplicationResumeRequest(string appName, string appID)
{
    if ( nullptr != m_observer )
    {
        m_observer->onXcastApplicationResumeRequest(std::move(appName),std::move(appID));
    }
}

void XCastManager::onApplicationStateRequest(string appName, string appID)
{
    if ( nullptr != m_observer )
    {
        m_observer->onXcastApplicationStateRequest(std::move(appName),std::move(appID));
    }
}

void XCastManager::updatePowerState(string powerState)
{
    if ( nullptr != m_observer )
    {
        m_observer->onXcastUpdatePowerStateRequest(std::move(powerState));
    }
}

XCastManager::~XCastManager()
{
    _instance = nullptr;
    m_observer = nullptr;
}

bool XCastManager::initialize(WPEFramework::PluginHost::IShell* pluginService, const std::string& gdial_interface_name, bool networkStandbyMode )
{
    std::vector<std::string> gdial_args;
    bool returnValue = false,
         isFriendlyNameEnabled = true,
         isWolWakeEnableEnabled = true;

    if (gdial_interface_name.empty())
    {
        LOGERR("Interface Name should not be empty");
        return false;
    }

    lock_guard<recursive_mutex> lock(m_mutexSync);
#ifdef RFC_ENABLED
    RFC_ParamData_t param = {0};
    WDMP_STATUS wdmpStatus = WDMP_SUCCESS;
    wdmpStatus = getRFCParameter(const_cast<char *>("XCastPlugin"), "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.XDial.Enable", &param);
    if (wdmpStatus == WDMP_SUCCESS || wdmpStatus == WDMP_ERR_DEFAULT_VALUE)
    {
        if( param.type == WDMP_BOOLEAN )
        {
            if(strncasecmp(param.value,"true",4) != 0 ) {
                LOGINFO("----------XCAST RFC Disabled---------- ");
                return true;
            }
        }
    }
    wdmpStatus = getRFCParameter(const_cast<char *>("XCastPlugin"), "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.XDial.FriendlyNameEnable", &param);
    if (wdmpStatus == WDMP_SUCCESS || wdmpStatus == WDMP_ERR_DEFAULT_VALUE)
    {
        if( param.type == WDMP_BOOLEAN )
        {
            if(strncasecmp(param.value,"true",4) == 0 ) {
                isFriendlyNameEnabled = true;
            }
            else{
                isFriendlyNameEnabled = false;
            }
        }
    }
    wdmpStatus = getRFCParameter(const_cast<char *>("XCastPlugin"), "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.XDial.WolWakeEnable", &param);
    if (wdmpStatus == WDMP_SUCCESS || wdmpStatus == WDMP_ERR_DEFAULT_VALUE)
    {
        if( param.type == WDMP_BOOLEAN )
        {
            if(strncasecmp(param.value,"true",4) == 0 ) {
                isWolWakeEnableEnabled = true;
            }
            else {
                isWolWakeEnableEnabled = false;
            }
        }
    }
    wdmpStatus = getRFCParameter(const_cast<char *>("XCastPlugin"), "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.XDial.AppList", &param);
    if (wdmpStatus == WDMP_SUCCESS || wdmpStatus == WDMP_ERR_DEFAULT_VALUE)
    {
        if( param.type == WDMP_STRING )
        {
            m_defaultAppList = param.value;
        }
    }
#endif //RFC_ENABLED
    std::string temp_interface = "";
    getGDialInterfaceName(temp_interface);

    if (0 == gdial_interface_name.compare("ETHERNET"))
    {
        LOGINFO("VIface[%s:%s] uses \"eth0\"",gdial_interface_name.c_str(),temp_interface.c_str());
        temp_interface = "eth0";
    }
    else if (0 == gdial_interface_name.compare("WIFI"))
    {
        LOGINFO("VIface[%s:%s] uses \"wlan0\"",gdial_interface_name.c_str(),temp_interface.c_str());
        temp_interface = "wlan0";
    }
    else
    {
        LOGINFO("Actual IFace[%s]",temp_interface.c_str());
    }

    gdial_args.push_back("-I");
    gdial_args.push_back(std::move(temp_interface));

    if (m_uuid.empty())
    {
        m_uuid = getReceiverID(pluginService);
    }

    if (!m_uuid.empty())
    {
        gdial_args.push_back("-U");
        gdial_args.push_back(m_uuid);
    }

    if (m_modelName.empty())
    {
        if (!(envGetValue("MODEL_NUM", m_modelName)))
        {
            LOGERR("MODEL_NUM not configured in device properties file");
        }
    }

    if (!m_modelName.empty())
    {
        gdial_args.push_back("-M");
        gdial_args.push_back(m_modelName);
    }

    if (m_manufacturerName.empty())
    {
        if (!(envGetValue("MFG_NAME", m_manufacturerName)))
        {
            LOGERR("MFG_NAME not configured in device properties file");
        }
    }

    if (!m_manufacturerName.empty())
    {
        gdial_args.push_back("-R");
        gdial_args.push_back(m_manufacturerName);
    }

    if (m_defaultfriendlyName.empty())
    {
        m_defaultfriendlyName = m_modelName + "_" + m_manufacturerName;
    }

    if (!m_defaultfriendlyName.empty())
    {
        gdial_args.push_back("-F");
        gdial_args.push_back(m_defaultfriendlyName);
    }

    if (!m_defaultAppList.empty())
    {
        gdial_args.push_back("-A");
        gdial_args.push_back(m_defaultAppList);
    }

    if (isFriendlyNameEnabled) {
        gdial_args.push_back("--feature-friendlyname");
    }
    if (isWolWakeEnableEnabled && networkStandbyMode ) {
        gdial_args.push_back("--feature-wolwake");
    }

    if (nullptr == gdialCastObj)
    {
        gdialCastObj = gdialService::getInstance(this,gdial_args,"XCastOutofProcess");
    }

    if (nullptr != gdialCastObj)
    {
        returnValue = true;
        LOGINFO("gdialService::getInstance success[%p] ...",gdialCastObj);
    }
    LOGINFO("Exiting [%u] ...",returnValue);
    return returnValue;
}

void XCastManager::deinitialize()
{
    LOGINFO("Destroying gdialService instance");
    lock_guard<recursive_mutex> lock(m_mutexSync);
    if (nullptr != gdialCastObj)
    {
        gdialService::destroyInstance();
        gdialCastObj = nullptr;
    }
}

void XCastManager::shutdown()
{
    LOGINFO("Shutting down XCastManager");
    deinitialize();
    if(XCastManager::_instance != nullptr)
    {
        delete XCastManager::_instance;
        XCastManager::_instance = nullptr;
    }
}

std::string XCastManager::getReceiverID(WPEFramework::PluginHost::IShell* pluginService)
{
    std::ifstream file("/tmp/gpid.txt");
    std::string line, gpidValue, receiverId = "";

    if (file.is_open())
    {
        while (std::getline(file, line))
        {
            std::size_t pos = line.find("deviceId");
            if (pos != std::string::npos)
            {
                std::size_t colonPos = line.find(":", pos);
                if (colonPos != std::string::npos)
                {
                    gpidValue = line.substr(colonPos + 1);
                    // Remove spaces and unwanted characters
                    gpidValue.erase(std::remove_if(gpidValue.begin(), gpidValue.end(), ::isspace), gpidValue.end());
                    gpidValue.erase(std::remove(gpidValue.begin(), gpidValue.end(), '{'), gpidValue.end());
                    gpidValue.erase(std::remove(gpidValue.begin(), gpidValue.end(), '}'), gpidValue.end());
                    gpidValue.erase(std::remove(gpidValue.begin(), gpidValue.end(), ','), gpidValue.end());
                    gpidValue.erase(std::remove(gpidValue.begin(), gpidValue.end(), '/'), gpidValue.end());
                    gpidValue.erase(std::remove(gpidValue.begin(), gpidValue.end(), '"'), gpidValue.end());
                }
                break;
            }
        }
        // Convert to lowercase
        std::transform(gpidValue.begin(), gpidValue.end(), gpidValue.begin(), ::tolower);
        receiverId = std::move(gpidValue);
    }

    if (receiverId.empty())
    {
        std::ifstream authService_deviceId("/opt/www/authService/deviceid.dat");
        std::ifstream whitebox_deviceId("/opt/www/whitebox/wbdevice.dat");
        if (authService_deviceId)
        {
            std::getline(authService_deviceId, receiverId);
        }
        else if (whitebox_deviceId)
        {
            std::getline(whitebox_deviceId, receiverId);
        }
    }

    if (receiverId.empty())
    {
        LOGINFO("ReceiverID not found, attempting to generate UUID from serial number");

        static std::string cachedGeneratedUUID;

        if (!cachedGeneratedUUID.empty()) {
            LOGINFO("Using cached generated UUID: %s", cachedGeneratedUUID.c_str());
            receiverId = cachedGeneratedUUID;
        } else if (pluginService != nullptr) {
            // Generate UUID from serial number
            std::string serialNumber;
            if (getSerialNumberFromDeviceInfo(pluginService, serialNumber)) {
                receiverId = generateUUIDv5FromSerialNumber(serialNumber);
                if (!receiverId.empty()) {
                    // Cache the generated UUID since serial number is constant
                    cachedGeneratedUUID = receiverId;
                    LOGINFO("Generated and cached UUID from serial number: %s", cachedGeneratedUUID.c_str());
                } else {
                    LOGERR("Failed to generate UUID from serial number");
                }
            } else {
                LOGERR("Failed to retrieve serial number from DeviceInfo plugin");
            }
        } else {
            LOGINFO("Plugin service not available, cannot generate UUID from serial number");
        }
    }
    return receiverId;
}

void XCastManager::getWiFiInterface(std::string& WiFiInterfaceName)
{
    std::string buildType;
    std::ifstream file_stream("/opt/wifi_interface");

    envGetValue("BUILD_TYPE", buildType);
    if (file_stream && "prod" != buildType)
    {
        std::getline(file_stream, WiFiInterfaceName);
    }
    else
    {
        envGetValue("WIFI_INTERFACE", WiFiInterfaceName);
    }
    if (WiFiInterfaceName.empty())
    {
        WiFiInterfaceName = "wlan0";
    }
}

void XCastManager::getGDialInterfaceName(std::string& interfaceName)
{
    std::ifstream file_stream("/tmp/wifi-on");
    if (file_stream)
    {
        getWiFiInterface(interfaceName);
    }
    else
    {
        envGetValue("MOCA_INTERFACE", interfaceName);
        if (interfaceName.empty())
        {
            interfaceName = "eth1";
        }
    }
}

bool XCastManager::envGetValue(const char *key, std::string &value)
{
    std::ifstream fs(COMMON_DEVICE_PROPERTIES_FILE, std::ifstream::in);
    std::string::size_type delimpos;
    std::string line;
    bool returnValue = false;
    value = "";
    if (!fs.fail())
    {
        while (std::getline(fs, line))
        {
            if (!line.empty() && ((delimpos = line.find('=')) > 0))
            {
                std::string itemKey = line.substr(0, delimpos);
                if (itemKey == key)
                {
                    value = line.substr(delimpos + 1, std::string::npos);
                    returnValue = true;
                    break;
                }
            }
        }
    }
    return returnValue;
}

int XCastManager::applicationStateChanged( const string& app, const string& state, const string& id, const string& error)
{
    int status = 0;
    LOGINFO("AppName[%s] AppState[%s] AppID[%s] Error[%s]", app.c_str(), id.c_str() , state.c_str() , error.c_str());
    lock_guard<recursive_mutex> lock(m_mutexSync);
    if (gdialCastObj != NULL)
    {
        gdialCastObj->ApplicationStateChanged( app, state, id, error);
        status = 1;
    }
    else
        LOGINFO(" gdialCastObj is NULL ");
    return status;
}//app && state not empty

void XCastManager::enableCastService(const string& friendlyname,bool enableService)
{
    LOGINFO("friendlyname[%s] enableService[%d]", friendlyname.c_str(), enableService);
    lock_guard<recursive_mutex> lock(m_mutexSync);
    if(gdialCastObj != NULL)
    {
        std::string activation = enableService ? "true": "false";
        gdialCastObj->ActivationChanged( std::move(activation), friendlyname);
        LOGINFO("XcastService send onActivationChanged");
    }
    else
        LOGINFO(" gdialCastObj is NULL ");
}

string XCastManager::getProtocolVersion(void)
{
    LOGINFO("Entering ...");
    std::string strVersion;
    lock_guard<recursive_mutex> lock(m_mutexSync);
    if(gdialCastObj != NULL)
    {
        strVersion = gdialCastObj->getProtocolVersion();
        LOGINFO("XcastService getProtocolVersion version:%s ",strVersion.c_str());
    }
    else
    {
        LOGINFO(" XcastService getProtocolVersion gdialCastObj is NULL so returns 2.1");
	    strVersion = "2.1";
    }
    LOGINFO("Exiting ...");
    return strVersion;
}

int XCastManager::setManufacturerName( string manufacturer)
{
    int status = 0;
    LOGINFO("Manufacturer[%s]", manufacturer.c_str());
    lock_guard<recursive_mutex> lock(m_mutexSync);
    if(gdialCastObj != NULL)
    {
        gdialCastObj->setManufacturerName( manufacturer );
        status = 1;
    }
    else
    {
        LOGINFO(" gdialCastObj is NULL ");
    }
    m_manufacturerName = std::move(manufacturer);
    return status;
}

string XCastManager::getManufacturerName(void)
{
    lock_guard<recursive_mutex> lock(m_mutexSync);
    LOGINFO("ManufacturerName[%s]",m_manufacturerName.c_str());
    return m_manufacturerName;
}

int XCastManager::setModelName( string model)
{
    int status = 0;
    lock_guard<recursive_mutex> lock(m_mutexSync);
    LOGINFO("Model[%s]", model.c_str());
    if(gdialCastObj != NULL)
    {
        gdialCastObj->setModelName(model);
        status = 1;
    }
    else
    {
        LOGINFO(" gdialCastObj is NULL ");
    }
    m_modelName = std::move(model);
    return status;
}

string XCastManager::getModelName(void)
{
    lock_guard<recursive_mutex> lock(m_mutexSync);
    LOGINFO("ModelName[%s]",m_modelName.c_str());
    return m_modelName;
}

void XCastManager::registerApplications(std::vector<DynamicAppConfig*>& appConfigList)
{
    LOGINFO("Entering ...");

    RegisterAppEntryList *appReqList = new RegisterAppEntryList;

    for (DynamicAppConfig* pDynamicAppConfig : appConfigList)
    {
        RegisterAppEntry* appReq = new RegisterAppEntry;

        appReq->Names = pDynamicAppConfig->appName;
        appReq->prefixes = pDynamicAppConfig->prefixes;
        appReq->cors = pDynamicAppConfig->cors;
        appReq->allowStop = pDynamicAppConfig->allowStop;

        appReqList->pushBack(appReq);
    }
    lock_guard<recursive_mutex> lock(m_mutexSync);
    if(gdialCastObj != NULL)
    {
        gdialCastObj->RegisterApplications(appReqList);
        LOGINFO("XcastService send onRegisterAppEntryList");
    }
    else
    {
        LOGINFO(" gdialCastObj is NULL ");
        if (nullptr != appReqList)
        {
            LOGINFO("[%p] Freeing appConfigList",appReqList);
            delete appReqList;
            appReqList = nullptr;
        }
    }
    LOGINFO("Exiting ...");
}

void XCastManager::setNetworkStandbyMode(bool nwStandbymode)
{
    lock_guard<recursive_mutex> lock(m_mutexSync);
    LOGINFO("nwStandbymode:%u",nwStandbymode);
    if(gdialCastObj != NULL)
    {
        gdialCastObj->setNetworkStandbyMode(nwStandbymode);
    }
    else
    {
        LOGINFO("gdialCastObj is NULL");
    }
}

XCastManager * XCastManager::getInstance()
{
    LOGINFO("Entering ...");
    if(XCastManager::_instance == nullptr)
    {
        XCastManager::_instance = new XCastManager();
    }
    LOGINFO("Exiting ...");
    return XCastManager::_instance;
}

bool XCastManager::getSerialNumberFromDeviceInfo(WPEFramework::PluginHost::IShell* pluginService, std::string& serialNumber)
{
    if (pluginService == nullptr) {
        LOGERR("Plugin service is null; cannot call DeviceInfo.");
        return false;
    }

    auto deviceInfoPlugin = pluginService->QueryInterfaceByCallsign<WPEFramework::Exchange::IDeviceInfo>("DeviceInfo");
    if (deviceInfoPlugin == nullptr) {
        LOGERR("DeviceInfo plugin is not available or not activated");
        return false;
    }

    WPEFramework::Exchange::IDeviceInfo::DeviceSerialNo deviceSerialNumber;
    Core::hresult result = deviceInfoPlugin->SerialNumber(deviceSerialNumber);

    if (result == Core::ERROR_NONE && !deviceSerialNumber.serialnumber.empty()) {
        serialNumber = deviceSerialNumber.serialnumber;
        deviceInfoPlugin->Release();
        return true;
    }

    LOGERR("get DeviceInfo.SerialNumber failed, error code: %u, length: %zu", result, deviceSerialNumber.serialnumber.length());
    deviceInfoPlugin->Release();

    return false;
}

std::string XCastManager::generateUUIDv5FromSerialNumber(const std::string& serialNumber)
{
    LOGINFO("Generating UUID v5 from serial number...");

    if (serialNumber.empty()) {
        LOGERR("Serial number is empty, cannot generate UUID");
        return "";
    }

    // UUID v5 uses SHA-1 hashing
    // DNS namespace UUID: 6ba7b810-9dad-11d1-80b4-00c04fd430c8
    // Ref: https://datatracker.ietf.org/doc/html/rfc4122
    const uint8_t namespaceDNS[16] = {
        0x6b, 0xa7, 0xb8, 0x10, 0x9d, 0xad, 0x11, 0xd1,
        0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8
    };

    // Concatenate namespace UUID and serial number for hashing
    std::vector<uint8_t> data(namespaceDNS, namespaceDNS + 16);
    data.insert(data.end(), serialNumber.begin(), serialNumber.end());

    // Compute SHA-1 hash and copy immediately to ensure data is preserved
    Crypto::SHA1 sha1;
    sha1.Input(data.data(), data.size());
    const uint8_t* hashResult = sha1.Result();

    if (hashResult == nullptr) {
        LOGERR("SHA-1 hash computation failed");
        return "";
    }

    // Copy hash data before sha1 object goes out of scope
    uint8_t uuidBytes[16];
    memcpy(uuidBytes, hashResult, 16);

    // Set version (5) and variant bits according to RFC 4122
    uuidBytes[6] = (uuidBytes[6] & 0x0F) | 0x50;  // Version 5
    uuidBytes[8] = (uuidBytes[8] & 0x3F) | 0x80;  // Variant bits

    /**
     * @brief Helper lambda to format a range of bytes as hexadecimal into an output stream.
     * It formats each byte in the specified range [start, end) as a 2-digit hexadecimal value
     * and appends it to the provided ostringstream.
     *
     * @param stream The output string stream to write formatted bytes to (passed by reference)
     * @param bytes Pointer to the byte array to be formatted
     * @param start Starting index (inclusive) of the byte range to format
     * @param end Ending index (exclusive) of the byte range to format
     *
     * @note Defined as a lambda rather than a separate function to keep the formatting logic
     *       close to its single use case and to avoid polluting the namespace with a small
     *       utility function that is not needed elsewhere.
     */
    auto formatBytes = [](std::ostringstream& stream, const uint8_t* bytes, int start, int end) {
        for (int i = start; i < end; i++) {
            stream << std::setw(2) << static_cast<unsigned int>(bytes[i]);
        }
    };

    // Format as UUID string: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx
    std::ostringstream uuidStream;
    uuidStream << std::hex << std::setfill('0');

    formatBytes(uuidStream, uuidBytes, 0, 4);
    uuidStream << "-";
    formatBytes(uuidStream, uuidBytes, 4, 6);
    uuidStream << "-";
    formatBytes(uuidStream, uuidBytes, 6, 8);
    uuidStream << "-";
    formatBytes(uuidStream, uuidBytes, 8, 10);
    uuidStream << "-";
    formatBytes(uuidStream, uuidBytes, 10, 16);

    std::string uuid = uuidStream.str();
    LOGINFO("Generated UUID v5: %s", uuid.c_str());

    return uuid;
}
