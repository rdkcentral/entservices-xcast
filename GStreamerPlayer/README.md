# GStreamerPlayer Plugin

## Overview

The GStreamerPlayer plugin provides a Thunder/WPEFramework-based media player implementation using GStreamer. It supports basic playback controls (play, pause, stop) and provides pipeline state notifications.

## Features

- **Play/Pause/Stop Controls**: Basic media playback control
- **URL Management**: Set media URL for playback (HTTP, HTTPS, file:// URLs)
- **State Notifications**: Real-time pipeline state change notifications
- **Out-of-Process**: Runs in separate process for stability and isolation
- **Auto-generated JSON-RPC**: All APIs automatically exposed via JSON-RPC

## Architecture

```
┌─────────────────────────────────────────────┐
│  GStreamerPlayer.h/cpp (Plugin Layer)       │
│  - Thunder Plugin Interface                 │
│  - JSON-RPC Auto-registration               │
│  - Notification Forwarding                  │
└──────────────┬──────────────────────────────┘
               │ COM-RPC Interface
               │
┌──────────────▼──────────────────────────────┐
│  GStreamerPlayerImplementation.h/cpp        │
│  - GStreamer Pipeline Management            │
│  - Playback Control Logic                   │
│  - State Change Notifications               │
└─────────────────────────────────────────────┘
```

## Files Created

### API Interface (entservices-apis):
- `apis/GStreamerPlayer/IGStreamerPlayer.h` - Thunder interface definition
- `apis/Ids.h` - Updated with GStreamerPlayer IDs

### Implementation (entservices-casting):
- `GStreamerPlayer/GStreamerPlayer.h` - Plugin header
- `GStreamerPlayer/GStreamerPlayer.cpp` - Plugin implementation
- `GStreamerPlayer/GStreamerPlayerImplementation.h` - Business logic header
- `GStreamerPlayer/GStreamerPlayerImplementation.cpp` - GStreamer integration
- `GStreamerPlayer/Module.h` - Module definition
- `GStreamerPlayer/Module.cpp` - Module implementation
- `GStreamerPlayer/CMakeLists.txt` - Build configuration
- `GStreamerPlayer/GStreamerPlayer.conf.in` - Plugin configuration

## API Methods

### setURL
Sets the media URL to be played.
```json
{
  "jsonrpc": "2.0",
  "method": "org.rdk.GStreamerPlayer.1.setURL",
  "params": {
    "url": "http://example.com/video.mp4"
  }
}
```

### play
Starts or resumes playback.
```json
{
  "jsonrpc": "2.0",
  "method": "org.rdk.GStreamerPlayer.1.play"
}
```

### pause
Pauses playback.
```json
{
  "jsonrpc": "2.0",
  "method": "org.rdk.GStreamerPlayer.1.pause"
}
```

### stop
Stops playback and resets the pipeline.
```json
{
  "jsonrpc": "2.0",
  "method": "org.rdk.GStreamerPlayer.1.stop"
}
```

### getState
Gets the current pipeline state.
```json
{
  "jsonrpc": "2.0",
  "method": "org.rdk.GStreamerPlayer.1.getState"
}
```

Response:
```json
{
  "jsonrpc": "2.0",
  "result": {
    "state": "playing",
    "success": true
  }
}
```

## Events

### onPipelineStateChanged
Triggered when the GStreamer pipeline state changes.

```json
{
  "jsonrpc": "2.0",
  "method": "org.rdk.GStreamerPlayer.1.onPipelineStateChanged",
  "params": {
    "state": "playing",
    "error": "none"
  }
}
```

**States:**
- `stopped` - Pipeline is stopped
- `playing` - Media is playing
- `paused` - Playback is paused
- `error` - An error occurred

**Error Codes:**
- `none` - No error
- `invalidUrl` - Invalid URL provided
- `playbackError` - Playback failed
- `notReady` - Pipeline not ready

## Building

### Prerequisites
- Thunder/WPEFramework
- GStreamer 1.0+
- GLib 2.0+

### Build Steps

1. Build the API interface:
```bash
cd entservices-apis/build
cmake ..
make
```

2. Build the plugin:
```bash
cd entservices-casting/build
cmake .. -DPLUGIN_GSTREAMERPLAYER=ON
make
```

## Configuration

Edit `GStreamerPlayer.conf.in`:

```json
{
  "callsign": "org.rdk.GStreamerPlayer",
  "autostart": false,
  "configuration": {
    "root": {
      "mode": "Off"  // "Off" = out-of-process, "Local" = in-process
    }
  }
}
```

## Usage Example

### Using Thunder CLI:

```bash
# Activate the plugin
curl -d '{"jsonrpc":"2.0","id":1,"method":"Controller.1.activate","params":{"callsign":"org.rdk.GStreamerPlayer"}}' http://localhost:9998/jsonrpc

# Set URL
curl -d '{"jsonrpc":"2.0","id":2,"method":"org.rdk.GStreamerPlayer.1.setURL","params":{"url":"http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4"}}' http://localhost:9998/jsonrpc

# Play
curl -d '{"jsonrpc":"2.0","id":3,"method":"org.rdk.GStreamerPlayer.1.play"}' http://localhost:9998/jsonrpc

# Get state
curl -d '{"jsonrpc":"2.0","id":4,"method":"org.rdk.GStreamerPlayer.1.getState"}' http://localhost:9998/jsonrpc

# Pause
curl -d '{"jsonrpc":"2.0","id":5,"method":"org.rdk.GStreamerPlayer.1.pause"}' http://localhost:9998/jsonrpc

# Stop
curl -d '{"jsonrpc":"2.0","id":6,"method":"org.rdk.GStreamerPlayer.1.stop"}' http://localhost:9998/jsonrpc
```

### Subscribing to Events:

```bash
# Subscribe to state change events
curl -d '{"jsonrpc":"2.0","id":7,"method":"org.rdk.GStreamerPlayer.1.register","params":{"event":"onPipelineStateChanged","id":"client1"}}' http://localhost:9998/jsonrpc
```

## Implementation Details

### GStreamer Pipeline
- Uses `playbin` element for automatic decoding and playback
- Supports multiple protocols (HTTP, HTTPS, file://)
- Automatic format detection and codec selection

### Thread Safety
- Critical sections protect notification list
- Thread-safe state management
- Proper synchronization for GStreamer callbacks

### Process Isolation
- Runs out-of-process by default
- Automatic restart on crashes
- Clean shutdown handling

## Guidelines Followed

This implementation follows all instructions from:
- ✅ `entservices-apis/.github/instructions/api_headers.instructions.md`
- ✅ `entservices-apis/.github/instructions/api_headers_methods.instructions.md`
- ✅ `entservices-apis/.github/instructions/api_headers_notifications.instructions.md`
- ✅ `entservices-casting/.github/instructions/Plugin.instructions.md`
- ✅ `entservices-casting/.github/instructions/Pluginimplementation.instructions.md`

Key compliance points:
- All methods return `Core::hresult`
- Notifications return `void`
- PascalCase in C++ interface, camelCase in JSON-RPC (via `@text` tags)
- `@json 1.0.0` and `@text:keep` tags present
- Auto-generated JSON-RPC stubs used (`JGStreamerPlayer`)
- Out-of-process support with `RPC::IRemoteConnection::INotification`
- Proper registration/unregistration in Initialize/Deinitialize

## Testing

### Unit Tests
TBD - Following L1/L2 test framework patterns

### Manual Testing
1. Activate plugin via Thunder Controller
2. Set a valid media URL
3. Call play and verify playback
4. Verify state change notifications
5. Test pause/resume functionality
6. Test stop and pipeline reset

## Troubleshooting

### Plugin fails to initialize
- Check if GStreamer is properly installed
- Verify Thunder framework is running
- Check plugin logs in `/opt/logs/`

### Playback errors
- Verify URL is accessible
- Check codec support in GStreamer
- Review GStreamer error messages in logs

### State change notifications not received
- Verify event subscription
- Check notification registration
- Review COM-RPC connection status

## Future Enhancements

Potential features for future versions:
- Seek functionality
- Volume control
- Position/Duration queries
- Multiple audio/subtitle track support
- Buffering state notifications
- Network quality adaptation

## License

Copyright 2024 RDK Management

Licensed under the Apache License, Version 2.0
