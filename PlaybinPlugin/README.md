# PlaybinPlugin

Out-of-process Thunder plugin that plays a configured media URI using a single
GStreamer `playbin` element, exposed over both COM-RPC and JSON-RPC.

## Why `playbin`

Unlike `GStreamerPlayer` (which builds a manual pipeline of a fixed
`filesrc -> qtdemux -> h264parse/decodebin -> westerossink/autoaudiosink`
chain), `PlaybinPlugin` delegates source/demux/decode/sink selection to
GStreamer's `playbin` element. `playbin` inspects the configured URI and the
GStreamer plugins installed on the target at runtime to build an appropriate
pipeline, so the plugin is not limited to one container/codec combination and
does not need to be extended every time a new format must be supported.

**Media format support is determined entirely by the GStreamer plugins and
codecs installed on the target device at runtime - not by this plugin, and
not by a media file's extension.** A URL or path may be accepted by
`Configure()` and still fail to play if the target has no compatible
demuxer/decoder/sink installed; such failures are reported via the
`onPlayerError` notification (see below), not silently ignored.

## Architecture

```
                 COM-RPC
                    |
                 JSON-RPC
                    |
                    v
          +-------------------+
          |   PlaybinPlugin    |   <- in-process shell (PlaybinPlugin.h/.cpp)
          +-------------------+
                    |  COM-RPC (Thunder Root<>/out-of-process host)
                    v
          +-------------------------------+
          | PlaybinPluginImplementation    |   <- out-of-process host
          +-------------------------------+
                    |
                    v
                 playbin
                    |
          +---------+---------+
          |                   |
       Audio                Video
          |                   |
       Sink                  Sink
```

`PlaybinPlugin` is the in-process shell that lives in the Thunder framework
process; it forwards every method call and notification across the COM-RPC
boundary to/from `PlaybinPluginImplementation`, which runs in its own
out-of-process host (`mode = Local`) and owns the actual `playbin` element.
This mirrors the `GStreamerPlayer` plugin's split exactly - the only
difference is the pipeline construction strategy used by the implementation.

## Supported input

`Configure()` accepts a single string that may be:

- An absolute local file system path (no URI scheme) - normalized internally
  to a `file://` URI via `gst_filename_to_uri()`.
- A `file://` URI - stored as-is.
- An `http://` or `https://` URL - stored as-is.
- Any other URI scheme that the underlying `playbin`/GStreamer install has a
  source element for - stored as-is; acceptance does not guarantee
  playability (see "Why `playbin`" above).

An empty string, or a local path that cannot be resolved to a URI, is
rejected with `Core::ERROR_GENERAL` and does not change any previously
configured media.

## API

| Method      | Behavior |
|-------------|----------|
| `configure` | Stores the media location for later playback. Does not start playback. If a pipeline is currently PLAYING/PAUSED, it is stopped and released first. |
| `play`      | Creates the `playbin` pipeline (first call after `configure`/`stop`) and sets it to PLAYING, or resumes an existing PAUSED pipeline to PLAYING. No-op success if already PLAYING. Returns `ERROR_ILLEGAL_STATE` if no media has been configured. |
| `pause`     | Transitions an existing pipeline to PAUSED without destroying it. Returns `ERROR_ILLEGAL_STATE` if no pipeline exists. |
| `stop`      | Transitions the pipeline to NULL and releases it. The last configured media is remembered, so a following `play` recreates a pipeline for it without requiring `configure` again. Returns `ERROR_ILLEGAL_STATE` if no pipeline exists. |

Notifications (`Register`/`Unregister` a listener):

- `onPlayerInitialized` - fired once the pipeline reaches PLAYING for the first time after creation.
- `onPlayerStopped` - fired when `stop` completes releasing the pipeline.
- `onPlayerError` - fired when the GStreamer bus reports an error (e.g. unsupported/unreachable media), carrying a human-readable message.

Both COM-RPC and JSON-RPC calls invoke the same `PlaybinPluginImplementation`
logic; no behavior is duplicated between the two transports.

## State transitions

```
Plugin Created
      |
      v
No playbin / idle
      |
      v
configure()  -----------------------------------+
      |                                         | (already PLAYING/PAUSED:
      v                                         |  stop+release existing
play()                                          |  pipeline first)
      |                                         |
      v                                         |
   PLAYING <----------------- play() (resume) --+
      |            ^
   pause()         |
      v          play()
   PAUSED --------- 
      |
   stop()
      v
    NULL
      |
      v
Destroy/release playbin (last configured media is remembered)
```

Invalid-state calls (`play` before `configure`, `pause`/`stop` with no active
pipeline, a second `stop` with no intervening `play`) return
`Core::ERROR_ILLEGAL_STATE` rather than silently succeeding or crashing.

## Out-of-process configuration

`PlaybinPlugin.conf.in` configures the plugin as `mode = "Local"`
(out-of-process), with `callsign = "org.rdk.PlaybinPlugin"` and
`locator = "lib<NAMESPACE>PlaybinPluginImplementation.so"`, following the
same `write_config()` build step used by `GStreamerPlayer`.

-----------------
Build:

Enable the plugin with `-DPLUGIN_PLAYBINPLUGIN=ON` when configuring the
`PlayerPlugin` CMake project (mirrors `PLUGIN_GSTREAMERPLAYER` in
`entservices-xcast`).

-----------------
Test:

```
curl --header "Content-Type: application/json" --request POST \
     --data '{"jsonrpc":"2.0","id":"1","method":"org.rdk.PlaybinPlugin.1.configure","params":{"media":"https://example.com/sample.mp4"}}' \
     http://127.0.0.1:9998/jsonrpc

curl --header "Content-Type: application/json" --request POST \
     --data '{"jsonrpc":"2.0","id":"2","method":"org.rdk.PlaybinPlugin.1.play"}' \
     http://127.0.0.1:9998/jsonrpc
```
