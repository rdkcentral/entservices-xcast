# GStreamerPlayer – cURL / JSON-RPC Commands

Thunder JSON-RPC endpoint: `http://<DEVICE_IP>:9998/jsonrpc`  
Replace `<DEVICE_IP>` with your actual device IP (e.g. `127.0.0.1` for localhost).  
All calls use the callsign: `org.rdk.GStreamerPlayer`

---

## Controller Commands

### Activate the plugin
Must be done before any other call.

```bash
curl -d '{"jsonrpc":"2.0","id":1,"method":"Controller.1.activate","params":{"callsign":"org.rdk.GStreamerPlayer"}}' http://127.0.0.1:9998/jsonrpc
```

Expected response:
```json
{"jsonrpc":"2.0","id":1,"result":null}
```

---

### Check plugin status

```bash
curl -d '{"jsonrpc":"2.0","id":2,"method":"Controller.1.status@org.rdk.GStreamerPlayer"}' http://127.0.0.1:9998/jsonrpc
```

Expected response contains `"state":"activated"` when the plugin is running.

---

### Deactivate the plugin

```bash
curl -d '{"jsonrpc":"2.0","id":3,"method":"Controller.1.deactivate","params":{"callsign":"org.rdk.GStreamerPlayer"}}' http://127.0.0.1:9998/jsonrpc
```

Expected response:
```json
{"jsonrpc":"2.0","id":3,"result":null}
```

---

## GStreamerPlayer API Commands

### play
Load and play media from the given URI. Builds a GStreamer pipeline internally: `uridecodebin → queue → westerossink / autoaudiosink`.

```bash
curl -d '{"jsonrpc":"2.0","id":10,"method":"org.rdk.GStreamerPlayer.1.play","params":{"uri":"https://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4"}}' http://127.0.0.1:9998/jsonrpc

curl -d '{"jsonrpc":"2.0","id":10,"method":"org.rdk.GStreamerPlayer.1.play","params":{"uri":"https://gstreamer.freedesktop.org/data/media/sintel_trailer-480p.webm"}}' http://127.0.0.1:9998/jsonrpc

```
Expected response:
```json
{"jsonrpc":"2.0","id":10,"result":null}
```

---

### pause
Pause the current playback.

```bash
curl -d '{"jsonrpc":"2.0","id":11,"method":"org.rdk.GStreamerPlayer.1.pause","params":{}}' http://127.0.0.1:9998/jsonrpc
```

Expected response:
```json
{"jsonrpc":"2.0","id":11,"result":null}
```

---

### setResolution
Set the video window position and size on screen (calls `g_object_set` on the `westerossink` element).

```bash
curl -d '{"jsonrpc":"2.0","id":12,"method":"org.rdk.GStreamerPlayer.1.setResolution","params":{"x":0,"y":0,"width":1920,"height":1080}}' http://127.0.0.1:9998/jsonrpc
```

Expected response:
```json
{"jsonrpc":"2.0","id":12,"result":null}
```

---

### stop
Stop playback and clean up the GStreamer pipeline.

```bash
curl -d '{"jsonrpc":"2.0","id":13,"method":"org.rdk.GStreamerPlayer.1.stop","params":{}}' http://127.0.0.1:9998/jsonrpc
```

Expected response:
```json
{"jsonrpc":"2.0","id":13,"result":null}
```

---

## Events (WebSocket)

Events require a WebSocket connection. Use `wscat` (`npm install -g wscat`).

```bash
wscat -c ws://127.0.0.1:9998/jsonrpc
```

Then send the registration messages:

### Subscribe to onPlayerInitialized
Fired when the GStreamer pipeline has been created and playback has started.

```json
{"jsonrpc":"2.0","id":20,"method":"org.rdk.GStreamerPlayer.1.register","params":{"event":"onPlayerInitialized","id":"client.events.1"}}
```

### Subscribe to onPlayerStopped
Fired when the player has been stopped and the pipeline is cleaned up.

```json
{"jsonrpc":"2.0","id":21,"method":"org.rdk.GStreamerPlayer.1.register","params":{"event":"onPlayerStopped","id":"client.events.1"}}
```

---

## Notes

- **Thunder default port**: `9998`
- **JSON-RPC version**: `2.0`
- **Method format**: `<callsign>.<version>.<methodName>`
- **Plugin callsign**: defined in `GStreamerPlayer.conf.in` as `org.rdk.GStreamerPlayer`
- If a security token is required on the device, add the header: `-H "Authorization: Bearer <TOKEN>"`
