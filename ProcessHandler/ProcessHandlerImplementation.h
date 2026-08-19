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

/**
 * @file ProcessHandlerImplementation.h
 * @brief Out-of-process implementation of IProcessHandler.
 *
 * This class runs in a separate WPEProcess host.  It directly executes:
 *   Kill  – ::kill(pid, SIGKILL) to terminate the target process.
 *   Top   – popen("top -b -n1") to capture a snapshot of all running processes.
 *
 * There are no events; no INotification interface is needed.
 */

#pragma once

#include "Module.h"
#include <interfaces/IProcessHandler.h>

#include <com/com.h>
#include <core/core.h>

namespace WPEFramework {
namespace Plugin {

    class ProcessHandlerImplementation : public Exchange::IProcessHandler {
    public:
        ProcessHandlerImplementation();
        ~ProcessHandlerImplementation() override;

        ProcessHandlerImplementation(const ProcessHandlerImplementation&)            = delete;
        ProcessHandlerImplementation& operator=(const ProcessHandlerImplementation&) = delete;

        BEGIN_INTERFACE_MAP(ProcessHandlerImplementation)
        INTERFACE_ENTRY(Exchange::IProcessHandler)
        END_INTERFACE_MAP

        // ----- IProcessHandler -----
        Core::hresult Kill(const uint32_t pid) override;
        Core::hresult Top(string& output) override;
    };

} // namespace Plugin
} // namespace WPEFramework
