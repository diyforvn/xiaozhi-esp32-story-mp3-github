#pragma once

#include <cstdint>
#include <ctime>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

struct ScheduledAction {
    int id = 0;
    int hour = 0;
    int minute = 0;
    uint8_t days = 0;  // Sunday bit 0 through Saturday bit 6; zero means once.
    time_t once_at = 0;
    std::string message;
    bool speak = true;
    bool enabled = true;
    std::string device_tool;
    std::string device_args = "{}";
    int last_fired_ymd = 0;
    uint32_t legacy_alarm_id = 0;
};

class AutomationScheduleManager {
public:
    static AutomationScheduleManager& GetInstance();

    void Start();
    int Add(int hour, int minute, uint8_t days, const std::string& message, bool speak,
            const std::string& device_tool = "", const std::string& device_args = "{}");
    int AddAt(time_t when, const std::string& message, bool speak,
              const std::string& device_tool = "", const std::string& device_args = "{}");
    int AddIn(int minutes_from_now, const std::string& message, bool speak,
              const std::string& device_tool = "", const std::string& device_args = "{}");
    int Remove(int id);
    int RemoveAt(int hour, int minute);
    bool Clear();
    std::string ListJson();

    static bool ParseDays(const std::string& repeat, uint8_t& days);
    static std::string NowString();

private:
    AutomationScheduleManager() = default;

    struct Pending {
        int id;
        std::string message;
        bool speak;
        std::string device_tool;
        std::string device_args;
        time_t queued_at;
    };

    void RegisterMcpTools();
    void Load();
    bool SaveLocked();
    void MigrateLegacyAlarmsLocked();
    void Tick();
    void Fire(const Pending& action);
    static void TaskEntry(void* arg);

    std::mutex mutex_;
    std::vector<ScheduledAction> schedules_;
    std::deque<Pending> pending_;
    int next_id_ = 1;
    bool started_ = false;
    bool legacy_migration_pending_ = false;
};
