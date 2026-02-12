# XCast Plugin Architecture

This document provides a comprehensive overview of the XCast plugin architecture within the RDK Thunder framework, detailing component interactions, data flow, and technical implementation.

## System Architecture Overview

The XCast plugin operates as a Thunder framework plugin, providing casting and media streaming capabilities for RDK-based devices. The architecture follows a layered approach with clear separation of concerns.

```
┌─────────────────┐    ┌──────────────────┐    ┌─────────────────┐
│   Client Apps   │◄──►│ Thunder Framework │◄──►│  XCast Plugin   │
└─────────────────┘    └──────────────────┘    └─────────────────┘
                                ▲                        │
                                │                        ▼
                       ┌─────────────────┐    ┌─────────────────┐
                       │   JSON-RPC API  │    │  Helper Modules │
                       └─────────────────┘    └─────────────────┘
                                                        │
                                                        ▼
                                             ┌─────────────────┐
                                             │   IARM Bus      │
                                             └─────────────────┘
                                                        │
                                                        ▼
                                             ┌─────────────────┐
                                             │ System Services │
                                             │ & Hardware      │
                                             └─────────────────┘
```

## Core Components

### XCast Plugin Core (`XCast/`)
- **XCast.cpp/h**: Main plugin implementation inheriting from `Thunder::PluginHost::IPlugin`
- **XCastImplementation.cpp/h**: Core business logic and service implementations
- **XCastManager.cpp/h**: Media session and casting state management
- **Module.cpp/h**: Plugin module definition and Thunder integration

### Helper Library (`helpers/`)
- **UtilsJsonRpc.h**: JSON-RPC communication utilities and method binding
- **UtilsIarm.h**: IARM bus communication wrappers for system integration
- **UtilsSynchroIarm.hpp**: Synchronous IARM operations with timeout handling
- **UtilsLogging.h**: Centralized logging functionality with RDK integration

### Build System (`cmake/`)
- **Find*.cmake**: CMake modules for dependency discovery (GLIB, GStreamer, IARM, CEC)
- **services.cmake**: Service-specific build configurations and feature flags
- **CMakeLists.txt**: Main build configuration with Thunder integration

## Component Interactions and Data Flow

### 1. Client Request Flow
```
Client App → Thunder JSON-RPC → XCast Plugin → XCastManager → IARM Bus → System Services
```

### 2. Event Notification Flow
```
System Services → IARM Bus → XCastImplementation → Thunder Events → Client App
```

### 3. Plugin Lifecycle
```
Thunder Load → Module Initialize → XCast Activate → Service Registration → Ready State → Deactivate → Cleanup
```

## Thunder Framework Integration

### Plugin Interface Implementation
- Inherits from `Thunder::PluginHost::IPlugin` interface
- Implements lifecycle methods: `Initialize()`, `Deinitialize()`, `Information()`
- Provides JSON-RPC method exposure through Thunder's automatic registration system

### Service Registration
- Registers with Thunder as "XCast" service with unique callsign
- Exposes methods via Thunder's JSON-RPC interface with automatic parameter binding
- Handles client connection management and authentication through Thunder security model

### Configuration Management
- Utilizes Thunder's configuration system for plugin settings via XCast.conf
- Supports runtime configuration updates through Thunder configuration API
- Integrates with Thunder's permission and access control framework

## Dependencies and External Interfaces

### Core Dependencies
- **Thunder Framework (R4)**: Plugin host, JSON-RPC communication, and service management
- **IARM Bus**: Inter-application communication within RDK ecosystem
- **GLIB**: Core utility libraries for data structures and event handling
- **RFC API**: Remote Feature Control for configuration management

### External Service Interfaces
- **Display Services (DS)**: Video output configuration and management
- **CEC (Consumer Electronics Control)**: HDMI device communication
- **System Manager**: Device control, reboot, and state management
- **Power Manager**: Power state coordination and deep sleep management

## Technical Implementation Details

### Memory Management
- RAII principles with smart pointers for automatic resource cleanup
- Thunder framework handles primary memory management for plugin lifecycle
- Careful resource cleanup in plugin deactivation to prevent memory leaks

### Threading Model
- Thunder framework provides main event loop and thread management
- Asynchronous operations handled through Thunder's work queue system
- Thread-safe communication via IARM synchronization utilities in helpers

### Error Handling
- Structured error codes aligned with Thunder framework conventions
- Comprehensive logging through RDK logging framework with configurable levels
- Graceful degradation when external services are unavailable

### JSON-RPC Implementation
```cpp
// Thunder auto-generates JSON-RPC bindings from C++ method signatures
class XCast : public Thunder::PluginHost::IPlugin {
    uint32_t getQuirks(IQuirks*& quirks) const override;
    uint32_t setEnabled(const bool enabled) override;
    // Methods automatically exposed via JSON-RPC
};
```

### Build Configuration
```cmake
# Thunder plugin integration with conditional compilation
if(USE_THUNDER_R4)
    find_package(${NAMESPACE}COM REQUIRED)
    target_link_libraries(${PLUGIN_NAME} PRIVATE ${NAMESPACE}COM::${NAMESPACE}COM)
endif()

# Feature flag configuration
if(PLUGIN_XCAST)
    add_subdirectory(XCast)
endif()
```

### Testing Framework
- **L1 Unit Tests**: Google Test framework with comprehensive mocking
- **Mock Services**: Isolated testing with mocked IARM, Thunder, and system services
- **CI/CD Integration**: GitHub Actions for automated build and test validation
- **Coverage Analysis**: Code coverage reporting with lcov integration

## Performance Characteristics

### Optimization Strategies
- Lazy initialization of heavyweight resources (GStreamer pipelines)
- Efficient JSON parsing with Thunder's optimized serialization
- Minimal memory footprint targeting embedded set-top box constraints
- Asynchronous operation patterns to prevent blocking Thunder event loop

### Resource Management
- Memory usage optimized for devices with 512MB-2GB RAM constraints
- CPU usage monitoring with configurable operation throttling
- Network bandwidth consideration for concurrent streaming operations
- Power management integration for energy-efficient operation

This architecture ensures maintainable, scalable, and efficient operation within the RDK ecosystem while providing robust casting capabilities that integrate seamlessly with Thunder framework's plugin model.