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

#include "HelloPluginImplementation.h"
#include <iostream>

namespace WPEFramework {
    namespace Plugin {

        SERVICE_REGISTRATION(HelloPluginImplementation, 1, 0);

        /* static */ const string HelloPluginImplementation::GREETING_MESSAGE =
            _T("Hi, I'm plugin and I'm working happily");

        HelloPluginImplementation::HelloPluginImplementation()
        {
            // Output the greeting immediately when the out-of-process implementation starts.
            std::cout << GREETING_MESSAGE << std::endl;
            SYSLOG(Logging::Startup, (_T("HelloPluginImplementation: %s"), GREETING_MESSAGE.c_str()));
        }

        HelloPluginImplementation::~HelloPluginImplementation()
        {
            SYSLOG(Logging::Shutdown, (_T("HelloPluginImplementation Destructor")));
        }

        Core::hresult HelloPluginImplementation::Register(IHelloPlugin::INotification* sink)
        {
            ASSERT(sink != nullptr);

            _adminLock.Lock();
            auto it = std::find(_notificationClients.begin(), _notificationClients.end(), sink);
            if (it == _notificationClients.end()) {
                sink->AddRef();
                _notificationClients.push_back(sink);
            }
            _adminLock.Unlock();

            return Core::ERROR_NONE;
        }

        Core::hresult HelloPluginImplementation::Unregister(IHelloPlugin::INotification* sink)
        {
            ASSERT(sink != nullptr);

            _adminLock.Lock();
            auto it = std::find(_notificationClients.begin(), _notificationClients.end(), sink);
            if (it != _notificationClients.end()) {
                (*it)->Release();
                _notificationClients.erase(it);
            }
            _adminLock.Unlock();

            return Core::ERROR_NONE;
        } //end of Unregister

        Core::hresult HelloPluginImplementation::GetGreeting(string& message)
        {
            message = GREETING_MESSAGE;
            SYSLOG(Logging::Notification, (_T("HelloPluginImplementation::GetGreeting: %s"), message.c_str()));
            return Core::ERROR_NONE;
        }

    } // namespace Plugin
} // namespace WPEFramework
