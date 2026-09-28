## Context

`entservices-xcast/GStreamerPlayer` is the reference implementation for this change: an out-of-process Thunder plugin split into an in-process shell (`GStreamerPlayer.h/.cpp`, aggregates `Exchange::IGStreamerPlayer` via `_service->Root<>()`, forwards JSON-RPC through generated `JGStreamerPlayer`) and an out-of-process `Implementation` (`GStreamerPlayerImplementation.h/.cpp`, `SERVICE_REGISTRATION`, owns the actual GStreamer objects). The interface is defined in `entservices-apis/apis/GStreamerPlayer/IGStreamerPlayer.h` using the repo's standard COM-RPC/JSON-RPC annotation conventions (`@json 1.0.0`, `Core::hresult` methods, `@text`/`@brief`/`@param`/`@retval`, nested `INotification`). Build wiring follows `CMakeLists.txt` (two shared libraries: shell + Implementation), `<Plugin>.conf.in` (`mode = Local`, `locator = lib<...>Implementation.so`), `Module.h`/`Module.cpp`, and a `PLUGIN_<NAME>` option in `services.cmake` included by the repo's top-level `CMakeLists.txt`. `PlayerPlugin/` currently has only `helpers/`, `openspec/`, and an empty `PlaybinPlugin/` placeholder directory - it has no root `CMakeLists.txt`/`services.cmake` yet, so those must be created (not just extended) as part of this change. See proposal.md for motivation; see `specs/player-plugin/spec.md` for the required behavior.

## Goals / Non-Goals

**Goals:**
- Reuse the exact `entservices-xcast` plugin/implementation/interface/build split for the new `PlaybinPlugin`, changing only the pipeline construction strategy (single `playbin` element instead of manually wired elements).
- Keep COM-RPC and JSON-RPC backed by one shared `Implementation` class - no behavior duplicated between the two transports.
- Handle the invalid-state and repeated-call edge cases listed in `specs/player-plugin/spec.md` explicitly and return distinct, existing `Core::hresult` error codes rather than inventing new ones.

**Non-Goals:**
- Re-architecting or modifying `entservices-xcast/GStreamerPlayer` itself; it stays in place as an independent plugin.
- Advanced playback features not requested (seeking, playlists, DRM, subtitle/track selection, volume control). Only `Configure`/`Play`/`Pause`/`Stop` (+ lifecycle notifications) are in scope, per the proposal's explicit API-surface constraint.
- Building a manual element-by-element pipeline for any format; `playbin` is solely responsible for source/demux/decode/sink selection.

## Decisions

**1. New interface `IPlaybinPlugin.h` in `entservices-apis/apis/PlaybinPlugin/`, not a reuse/extension of `IGStreamerPlayer.h`.**
The two plugins are independently deployed processes with different pipeline strategies and the proposal explicitly requires the manual-pipeline plugin be left untouched. A distinct interface avoids coupling their lifecycles/versioning. Mirrors `IGStreamerPlayer.h` structure exactly:
```cpp
/* @json 1.0.0 @text:keep */
struct EXTERNAL IPlaybinPlugin : virtual public Core::IUnknown {
    enum { ID = ID_PLAYBIN_PLUGIN };

    // @event
    struct EXTERNAL INotification : virtual public Core::IUnknown {
        enum { ID = ID_PLAYBIN_PLUGIN_NOTIFICATION };
        virtual void OnPlayerInitialized() {}   // fired on first PLAYING after Play()
        virtual void OnPlayerStopped() {}       // fired when Stop() completes
        virtual void OnPlayerError(const string& message /* @text message */) {} // fired on GST_MESSAGE_ERROR from the bus
    };

    virtual Core::hresult Register(IPlaybinPlugin::INotification* sink) = 0;
    virtual Core::hresult Unregister(IPlaybinPlugin::INotification* sink) = 0;

    virtual Core::hresult Configure(const string& media /* @text media */) = 0;
    virtual Core::hresult Play() = 0;
    virtual Core::hresult Pause() = 0;
    virtual Core::hresult Stop() = 0;
};
```
Alternative considered: add `Configure`/switch `IGStreamerPlayer` to use `playbin` internally. Rejected because the proposal and constraints require the existing plugin to remain unmodified, and the two plugins intentionally offer different pipeline guarantees (manual vs. `playbin`) that callers may choose between.

**2. ID allocation: `ID_PLAYBIN_PLUGIN = ID_ENTOS_OFFSET + 0x540`.**
`apis/Ids.h` allocates IDs in ascending 0x10-sized blocks; the highest currently allocated is `ID_GSTREAMER_PLAYER = ID_ENTOS_OFFSET + 0x530`. `0x540` is the next free block (`0x540` main, `0x541` notification). This must be re-verified against `apis/Ids.h` at implementation time in case other changes land first, per the repo's "never change once assigned" rule.

**3. `Configure` normalizes bare file paths to `file://` URIs before storing; `Play` never re-parses or re-normalizes.**
Keeps URI handling in one place (`Configure`) rather than duplicating scheme-detection logic in `Play`. A path is treated as a bare local path (and converted) when it does not contain `"://"`; anything already containing a scheme (`file://`, `http://`, `https://`, etc.) is stored unchanged and passed through to `playbin`'s `uri` property as-is, since `playbin` accepts any URI scheme its installed source elements support.

**4. `Configure` while PLAYING/PAUSED stops and releases the existing pipeline before applying the new configuration (rather than rejecting the call).**
Matches `GStreamerPlayerImplementation::Play()`'s existing pattern of tearing down any pre-existing pipeline before building a new one, and avoids forcing callers to manually `Stop()` before every `Configure()`. Rejected alternative: return `Core::ERROR_ILLEGAL_STATE` and require an explicit `Stop()` first - adds friction for the common "switch media" case without a corresponding safety benefit, since `Configure` alone never starts playback.

**5. `Stop` fully destroys the pipeline (`GST_STATE_NULL` + unref); it does not clear the last-configured media location.**
This matches the proposal's explicit lifecycle diagram (`Stop -> NULL -> Destroy/release playbin`) and the spec's "Play after Stop" scenario: the last configured media is remembered independently of pipeline existence, so `Play` after `Stop` recreates a pipeline from that remembered URI. `Configure` remains the only way to change or clear which media is targeted.

**6. Single `playbin` element replaces the entire manual chain; state changes go through `gst_element_set_state()` on that one element.**
`playbin` internally manages `uridecodebin`-style source/demux/decode selection and default video/audio sinks (overridable via `video-sink`/`audio-sink` properties, not used unless a future change needs custom sink selection). This directly satisfies the proposal's "do not manually construct the complete pipeline" constraint and removes the pad-linking/queue logic (`OnPadAdded`, `OnDecodebinPadAdded`) that `GStreamerPlayerImplementation` needs for its manual pipeline - the `Implementation` class for this plugin only needs a bus watch (`OnBusMessage`) for `ASYNC_DONE`/`ERROR`/`EOS`, reused from the same pattern.

**7. Directory/build naming: plugin folder `PlayerPlugin/PlaybinPlugin/`, Thunder plugin class `PlaybinPlugin`, callsign `org.rdk.PlaybinPlugin`, CMake option `PLUGIN_PLAYBINPLUGIN`.**
Follows the already-created (empty) `PlayerPlugin/PlaybinPlugin/` directory and the `GStreamerPlayer`/`PLUGIN_GSTREAMERPLAYER` naming pattern (`set(PLUGIN_NAME PlaybinPlugin)`, `${NAMESPACE}${PLUGIN_NAME}` module name, `${MODULE_NAME}Implementation`).

**8. `PlayerPlugin` gets its own root `CMakeLists.txt`/`services.cmake`, modeled on `entservices-xcast`'s, rather than being folded into `entservices-xcast`.**
The workspace already treats `PlayerPlugin` as an independent repo/directory (separate `openspec/`, separate `helpers/` copy, separate `.github/prompts`), so it needs its own top-level build entry point (`option(PLUGIN_PLAYBINPLUGIN ...)`, `add_subdirectory(PlaybinPlugin)`, `add_subdirectory(Tests/L1Tests)` guarded by `RDK_SERVICES_L1_TEST`) rather than depending on `entservices-xcast`'s build files.

## Risks / Trade-offs

- **[Risk] `playbin`'s automatic sink selection may not match the platform's expected video sink (e.g., `westerossink`) out of the box.** → Mitigation: none required for the in-scope API surface (Configure/Play/Pause/Stop only); if a future change needs sink pinning, it can set `playbin`'s `video-sink`/`audio-sink` properties without changing the public interface.
- **[Risk] `playbin` accepts a broad range of URIs at the API layer but playback success still depends on which GStreamer plugins are installed on the target.** → Mitigation: the spec's "Unsupported or invalid media" requirement makes this explicit; documentation must state that URI acceptance is not a playability guarantee, and `Play` surfaces a bus `ERROR` as a playback error/notification rather than claiming success.
- **[Risk] Reusing the bus-watch/GMainLoop-thread pattern from `GStreamerPlayerImplementation` for a single `playbin` element still requires the same thread-safety care (`_adminLock`) around notification lists and pipeline pointer access from both the COM-RPC thread and the GMainLoop thread.** → Mitigation: copy the existing locking pattern (`Core::CriticalSection`, `Core::Sink<Notification>`) verbatim rather than redesigning it.
- **[Trade-off] Adding `OnPlayerError` to `INotification` is a superset of `IGStreamerPlayer::INotification`, so the two interfaces are not identical even though they look similar.** → Accepted because the proposal explicitly asks for graceful invalid/unsupported-media handling, which `IGStreamerPlayer` does not currently surface as a notification; keeping interfaces independent (Decision 1) means this addition has no impact on `IGStreamerPlayer` consumers.

## Migration Plan

- No migration of existing systems is required; this is an additive change. `PLUGIN_PLAYBINPLUGIN` is a new, off-by-default CMake option (mirroring `PLUGIN_GSTREAMERPLAYER_AUTOSTART=false` default), so nothing changes for deployments unless the option is explicitly turned on.
- Rollback is deleting/reverting the new `PlayerPlugin/PlaybinPlugin/` and `entservices-apis/apis/PlaybinPlugin/` additions; no other plugin or interface is touched.

## Open Questions

- Exact target-specific video/audio sink requirements (e.g., whether `playbin`'s default `autovideosink`/`autoaudiosink` selection is acceptable in the deployment environment, or whether `video-sink`/`audio-sink` must be pinned to `westerossink`/a platform sink) - can be resolved during implementation/testing on target hardware without changing the public `Configure`/`Play`/`Pause`/`Stop` API or the specs.
- Whether PowerManager integration (seen as a helper in `PlayerPlugin/helpers/PowerManagerInterface.h`) is required for this plugin - not mentioned in the proposal's required API surface; left out of scope unless the user asks for it, since none of the four in-scope APIs have power-state dependent behavior described.
