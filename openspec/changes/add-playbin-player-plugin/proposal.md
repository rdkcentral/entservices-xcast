## Why

The existing `GStreamerPlayer` plugin in `entservices-xcast` proves out the out-of-process, COM-RPC + JSON-RPC playback plugin pattern, but it builds its GStreamer pipeline manually (`filesrc -> qtdemux -> h264parse/decodebin -> westerossink/autoaudiosink`). That pipeline is hardcoded to one container/codec combination and duplicates element wiring, pad-linking, and error handling that GStreamer's `playbin` element already solves generically (source, demux, decode, and sink selection for any URI scheme/codec the target has plugins for). We need a `Player` plugin, following the same architectural conventions, that delegates pipeline construction to `playbin` so it can play local files, `file://` URIs, and HTTP/HTTPS URLs without a bespoke pipeline per format.

## What Changes

- Add a new out-of-process Thunder plugin, **`PlaybinPlugin`**, under `PlayerPlugin/PlaybinPlugin/`, modeled directly on `entservices-xcast/GStreamerPlayer` (in-process shell + out-of-process `Implementation` host, COM-RPC aggregation, JSON-RPC via generated `JPlaybinPlugin`/`JsonData_PlaybinPlugin`).
- Expose four primary JSON-RPC/COM-RPC methods on the new interface: `Configure(media)`, `Play()`, `Pause()`, `Stop()`, plus the standard `Register`/`Unregister` notification pair (mirroring `IGStreamerPlayer::INotification`).
- Internally, the `Implementation` class owns a single `playbin` element (`gst_element_factory_make("playbin", ...)`) instead of manually building a pipeline; `Configure()` only sets the `uri` property, `Play()`/`Pause()`/`Stop()` only change pipeline state (`PLAYING`/`PAUSED`/`NULL` + unref).
- Add a new COM-RPC interface definition `IPlaybinPlugin.h` to `entservices-apis/apis/PlaybinPlugin/`, following the exact conventions already used by `apis/GStreamerPlayer/IGStreamerPlayer.h` (license header, `@json 1.0.0` tag, `Core::hresult` methods, `@text`/`@brief`/`@param`/`@retval` annotations, nested `INotification` with `OnPlayerInitialized`/`OnPlayerStopped`-style events), plus new `ID_PLAYBIN_PLUGIN*` entries in `apis/Ids.h`.
- Add the matching out-of-process build/config wiring in `PlayerPlugin`: `CMakeLists.txt` (plugin + implementation shared libraries), `PlaybinPlugin.conf.in` (`mode = Local`, `locator = lib<...>Implementation.so`), `Module.h`/`Module.cpp`, and a `PLUGIN_PLAYBINPLUGIN` option wired into a `services.cmake`/root `CMakeLists.txt` for `PlayerPlugin` (currently absent, following the `entservices-xcast` top-level pattern).
- Add an L1 test target (`Tests/L1Tests`) covering plugin initialization and `Configure`/`Play`/`Pause`/`Stop` state transitions and invalid-state handling, following the `test_XCast.cpp` gtest/gmock pattern.
- Does **not** modify `entservices-xcast/GStreamerPlayer` or any other existing plugin - the manual-pipeline plugin remains untouched and continues to exist as a separate, independent plugin.

## Capabilities

### New Capabilities
- `player-plugin`: Out-of-process Thunder plugin that configures and controls a single `playbin`-backed media playback session (`Configure`/`Play`/`Pause`/`Stop`) over both COM-RPC and JSON-RPC, including pipeline lifecycle and invalid-state handling.

### Modified Capabilities
- None. This change only adds new capability; it does not alter the requirements of any existing spec.

## Impact

- **New code**: `PlayerPlugin/PlaybinPlugin/` (plugin + implementation sources, headers, `Module.*`, `CMakeLists.txt`, `.conf.in`), `PlayerPlugin/CMakeLists.txt` + `services.cmake` (new, since `PlayerPlugin` has no root build file yet), `PlayerPlugin/Tests/L1Tests/` test sources.
- **entservices-apis**: new `apis/PlaybinPlugin/IPlaybinPlugin.h` interface header and new ID allocations in `apis/Ids.h`; triggers regeneration of COM-RPC proxy/stubs and JSON-RPC glue (`JPlaybinPlugin.h`, `JsonData_PlaybinPlugin.h`) via the existing `ProxyStubGenerator`/`JsonGenerator` CMake integration - no manual generated-code edits.
- **entservices-xcast**: read-only reference only; no files in this repo are modified.
- **Dependencies**: GStreamer `playbin` and its plugin set (whatever demuxers/decoders/sinks are installed on the target, e.g. `playbin` + `westerossink`/`autoaudiosink` or platform equivalents) must be available at runtime; no new build-time dependency beyond what `GStreamerPlayer` already requires (`GStreamer::GStreamer` CMake package, Thunder/WPEFramework plugin + COM/Protocols packages).
- **Runtime**: introduces one new out-of-process host process (`org.rdk.PlaybinPlugin` callsign, `Local` mode) started/stopped by Thunder like `GStreamerPlayer`; no impact on other plugins' processes.
