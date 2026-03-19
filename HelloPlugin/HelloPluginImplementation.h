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
#include <interfaces/IHelloPlugin.h>

#include <com/com.h>
#include <core/core.h>
#include <list>
#include <mutex>

namespace WPEFramework {
    namespace Plugin {

        /**
         * @brief Out-of-process implementation of IHelloPlugin.
         *
         * This class runs inside a separate host process. On startup it prints
         * "Hi, I'm plugin and I'm working happily" to standard output and to
         * the system log.
         */
        class HelloPluginImplementation : public Exchange::IHelloPlugin {
        public:
            HelloPluginImplementation();
            ~HelloPluginImplementation() override;

            HelloPluginImplementation(const HelloPluginImplementation&) = delete;
            HelloPluginImplementation& operator=(const HelloPluginImplementation&) = delete;

            BEGIN_INTERFACE_MAP(HelloPluginImplementation)
            INTERFACE_ENTRY(Exchange::IHelloPlugin)
            END_INTERFACE_MAP

            // IHelloPlugin
            Core::hresult Register(IHelloPlugin::INotification* sink) override;
            Core::hresult Unregister(IHelloPlugin::INotification* sink) override;
            Core::hresult GetGreeting(string& message) override;

        private:
            mutable Core::CriticalSection _adminLock;
            std::list<Exchange::IHelloPlugin::INotification*> _notificationClients;

            static const string GREETING_MESSAGE;
        };

    } // namespace Plugin
} // namespace WPEFramework
