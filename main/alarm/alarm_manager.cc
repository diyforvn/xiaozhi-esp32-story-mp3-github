#include "alarm_manager.h"
#include "alarm_storage.h"
#include "esp_log.h"
#include "esp_random.h"
#include <cinttypes>

static const char* TAG = "AlarmManager";

AlarmManager g_alarm_manager;

RepeatMode ParseRepeatMode(const std::string& s) {
    if (s == "daily") return RepeatMode::DAILY;
    if (s == "weekdays") return RepeatMode::WEEKDAYS;
    if (s == "weekends") return RepeatMode::WEEKENDS;
    if (s == "custom") return RepeatMode::CUSTOM_DAYS;
    return RepeatMode::ONCE;
}

const char* RepeatModeToString(RepeatMode mode) {
    switch (mode) {
        case RepeatMode::DAILY: return "daily";
        case RepeatMode::WEEKDAYS: return "weekdays";
        case RepeatMode::WEEKENDS: return "weekends";
        case RepeatMode::CUSTOM_DAYS: return "custom";
        default: return "once";
    }
}

void AlarmManager::Init() {
    alarms_ = AlarmStorage::Load();

    // next_id_ phải lớn hơn id cao nhất đã có, để tránh trùng sau reboot.
    for (auto& a : alarms_) {
        if (a.id >= next_id_) next_id_ = a.id + 1;
    }

    event_queue_ = xQueueCreate(8, sizeof(AlarmEvent));
    if (!event_queue_) {
        ESP_LOGE(TAG, "Khong the tao alarm event queue");
    }

    ESP_LOGI(TAG, "Da load %d alarm tu NVS", (int)alarms_.size());
}

uint32_t AlarmManager::GenerateAlarmId() {
    return next_id_++;
}

uint32_t AlarmManager::AddAlarm(Alarm alarm) {
    alarm.id = GenerateAlarmId();
    alarm.last_triggered = 0;
    alarms_.push_back(alarm);
    PersistNow();
    ESP_LOGI(TAG, "Them alarm id=%" PRIu32 " luc %02u:%02u", alarm.id, alarm.hour, alarm.minute);
    return alarm.id;
}

bool AlarmManager::RemoveAlarm(uint32_t id) {
    for (auto it = alarms_.begin(); it != alarms_.end(); ++it) {
        if (it->id == id) {
            alarms_.erase(it);
            PersistNow();
            ESP_LOGI(TAG, "Da xoa alarm id=%" PRIu32, id);
            return true;
        }
    }
    return false;
}

bool AlarmManager::SetEnabled(uint32_t id, bool enabled) {
    for (auto& a : alarms_) {
        if (a.id == id) {
            a.enabled = enabled;
            PersistNow();
            return true;
        }
    }
    return false;
}

bool AlarmManager::TouchLastTriggered(uint32_t id, time_t when) {
    for (auto& a : alarms_) {
        if (a.id == id) {
            a.last_triggered = when;
            // Khong can PersistNow() moi lan trigger (tranh mai NVS voi
            // alarm lap moi phut/moi ngay) - last_triggered chi dung de
            // chong trigger trung trong RAM, mat khi reboot la chap nhan duoc.
            return true;
        }
    }
    return false;
}

int AlarmManager::FindAlarmIndexByTime(uint8_t hour, uint8_t minute) const {
    for (size_t i = 0; i < alarms_.size(); ++i) {
        if (alarms_[i].hour == hour && alarms_[i].minute == minute) {
            return (int)i;
        }
    }
    return -1;
}

void AlarmManager::TriggerAlarm(Alarm& alarm) {
    if (!event_queue_) return;

    AlarmEvent evt;
    evt.alarm_id = alarm.id;
    evt.message = alarm.message;
    evt.action = alarm.action;
    evt.mcp_tool_name = alarm.mcp_tool_name;
    evt.mcp_tool_args = alarm.mcp_tool_args;

    // Non-blocking send: neu queue day, bo qua thay vi block timer callback.
    if (xQueueSend(event_queue_, &evt, 0) != pdTRUE) {
        ESP_LOGW(TAG, "Alarm event queue day, bo qua trigger id=%" PRIu32, alarm.id);
    }
}

void AlarmManager::PersistNow() {
    AlarmStorage::Save(alarms_);
}