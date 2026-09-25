#pragma once

#include <cstdint>
#include <ctime>
#include <string>
#include <vector>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Chu kỳ lặp lại của một báo thức / lịch nhắc.
enum class RepeatMode : uint8_t {
    ONCE = 0,
    DAILY,
    WEEKDAYS,      // Thứ 2 - Thứ 6
    WEEKENDS,      // Thứ 7 - Chủ nhật
    CUSTOM_DAYS    // dùng weekday_mask, bit0 = Chủ nhật ... bit6 = Thứ 7
};

RepeatMode ParseRepeatMode(const std::string& s);
const char* RepeatModeToString(RepeatMode mode);

struct Alarm {
    uint32_t id = 0;
    uint8_t hour = 0;              // 0-23
    uint8_t minute = 0;            // 0-59
    RepeatMode repeat = RepeatMode::ONCE;
    uint8_t weekday_mask = 0;      // chỉ dùng khi repeat == CUSTOM_DAYS
    std::string message;           // nội dung hiển thị khi báo (fallback neu khong co device tool)
    std::string action = "notify"; // "notify" (chi hien thi) | "mcp_tool" (goi lai 1 MCP tool co san)
    std::string mcp_tool_name;     // ten tool, vd "self.light.turn_on" - chi dung khi action=="mcp_tool"
    std::string mcp_tool_args = "{}"; // JSON string tham so cho tool, vd {"r":255,"g":0,"b":0}
    bool enabled = true;
    time_t last_triggered = 0;     // chống trigger lặp trong cùng phút
};

// Event được đẩy vào queue khi 1 alarm đến giờ, để xử lý ở task khác
// (tránh làm nặng timer callback / đụng LVGL không thread-safe).
struct AlarmEvent {
    uint32_t alarm_id;
    std::string message;
    std::string action;
    std::string mcp_tool_name;
    std::string mcp_tool_args;
};

class AlarmManager {
public:
    // Gọi 1 lần lúc khởi động app, sau khi NVS đã init.
    void Init();

    // CRUD, dùng bởi MCP tool handlers.
    uint32_t AddAlarm(Alarm alarm);          // trả về id đã gán
    bool RemoveAlarm(uint32_t id);
    bool SetEnabled(uint32_t id, bool enabled);
    bool TouchLastTriggered(uint32_t id, time_t when);
    const std::vector<Alarm>& GetAlarms() const { return alarms_; }

    // Tìm alarm gần đúng theo giờ:phút, dùng khi user nói "xóa báo thức lúc 3 giờ"
    // mà không nhớ id.
    int FindAlarmIndexByTime(uint8_t hour, uint8_t minute) const;

    // Được gọi bởi SchedulerTask khi 1 alarm khớp giờ hiện tại.
    void TriggerAlarm(Alarm& alarm);

    // Lưu ngay danh sách hiện tại xuống NVS.
    void PersistNow();

    // Queue chứa các AlarmEvent chờ xử lý bởi app task (audio/UI).
    QueueHandle_t event_queue() const { return event_queue_; }

private:
    std::vector<Alarm> alarms_;
    QueueHandle_t event_queue_ = nullptr;
    uint32_t next_id_ = 1;

    uint32_t GenerateAlarmId();
};

// Instance toàn cục dùng chung giữa MCP tool handlers và scheduler task.
extern AlarmManager g_alarm_manager;