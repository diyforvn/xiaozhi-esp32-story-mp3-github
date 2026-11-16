# Automation

Automation is initialized once by `InitAutomationSubsystem()` from
`main.cc`. It has two functional modules:

- `schedule/` is the single scheduler for alarms and reminders. One schedule
  can run at a clock time, repeat on selected weekdays, run on a specific date,
  or run after a delay. At its due time it can show a notification, request
  server TTS, and/or invoke an existing MCP device tool.
- `scene/` stores and runs an ordered sequence of MCP tools. A schedule can
  invoke `self.scene.run` as its device action.

Use `self.schedule.*` as the canonical scheduling API. The older
`self.alarm.*` and `self.reminder.*` tools remain aliases backed by the same
manager and NVS list. Existing reminder data remains in the `reminder` NVS
namespace. Existing alarm data in `alarms` is migrated on startup and removed
from the legacy key only after it has been saved in the unified list. Scene
data remains in its existing `scenes` NVS namespace.

Server generated reminder speech uses the protocol in
[`../../docs/alarm_server_tts.md`](../../docs/alarm_server_tts.md).
