# Server generated alarm speech

The unified schedule manager keeps schedule metadata (time, repeat rule,
action and text) in NVS. It does not download or store reminder audio. When a
schedule with speech enabled fires, the firmware opens an audio session if
one is not already open, then sends this JSON request over the session's
normal text transport (WebSocket text frame or MQTT publish):

```json
{
  "session_id": "<active-session-id>",
  "type": "alarm",
  "state": "speak",
  "alarm_id": 12,
  "text": "Đến giờ uống thuốc"
}
```

The server must handle `type=alarm,state=speak`, synthesize `text` using its
configured TTS provider, and return the result using the existing Xiaozhi TTS
stream format:

1. JSON `{"type":"tts","state":"start"}`.
2. Zero or more binary Opus audio frames on the active channel. For a WebSocket
   protocol version 2 or 3 these use the same binary envelope as normal TTS;
   for MQTT the server streams through the established encrypted UDP channel.
3. JSON `{"type":"tts","state":"stop"}`.

The server may also send `{"type":"alarm","state":"error","message":"..."}`
if synthesis fails. The device returns to idle and closes the session on this
response. The device closes the alarm session after TTS stop as well. This
keeps the interaction one-way: no microphone recording is started for an
alarm.

This is a backend extension. A stock Xiaozhi server that does not implement
the `alarm/speak` request will ignore it and will not produce speech. Add a
handler to the server's WebSocket and/or MQTT session message dispatcher that
calls its existing TTS pipeline and streams the audio frames as above. No
firmware audio asset or persistent audio cache is involved; decoded playback
uses the existing bounded audio queues and RAM buffers.
