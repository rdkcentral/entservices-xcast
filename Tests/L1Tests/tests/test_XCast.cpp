/**
* If not stated otherwise in this file or this component's LICENSE
* file the following copyright and licenses apply:
*
* Copyright 2022 RDK Management
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

#include <gtest/gtest.h>

#include "XCast.h"

#include "DispatcherMock.h"
#include "FactoriesImplementation.h"
#include "ServiceMock.h"
#include "ThunderPortability.h"

#include <iostream>
#include <string>
#include <vector>
#include <cstdio>
#include "COMLinkMock.h"
#include "WrapsMock.h"
#include "RfcApiMock.h"
#include "gdialserviceMock.h"
#include "NetworkManagerMock.h"
#include "PowerManagerMock.h"
#include "WorkerPoolImplementation.h"
#include "XCastImplementation.h"
#include <sys/time.h>
#include <future>
#include <thread>

// utils
#include "WaitGroup.h"

using namespace WPEFramework;

using ::testing::NiceMock;

#define TEST_LOG(x, ...) fprintf(stderr, "\033[1;32m[%s:%d](%s)<PID:%d><TID:%d>" x "\n\033[0m", __FILE__, __LINE__, __FUNCTION__, getpid(), gettid(), ##__VA_ARGS__); fflush(stderr);

class XCastTest : public ::testing::Test {
protected:
    Core::ProxyType<Plugin::XCast> plugin;
    Core::JSONRPC::Handler& mJsonRpcHandler;
    DECL_CORE_JSONRPC_CONX connection;
    Core::JSONRPC::Message message;
    string response;
    ServiceMock* mServiceMock = nullptr;
    WrapsImplMock   *p_wrapsImplMock = nullptr;
    RfcApiImplMock  *p_rfcApiImplMock = nullptr;
    gdialServiceImplMock *p_gdialserviceImplMock = nullptr;
    MockINetworkManager* mockNetworkManager = nullptr;
    PLUGINHOST_DISPATCHER* dispatcher = nullptr;
    Exchange::IPowerManager::INetworkStandbyModeChangedNotification* _networkStandbyModeChangedNotification = nullptr;
    Exchange::IPowerManager::IModeChangedNotification* _modeChangedNotification = nullptr;
    Exchange::IPowerManager::PowerState _powerState = Exchange::IPowerManager::POWER_STATE_OFF;
    Exchange::INetworkManager::INotification* _networkManagerNotification = nullptr;
    bool _networkStandbyMode = false;
    Core::ProxyType<Plugin::XCastImplementation> xcastImpl;
    NiceMock<COMLinkMock> comLinkMock;
    Core::ProxyType<WorkerPoolImplementation> workerPool;
    NiceMock<FactoriesImplementation> factoriesImplementation;

    Core::hresult createResources()
    {
        Core::hresult status = Core::ERROR_GENERAL;

        p_wrapsImplMock = new NiceMock<WrapsImplMock>;
        printf("Pass created wrapsImplMock: %p ", p_wrapsImplMock);
        Wraps::setImpl(p_wrapsImplMock);

        p_rfcApiImplMock  = new NiceMock <RfcApiImplMock>;
        printf("Pass created RfcApiImplMock: %p ", p_rfcApiImplMock);
        RfcApi::setImpl(p_rfcApiImplMock);

        p_gdialserviceImplMock  = new NiceMock<gdialServiceImplMock>;
        printf("Pass created gdialServiceImplMock: %p ", p_gdialserviceImplMock);
        gdialService::setImpl(p_gdialserviceImplMock);

        mServiceMock = new NiceMock<ServiceMock>;
        mockNetworkManager = new MockINetworkManager();

        PluginHost::IFactories::Assign(&factoriesImplementation);
        dispatcher = static_cast<PLUGINHOST_DISPATCHER*>(
        plugin->QueryInterface(PLUGINHOST_DISPATCHER_ID));
        dispatcher->Activate(mServiceMock);
        TEST_LOG("In createResources!");

        EXPECT_CALL(*mServiceMock, QueryInterfaceByCallsign(::testing::_, ::testing::_))
          .Times(::testing::AnyNumber())
          .WillRepeatedly(::testing::Invoke(
              [&](const uint32_t id, const std::string& name) -> void* {
                if (name == "org.rdk.NetworkManager") {
                    return static_cast<void*>(mockNetworkManager);
                }
            TEST_LOG("callsign[%s]",name.c_str());
            return nullptr;
        }));

        EXPECT_CALL(*mServiceMock, COMLink())
            .Times(::testing::AnyNumber())
            .WillRepeatedly(::testing::Invoke(
            [this]() {
                    TEST_LOG("Pass created comLinkMock: %p ", &comLinkMock);
                    return &comLinkMock;
                }));

        #ifdef USE_THUNDER_R4
            EXPECT_CALL(comLinkMock, Instantiate(::testing::_, ::testing::_, ::testing::_))
                .Times(::testing::AnyNumber())
                .WillRepeatedly(::testing::Invoke(
                        [&](const RPC::Object& object, const uint32_t waitTime, uint32_t& connectionId) {
                            xcastImpl = Core::ProxyType<Plugin::XCastImplementation>::Create();
                            TEST_LOG("Pass created xcastImpl: %p ", &xcastImpl);
                            return &xcastImpl;
                    }));
        #else
            EXPECT_CALL(comLinkMock, Instantiate(::testing::_, ::testing::_, ::testing::_, ::testing::_, ::testing::_))
                .Times(::testing::AnyNumber())
                .WillRepeatedly(::testing::Return(xcastImpl));
        #endif /*USE_THUNDER_R4 */

        EXPECT_CALL(*p_rfcApiImplMock, getRFCParameter(::testing::_, ::testing::_, ::testing::_))
            .Times(::testing::AnyNumber())
            .WillRepeatedly(::testing::Invoke(
                [](char* pcCallerID, const char* pcParameterName, RFC_ParamData_t* pstParamData) {
                    WDMP_STATUS wdmpStatus = WDMP_SUCCESS;
                    EXPECT_EQ(string(pcCallerID), string("XCastPlugin"));
                    if (string(pcParameterName) == "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.XDial.AppList") {
                        strncpy(pstParamData->value, "youtube::netflix", sizeof(pstParamData->value));
                        pstParamData->type = WDMP_STRING;
                    }
                    else if ((string(pcParameterName) == "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.XDial.FriendlyNameEnable")||
                            (string(pcParameterName) == "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.XDial.WolWakeEnable")||
                            (string(pcParameterName) == "Device.DeviceInfo.X_RDKCENTRAL-COM_RFC.Feature.XDial.DynamicAppList")) {
                        strncpy(pstParamData->value, "true", sizeof(pstParamData->value));
                        pstParamData->type = WDMP_BOOLEAN;
                    }
                    else {
                        EXPECT_EQ(string(pcParameterName), string("RFC mocks required for this parameter"));
                        wdmpStatus = WDMP_ERR_INVALID_PARAMETER_NAME;
                    }
                    return wdmpStatus;
                }));

        EXPECT_CALL(*mockNetworkManager, GetPrimaryInterface(::testing::_))
            .Times(::testing::AnyNumber())
            .WillRepeatedly(::testing::Invoke([&](string& interface) -> uint32_t {
                    interface = "eth0";
                    return Core::ERROR_NONE;
            }));

        EXPECT_CALL(*mockNetworkManager, GetIPSettings(::testing::_, ::testing::_, ::testing::_))
            .Times(::testing::AnyNumber())
            .WillRepeatedly(::testing::Invoke(
                [&](string&, const string&, WPEFramework::Exchange::INetworkManager::IPAddress& address) -> uint32_t {
                    address.primarydns = "75.75.75.76";
                    address.secondarydns = "75.75.76.76";
                    address.dhcpserver = "192.168.0.1";
                    address.gateway = "192.168.0.1";
                    address.ipaddress = "192.168.0.11";
                    address.ipversion = "IPv4";
                    address.prefix = 24;
                    return Core::ERROR_NONE;
            }));

        EXPECT_CALL(*mockNetworkManager, Register(::testing::_))
            .Times(::testing::AnyNumber())
            .WillRepeatedly(::testing::Invoke(
                [&](WPEFramework::Exchange::INetworkManager::INotification* notification) -> uint32_t {
                    _networkManagerNotification = notification;
                    return Core::ERROR_NONE;
                }));

        EXPECT_EQ(string(""), plugin->Initialize(mServiceMock));
        TEST_LOG("createResources - All done!");
        status = Core::ERROR_NONE;

        return status;
    }

    void releaseResources()
    {
        TEST_LOG("In releaseResources!");

        Wraps::setImpl(nullptr);
        if (p_wrapsImplMock != nullptr)
        {
            delete p_wrapsImplMock;
            p_wrapsImplMock = nullptr;
        }
        RfcApi::setImpl(nullptr);
        if (p_rfcApiImplMock != nullptr)
        {
            delete p_rfcApiImplMock;
            p_rfcApiImplMock = nullptr;
        }
        gdialService::setImpl(nullptr);
        if (p_gdialserviceImplMock != nullptr)
        {
            delete p_gdialserviceImplMock;
            p_gdialserviceImplMock = nullptr;
        }
        PluginHost::IFactories::Assign(nullptr);
        IarmBus::setImpl(nullptr);

        dispatcher->Deactivate();
        dispatcher->Release();

        plugin->Deinitialize(mServiceMock);
        delete mockNetworkManager;
        delete mServiceMock;
    }

    XCastTest()
        : plugin(Core::ProxyType<Plugin::XCast>::Create())
        , mJsonRpcHandler(*(plugin))
        , INIT_CONX(1, 0)
        , workerPool(Core::ProxyType<WorkerPoolImplementation>::Create(2, Core::Thread::DefaultStackSize(), 16))
    {
        PluginHost::IFactories::Assign(&factoriesImplementation);
        Core::IWorkerPool::Assign(&(*workerPool));
        workerPool->Run();
    }

    virtual ~XCastTest() override
    {
        TEST_LOG("XCastTest Destructor");

        Core::IWorkerPool::Assign(nullptr);
        workerPool.Release();

        Wraps::setImpl(nullptr);
        if (p_wrapsImplMock != nullptr)
        {
            delete p_wrapsImplMock;
            p_wrapsImplMock = nullptr;
        }
        RfcApi::setImpl(nullptr);
        if (p_rfcApiImplMock != nullptr)
        {
            delete p_rfcApiImplMock;
            p_rfcApiImplMock = nullptr;
        }
        gdialService::setImpl(nullptr);
        if (p_gdialserviceImplMock != nullptr)
        {
            delete p_gdialserviceImplMock;
            p_gdialserviceImplMock = nullptr;
        }
        PluginHost::IFactories::Assign(nullptr);
        IarmBus::setImpl(nullptr);
    }
};

TEST_F(XCastTest, GetInformation)
{
    EXPECT_EQ("This XCast Plugin facilitates to persist event data for monitoring applications", plugin->Information());
}

TEST_F(XCastTest, RegisteredMethods)
{
    Core::hresult status = createResources();

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Exists(_T("setApplicationState")));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Exists(_T("setEnabled")));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Exists(_T("getEnabled")));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Exists(_T("getStandbyBehavior")));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Exists(_T("setStandbyBehavior")));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Exists(_T("getFriendlyName")));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Exists(_T("setFriendlyName")));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Exists(_T("getProtocolVersion")));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Exists(_T("unregisterApplications")));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Exists(_T("getProtocolVersion")));

    if (Core::ERROR_NONE == status)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, getsetFriendlyName)
{
    Core::hresult status = createResources();

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("setFriendlyName"), _T("{\"friendlyname\": \"friendlyTest\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("getFriendlyName"), _T("{}"), response));
    EXPECT_EQ(response, string("{\"friendlyname\":\"friendlyTest\",\"success\":true}"));

    if (Core::ERROR_NONE == status)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, getsetStandbyBehavoir)
{
    Core::hresult status = createResources();

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("getStandbyBehavior"), _T("{}"), response));
    EXPECT_EQ(response, string("{\"standbybehavior\":\"inactive\",\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("setStandbyBehavior"), _T("{\"standbybehavior\": \"active\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("getStandbyBehavior"), _T("{}"), response));
    EXPECT_EQ(response, string("{\"standbybehavior\":\"active\",\"success\":true}"));

    if (Core::ERROR_NONE == status)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, getsetManufacturerName)
{
    Core::hresult status = createResources();

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("setManufacturerName"), _T("{\"manufacturer\": \"manufacturerTest\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("getManufacturerName"), _T("{}"), response));
    EXPECT_EQ(response, string("{\"manufacturer\":\"manufacturerTest\",\"success\":true}"));

    if (Core::ERROR_NONE == status)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, getsetModelName)
{
    Core::hresult status = createResources();

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("setModelName"), _T("{\"model\": \"modelTest\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("getModelName"), _T("{}"), response));
    EXPECT_EQ(response, string("{\"model\":\"modelTest\",\"success\":true}"));

    if (Core::ERROR_NONE == status)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, setApplicationState)
{
    Core::hresult status = createResources();

    EXPECT_CALL(*p_gdialserviceImplMock, ApplicationStateChanged(::testing::_, ::testing::_, ::testing::_, ::testing::_))
        .Times(5)
        .WillOnce(::testing::Invoke(
            [](string applicationName, string appState, string applicationId, string error) {
                EXPECT_EQ(applicationName, string("NetflixApp"));
                EXPECT_EQ(appState, string("running"));
                EXPECT_EQ(applicationId, string("1234"));
                EXPECT_EQ(error, string("none"));
                return GDIAL_SERVICE_ERROR_NONE;
            }))
        .WillOnce(::testing::Invoke(
            [](string applicationName, string appState, string applicationId, string error) {
                EXPECT_EQ(applicationName, string("NetflixApp"));
                EXPECT_EQ(appState, string("stopped"));
                EXPECT_EQ(applicationId, string("1234"));
                EXPECT_EQ(error, string("forbidden"));
                return GDIAL_SERVICE_ERROR_NONE;
            }))
        .WillOnce(::testing::Invoke(
            [](string applicationName, string appState, string applicationId, string error) {
                EXPECT_EQ(applicationName, string("NetflixApp"));
                EXPECT_EQ(appState, string("suspended"));
                EXPECT_EQ(applicationId, string("1234"));
                EXPECT_EQ(error, string("unavailable"));
                return GDIAL_SERVICE_ERROR_NONE;
            }))
        .WillOnce(::testing::Invoke(
            [](string applicationName, string appState, string applicationId, string error) {
                EXPECT_EQ(applicationName, string("NetflixApp"));
                EXPECT_EQ(appState, string("stopped"));
                EXPECT_EQ(applicationId, string("1234"));
                EXPECT_EQ(error, string("invalid"));
                return GDIAL_SERVICE_ERROR_NONE;
            }))
        .WillOnce(::testing::Invoke(
            [](string applicationName, string appState, string applicationId, string error) {
                EXPECT_EQ(applicationName, string("NetflixApp"));
                EXPECT_EQ(appState, string("stopped"));
                EXPECT_EQ(applicationId, string("1234"));
                EXPECT_EQ(error, string("internal"));
                return GDIAL_SERVICE_ERROR_NONE;
            }));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("setApplicationState"), _T("{\"applicationName\": \"NetflixApp\", \"state\":\"running\", \"applicationId\": \"1234\", \"error\": \"none\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("setApplicationState"), _T("{\"applicationName\": \"NetflixApp\", \"state\":\"stopped\", \"applicationId\": \"1234\", \"error\": \"forbidden\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("setApplicationState"), _T("{\"applicationName\": \"NetflixApp\", \"state\":\"suspended\", \"applicationId\": \"1234\", \"error\": \"unavailable\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("setApplicationState"), _T("{\"applicationName\": \"NetflixApp\", \"state\":\"stopped\", \"applicationId\": \"1234\", \"error\": \"invalid\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("setApplicationState"), _T("{\"applicationName\": \"NetflixApp\", \"state\":\"stopped\", \"applicationId\": \"1234\", \"error\": \"internal\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    if (Core::ERROR_NONE == status)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, getProtocolVersion)
{
    Core::hresult status = createResources();

    EXPECT_CALL(*p_gdialserviceImplMock, getProtocolVersion())
            .WillOnce(::testing::Invoke(
                []() {
                    return std::string("test");
                }));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("getProtocolVersion"), _T("{}"), response));
    EXPECT_EQ(response, string("{\"version\":\"test\",\"success\":true}"));

    if (Core::ERROR_NONE == status)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, unRegisterAllApplications)
{
    Core::hresult status = createResources();
    WaitGroup wg;
    wg.Add();

    EXPECT_CALL(*p_gdialserviceImplMock, RegisterApplications(::testing::_))
        .Times(3)
        .WillOnce(::testing::Invoke([](RegisterAppEntryList* appConfigList)
            {
                int i = 0;
                if (nullptr == appConfigList ) {
                    TEST_LOG("appConfigList is NULL");
                    return GDIAL_SERVICE_INVALID_PARAM_ERROR;
                }
                for (RegisterAppEntry* appEntry : appConfigList->getValues())
                {
                    TEST_LOG("Current Index: %d", i);
                    TEST_LOG("Names[%s]Prefix[%s]Cors[%s]AllowStop[%d]",appEntry->Names.c_str(),appEntry->prefixes.c_str(),appEntry->cors.c_str(),appEntry->allowStop);
                    if (0 == i)
                    {
                        EXPECT_EQ(appEntry->Names, string("Youtube"));
                        EXPECT_EQ(appEntry->prefixes, string("myYouTube"));
                        EXPECT_EQ(appEntry->cors, string(".youtube.com"));
                        EXPECT_EQ(appEntry->allowStop, 1 );
                    }
                    else if (1 == i)
                    {
                        EXPECT_EQ(appEntry->Names, string("Netflix"));
                        EXPECT_EQ(appEntry->prefixes, string("myNetflix"));
                        EXPECT_EQ(appEntry->cors, string(".netflix.com"));
                        EXPECT_EQ(appEntry->allowStop, 0 );
                    }
                    ++i;
                }
                return GDIAL_SERVICE_ERROR_NONE;
            }))
        .WillOnce(::testing::Invoke([](RegisterAppEntryList* appConfigList)
            {
                int i = 0;
                if (nullptr == appConfigList ) {
                    TEST_LOG("appConfigList is NULL");
                    return GDIAL_SERVICE_INVALID_PARAM_ERROR;
                }
                for (RegisterAppEntry* appEntry : appConfigList->getValues())
                {
                    TEST_LOG("Current Index: %d", i);
                    TEST_LOG("Names[%s]Prefix[%s]Cors[%s]AllowStop[%d]",appEntry->Names.c_str(),appEntry->prefixes.c_str(),appEntry->cors.c_str(),appEntry->allowStop);
                    if (0 == i)
                    {
                        EXPECT_EQ(appEntry->Names, string("Netflix"));
                        EXPECT_EQ(appEntry->prefixes, string("myNetflix"));
                        EXPECT_EQ(appEntry->cors, string(".netflix.com"));
                        EXPECT_EQ(appEntry->allowStop, 0);
                    }
                    ++i;
                }
                return GDIAL_SERVICE_ERROR_NONE;
            }))
        .WillOnce(::testing::Invoke([](RegisterAppEntryList* appConfigList)
            {
                int i = 0;
                if (nullptr == appConfigList ) {
                    TEST_LOG("appConfigList is NULL");
                    return GDIAL_SERVICE_INVALID_PARAM_ERROR;
                }
                for (RegisterAppEntry* appEntry : appConfigList->getValues())
                {
                    TEST_LOG("Current Index: %d", i);
                    TEST_LOG("Names[%s]Prefix[%s]Cors[%s]AllowStop[%d]",appEntry->Names.c_str(),appEntry->prefixes.c_str(),appEntry->cors.c_str(),appEntry->allowStop);
                    if (0 == i)
                    {
                        EXPECT_EQ(appEntry->Names, string("Netflix"));
                        EXPECT_EQ(appEntry->prefixes, string("myNetflix"));
                        EXPECT_EQ(appEntry->cors, string(".netflix.com"));
                        EXPECT_EQ(appEntry->allowStop, 0);
                    }
                    ++i;
                }
                return GDIAL_SERVICE_ERROR_NONE;
            }));

    EXPECT_CALL(*p_gdialserviceImplMock, ActivationChanged(::testing::_,::testing::_))
        .Times(3)
        .WillOnce(::testing::Invoke(
            [&](std::string activation, std::string friendlyname) {
                return GDIAL_SERVICE_ERROR_NONE;
            }))
        .WillOnce(::testing::Invoke(
            [&](std::string activation, std::string friendlyname) {
                return GDIAL_SERVICE_ERROR_NONE;
            }))
        .WillOnce(::testing::Invoke(
            [&](std::string activation, std::string friendlyname) {
                wg.Done();
                return GDIAL_SERVICE_ERROR_NONE;
            }));

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("registerApplications"), _T("{\"applications\": [{\"name\": \"Youtube\",\"prefix\": \"myYouTube\",\"cors\": \".youtube.com\",\"query\": \"source_type=12\",\"payload\": \"youtube_payload\",\"allowStop\": 1 },{\"name\": \"Netflix\",\"prefix\": \"myNetflix\",\"cors\": \".netflix.com\",\"query\": \"source_type=12\",\"payload\": \"netflix_payload\",\"allowStop\": 0}]}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("unregisterApplications"), _T("{\"applications\": [\"Youtube\"]}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    // Added to trigger registerApplication handling based on timer
    ASSERT_NE(_networkManagerNotification, nullptr);
    _networkManagerNotification->onActiveInterfaceChange("eth0", "wlan0");
    usleep(50);

    wg.Wait();

    if (Core::ERROR_NONE == status)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, onApplicationLaunchRequest)
{
    Core::hresult status = createResources();
    Core::Event onLaunchRequest(false, true);
    Core::Event onLaunchRequestParam(false, true);

    EXPECT_CALL(*mServiceMock, Submit(::testing::_, ::testing::_))
        .Times(2)
        .WillOnce(::testing::Invoke(
            [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text, string(_T("{\"jsonrpc\":\"2.0\",\"method\":\"client.events.onApplicationLaunchRequest\",\"params\":{\"applicationName\":\"Youtube\",\"parameter\":\"http:\\/\\/youtube.com?myYouTube\"}}")));
                TEST_LOG("LaunchRequest event received");
                onLaunchRequest.SetEvent();
                return Core::ERROR_NONE;
            }))
        .WillOnce(::testing::Invoke(
            [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text, string(_T("{\"jsonrpc\":\"2.0\",\"method\":\"client.events.onApplicationLaunchRequest\",\"params\":{\"applicationName\":\"Youtube\",\"strPayLoad\":\"youtube_payload\",\"strQuery\":\"source_type=12\",\"strAddDataUrl\":\"http:\\/\\/youtube.com\"}}")));
                TEST_LOG("LaunchRequest with param event received");
                onLaunchRequestParam.SetEvent();
                return Core::ERROR_NONE;
            }));

    EVENT_SUBSCRIBE(0, _T("onApplicationLaunchRequest"), _T("client.events"), message);

    GDialNotifier* gdialNotifier = gdialService::getObserverHandle();
    ASSERT_NE(gdialNotifier, nullptr);

    gdialNotifier->onApplicationLaunchRequest("Youtube", "http://youtube.com?myYouTube");
    EXPECT_EQ(Core::ERROR_NONE, onLaunchRequest.Lock(5000));
    gdialNotifier->onApplicationLaunchRequestWithLaunchParam("Youtube", "youtube_payload", "source_type=12", "http://youtube.com");
    EXPECT_EQ(Core::ERROR_NONE, onLaunchRequestParam.Lock(5000));

    EVENT_UNSUBSCRIBE(0, _T("onApplicationLaunchRequest"), _T("client.events"), message);

    if (Core::ERROR_NONE == status)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, onApplicationStopRequest)
{
    Core::hresult status = createResources();
    Core::Event onStopRequest(false, true);

    EXPECT_CALL(*mServiceMock, Submit(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text, string(_T("{\"jsonrpc\":\"2.0\",\"method\":\"client.events.onApplicationStopRequest\",\"params\":{\"applicationName\":\"Youtube\",\"applicationId\":\"1234\"}}")));
                TEST_LOG("StopRequest event received");
                onStopRequest.SetEvent();
                return Core::ERROR_NONE;
            }));

    EVENT_SUBSCRIBE(0, _T("onApplicationStopRequest"), _T("client.events"), message);

    GDialNotifier* gdialNotifier = gdialService::getObserverHandle();
    ASSERT_NE(gdialNotifier, nullptr);
    gdialNotifier->onApplicationStopRequest("Youtube", "1234");
    EXPECT_EQ(Core::ERROR_NONE, onStopRequest.Lock(5000));

    EVENT_UNSUBSCRIBE(0, _T("onApplicationStopRequest"), _T("client.events"), message);

    if (Core::ERROR_NONE == status)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, onApplicationHideRequest)
{
    Core::hresult status = createResources();
    Core::Event onHideRequest(false, true);

    EXPECT_CALL(*mServiceMock, Submit(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text, string(_T("{\"jsonrpc\":\"2.0\",\"method\":\"client.events.onApplicationHideRequest\",\"params\":{\"applicationName\":\"Youtube\",\"applicationId\":\"1234\"}}")));
                TEST_LOG("HideRequest event received");
                onHideRequest.SetEvent();
                return Core::ERROR_NONE;
            }));

    EVENT_SUBSCRIBE(0, _T("onApplicationHideRequest"), _T("client.events"), message);

    GDialNotifier* gdialNotifier = gdialService::getObserverHandle();
    ASSERT_NE(gdialNotifier, nullptr);
    gdialNotifier->onApplicationHideRequest("Youtube", "1234");

    EXPECT_EQ(Core::ERROR_NONE, onHideRequest.Lock(5000));

    EVENT_UNSUBSCRIBE(0, _T("onApplicationHideRequest"), _T("client.events"), message);

    if (Core::ERROR_NONE == status)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, onApplicationResumeRequest)
{
    Core::hresult status = createResources();
    Core::Event onResumeRequest(false, true);

    EXPECT_CALL(*mServiceMock, Submit(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text, string(_T("{\"jsonrpc\":\"2.0\",\"method\":\"client.events.onApplicationResumeRequest\",\"params\":{\"applicationName\":\"Youtube\",\"applicationId\":\"1234\"}}")));
                TEST_LOG("ResumeRequest event received");
                onResumeRequest.SetEvent();
                return Core::ERROR_NONE;
            }));

    EVENT_SUBSCRIBE(0, _T("onApplicationResumeRequest"), _T("client.events"), message);

    GDialNotifier* gdialNotifier = gdialService::getObserverHandle();
    ASSERT_NE(gdialNotifier, nullptr);
    gdialNotifier->onApplicationResumeRequest("Youtube", "1234");

    EXPECT_EQ(Core::ERROR_NONE, onResumeRequest.Lock(5000));

    EVENT_UNSUBSCRIBE(0, _T("onApplicationResumeRequest"), _T("client.events"), message);

    if (Core::ERROR_NONE == status)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, onApplicationStateRequest)
{
    Core::hresult status = createResources();
    Core::Event onStateRequest(false, true);

    EXPECT_CALL(*mServiceMock, Submit(::testing::_, ::testing::_))
        .Times(1)
        .WillOnce(::testing::Invoke(
            [&](const uint32_t, const Core::ProxyType<Core::JSON::IElement>& json) {
                string text;
                EXPECT_TRUE(json->ToString(text));
                EXPECT_EQ(text, string(_T("{\"jsonrpc\":\"2.0\",\"method\":\"client.events.onApplicationStateRequest\",\"params\":{\"applicationName\":\"Netflix\",\"applicationId\":\"1234\"}}")));
                TEST_LOG("StateRequest event received");
                onStateRequest.SetEvent();
                return Core::ERROR_NONE;
            }));

    EVENT_SUBSCRIBE(0, _T("onApplicationStateRequest"), _T("client.events"), message);

    GDialNotifier* gdialNotifier = gdialService::getObserverHandle();
    ASSERT_NE(gdialNotifier, nullptr);
    gdialNotifier->onApplicationStateRequest("Netflix", "1234");

    EXPECT_EQ(Core::ERROR_NONE, onStateRequest.Lock(5000));

    EVENT_UNSUBSCRIBE(0, _T("onApplicationStateRequest"), _T("client.events"), message);

    if (Core::ERROR_NONE == status)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, updatePowerState)
{
    Core::hresult status = createResources();
    WaitGroup wg;
    wg.Add();

    EXPECT_CALL(PowerManagerMock::Mock(), SetPowerState(::testing::_, ::testing::_,::testing::_))
        .Times(4)
        .WillOnce(::testing::Invoke(
            [&](const int keyCode, const PowerState powerState, const string& reason) -> uint32_t {
                EXPECT_EQ(powerState, Exchange::IPowerManager::PowerState::POWER_STATE_STANDBY);
                _powerState = powerState;
                if (_modeChangedNotification) {
                    _modeChangedNotification->OnPowerModeChanged(Exchange::IPowerManager::PowerState::POWER_STATE_ON, powerState);
                }
                return Core::ERROR_NONE;
            }))
        .WillOnce(::testing::Invoke(
            [&](const int keyCode, const PowerState powerState, const string& reason) -> uint32_t {
                EXPECT_EQ(powerState, WPEFramework::Exchange::IPowerManager::POWER_STATE_ON);
                _powerState = powerState;
                if (_modeChangedNotification) {
                    _modeChangedNotification->OnPowerModeChanged(Exchange::IPowerManager::PowerState::POWER_STATE_STANDBY, powerState);
                }
                return Core::ERROR_NONE;
            }))
        .WillOnce(::testing::Invoke(
            [&](const int keyCode, const PowerState powerState, const string& reason) -> uint32_t {
                EXPECT_EQ(powerState, WPEFramework::Exchange::IPowerManager::POWER_STATE_STANDBY);
                _powerState = powerState;
                if (_modeChangedNotification) {
                    _modeChangedNotification->OnPowerModeChanged(Exchange::IPowerManager::PowerState::POWER_STATE_ON, powerState);
                }
                return Core::ERROR_NONE;
            }))
        .WillOnce(::testing::Invoke(
            [&](const int keyCode, const PowerState powerState, const string& reason) -> uint32_t {
                EXPECT_EQ(powerState, WPEFramework::Exchange::IPowerManager::POWER_STATE_ON);
                _powerState = powerState;
                if (_modeChangedNotification) {
                    _modeChangedNotification->OnPowerModeChanged(Exchange::IPowerManager::PowerState::POWER_STATE_STANDBY, powerState);
                }
                wg.Done();
                return Core::ERROR_NONE;
            }));

    EXPECT_CALL(PowerManagerMock::Mock(), GetPowerState(::testing::_, ::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Invoke(
            [&](PowerState& currentState, PowerState& previousState) -> uint32_t {
                currentState = _powerState;
                return Core::ERROR_NONE;
            }));

    EXPECT_CALL(PowerManagerMock::Mock(), GetNetworkStandbyMode(::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Invoke(
            [&](bool& mode) -> uint32_t {
                mode = _networkStandbyMode;
                return Core::ERROR_NONE;
            }));

    EXPECT_CALL(PowerManagerMock::Mock(), Register(::testing::Matcher<Exchange::IPowerManager::INetworkStandbyModeChangedNotification*>(::testing::_)))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Invoke(
            [&](Exchange::IPowerManager::INetworkStandbyModeChangedNotification* notification) -> uint32_t {
                _networkStandbyModeChangedNotification = notification;
                return Core::ERROR_NONE;
            }));

    EXPECT_CALL(PowerManagerMock::Mock(), Register(::testing::Matcher<Exchange::IPowerManager::IModeChangedNotification*>(::testing::_)))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Invoke(
            [&](Exchange::IPowerManager::IModeChangedNotification* notification) -> uint32_t {
                _modeChangedNotification = notification;
                return Core::ERROR_NONE;
            }));

    GDialNotifier* gdialNotifier = gdialService::getObserverHandle();
    ASSERT_NE(gdialNotifier, nullptr);

    _powerState = Exchange::IPowerManager::PowerState::POWER_STATE_ON;
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("setEnabled"), _T("{\"enabled\": true }"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    gdialNotifier->updatePowerState("STANDBY");
    gdialNotifier->updatePowerState("ON");
    gdialNotifier->updatePowerState("TOGGLE");
    gdialNotifier->updatePowerState("TOGGLE");

    wg.Wait();

    if (Core::ERROR_NONE == status)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, onPowerManagerEvents)
{
    Core::hresult status = createResources();
    WaitGroup wg;
    wg.Add();

    EXPECT_CALL(PowerManagerMock::Mock(), GetPowerState(::testing::_, ::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Invoke(
            [&](PowerState& currentState, PowerState& previousState) -> uint32_t {
                currentState = _powerState;
                return Core::ERROR_NONE;
            }));

    EXPECT_CALL(PowerManagerMock::Mock(), GetNetworkStandbyMode(::testing::_))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Invoke(
            [&](bool& mode) -> uint32_t {
                mode = _networkStandbyMode;
                return Core::ERROR_NONE;
            }));

    EXPECT_CALL(PowerManagerMock::Mock(), Register(::testing::Matcher<Exchange::IPowerManager::INetworkStandbyModeChangedNotification*>(::testing::_)))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Invoke(
            [&](Exchange::IPowerManager::INetworkStandbyModeChangedNotification* notification) -> uint32_t {
                _networkStandbyModeChangedNotification = notification;
                return Core::ERROR_NONE;
            }));

    EXPECT_CALL(PowerManagerMock::Mock(), Register(::testing::Matcher<Exchange::IPowerManager::IModeChangedNotification*>(::testing::_)))
        .Times(::testing::AnyNumber())
        .WillRepeatedly(::testing::Invoke(
            [&](Exchange::IPowerManager::IModeChangedNotification* notification) -> uint32_t {
                _modeChangedNotification = notification;
                return Core::ERROR_NONE;
            }));

    EXPECT_CALL(*p_gdialserviceImplMock, setNetworkStandbyMode(::testing::_))
        .Times(2)
        .WillOnce(::testing::Invoke(
            [&](bool nwStandbyMode) {
                EXPECT_EQ(nwStandbyMode, true);
                _networkStandbyMode = nwStandbyMode;
            }))
        .WillOnce(::testing::Invoke(
            [&](bool nwStandbyMode) {
                EXPECT_EQ(nwStandbyMode, false);
                _networkStandbyMode = nwStandbyMode;
                wg.Done();
            }));

    _powerState = Exchange::IPowerManager::PowerState::POWER_STATE_ON;
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("setEnabled"), _T("{\"enabled\": true }"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("getEnabled"), _T("{}"), response));
    EXPECT_EQ(response, string("{\"enabled\":true,\"success\":true}"));

    ASSERT_NE(_networkStandbyModeChangedNotification, nullptr);
    ASSERT_NE(_modeChangedNotification, nullptr);

    _networkStandbyModeChangedNotification->OnNetworkStandbyModeChanged(true);
    usleep(50);
    _modeChangedNotification->OnPowerModeChanged(Exchange::IPowerManager::PowerState::POWER_STATE_STANDBY_DEEP_SLEEP, Exchange::IPowerManager::PowerState::POWER_STATE_ON);
    usleep(50);
    _modeChangedNotification->OnPowerModeChanged(Exchange::IPowerManager::PowerState::POWER_STATE_ON,Exchange::IPowerManager::PowerState::POWER_STATE_STANDBY_DEEP_SLEEP);
    sleep(1);
    _networkStandbyModeChangedNotification->OnNetworkStandbyModeChanged(false);
    wg.Wait();

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("setEnabled"), _T("{\"enabled\": false }"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("getEnabled"), _T("{}"), response));
    EXPECT_EQ(response, string("{\"enabled\":false,\"success\":true}"));

    if (Core::ERROR_NONE == status)
    {
        releaseResources();
    }
}

TEST_F(XCastTest, onNetworkManagerEvents)
{
    Core::hresult status = createResources();
    WaitGroup wg;
    wg.Add();

    EXPECT_CALL(*p_gdialserviceImplMock, ActivationChanged(::testing::_,::testing::_))
        .Times(2)
        .WillOnce(::testing::Invoke(
            [&](std::string activation, std::string friendlyname) {
                EXPECT_EQ(activation, "false");
                EXPECT_EQ(friendlyname, "friendlyTest");
                return GDIAL_SERVICE_ERROR_NONE;
            }))
        .WillOnce(::testing::Invoke(
            [&](std::string activation, std::string friendlyname) {
                EXPECT_EQ(activation, "false");
                EXPECT_EQ(friendlyname, "friendlyTest");
                wg.Done();
                return GDIAL_SERVICE_ERROR_NONE;
            }));

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("setStandbyBehavior"), _T("{\"standbybehavior\": \"active\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));
    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("getStandbyBehavior"), _T("{}"), response));
    EXPECT_EQ(response, string("{\"standbybehavior\":\"active\",\"success\":true}"));

    EXPECT_EQ(Core::ERROR_NONE, mJsonRpcHandler.Invoke(connection, _T("setFriendlyName"), _T("{\"friendlyname\": \"friendlyTest\"}"), response));
    EXPECT_EQ(response, string("{\"success\":true}"));

    ASSERT_NE(_networkManagerNotification, nullptr);
    _networkManagerNotification->onWiFiSignalQualityChange("myHomeSSID", -32, -106, 74, Exchange::INetworkManager::WIFI_SIGNAL_EXCELLENT);
    _networkManagerNotification->onWiFiStateChange(Exchange::INetworkManager::WIFI_STATE_DISCONNECTED);
    _networkManagerNotification->onAvailableSSIDs("{\"AvailableSSIDs\":[{\"SSID\":\"myHomeSSID\",\"BSSID\":\"00:11:22:33:44:55\",\"SignalStrength\":\"-32\",\"Frequency\":\"2412\",\"Security\":\"WPA2-Personal\"},{\"SSID\":\"myOfficeSSID\",\"BSSID\":\"66:77:88:99:AA:BB\",\"SignalStrength\":\"-45\",\"Frequency\":\"2412\",\"Security\":\"WPA2-Enterprise\"}]}");
    _networkManagerNotification->onInternetStatusChange(Exchange::INetworkManager::INTERNET_NOT_AVAILABLE, Exchange::INetworkManager::INTERNET_FULLY_CONNECTED, "eth0");
    _networkManagerNotification->onInterfaceStateChange(Exchange::INetworkManager::INTERFACE_LINK_UP, "eth0");
    _networkManagerNotification->onIPAddressChange("eth0", "IPv4", "192.168.5.100", Exchange::INetworkManager::IP_ACQUIRED);
    sleep(1);
    _networkManagerNotification->onActiveInterfaceChange("eth0", "wlan0");

    wg.Wait();

    if (Core::ERROR_NONE == status)
    {
        releaseResources();
    }
}