#include "scheduler_task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <ctime>
#include <cinttypes>

static const char* TAG = "SchedulerTask";
static AlarmManager* s_manager = nullptr;
static esp_timer_handle_t s_timer = nullptr;

// Trigger toi da 1 lan trong 60s cho cung 1 alarm, phong truong hop tick
// 30s roi vao dung phut do 2 lan lien tiep (vd 12:00:29 va 12:00:59 cung
// khop 12:00 neu logic chi so hour/minute).
static constexpr time_t kMinRetriggerIntervalSec = 60;

static bool DayMatches(const Alarm& alarm, const struct tm& t) {
    switch (alarm.repeat) {
        case RepeatMode::ONCE:
        case RepeatMode::DAILY:
            return true;
        case RepeatMode::WEEKDAYS:
            return t.tm_wday >= 1 && t.tm_wday <= 5;
        case RepeatMode::WEEKENDS:
            return t.tm_wday == 0 || t.tm_wday == 6;
        case RepeatMode::CUSTOM_DAYS:
            return (alarm.weekday_mask >> t.tm_wday) & 0x01;
    }
    return false;
}

static void CheckAlarmsCallback(void* /*arg*/) {
    if (!s_manager) return;

    time_t now;
    time(&now);
    struct tm timeinfo;
    localtime_r(&now, &timeinfo);

    // Nam < 2024 nghia la RTC/NTP chua sync -> bo qua, tranh trigger sai.
    if (timeinfo.tm_year < (2024 - 1900)) {
        return;
    }

    // Lay ban sao con tro de sua last_triggered/enabled truc tiep tren
    // vector goc (GetAlarms() tra ve const ref nen ta thao tac qua API).
    // Vi AlarmManager khong expose non-const iterator ra ngoai, ta duyet
    // qua index va goi lai cac ham public de dam bao invariant (persist...).
    const auto& alarms_snapshot = s_manager->GetAlarms();

    for (size_t i = 0; i < alarms_snapshot.size(); ++i) {
        // Copy ra ngoai vi TriggerAlarm() nhan Alarm& va vector goc co the
        // bi sua (enabled/last_triggered) trong chinh vong lap nay.
        Alarm alarm = alarms_snapshot[i];

        if (!alarm.enabled) continue;
        if (alarm.hour != timeinfo.tm_hour || alarm.minute != timeinfo.tm_min) continue;
        if (now - alarm.last_triggered < kMinRetriggerIntervalSec) continue;
        if (!DayMatches(alarm, timeinfo)) continue;

        ESP_LOGI(TAG, "Alarm id=%" PRIu32 " khop gio %02d:%02d, trigger", alarm.id,
                 timeinfo.tm_hour, timeinfo.tm_min);

        s_manager->TriggerAlarm(alarm);
        s_manager->TouchLastTriggered(alarm.id, now);

        if (alarm.repeat == RepeatMode::ONCE) {
            s_manager->SetEnabled(alarm.id, false);  // ghi NVS, tu tat sau khi bao 1 lan
        }
    }
}

void SchedulerTask::Start(AlarmManager* manager) {
    s_manager = manager;

    const esp_timer_create_args_t timer_args = {
        .callback = &CheckAlarmsCallback,
        .arg = nullptr,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "alarm_scheduler",
        .skip_unhandled_events = true,
    };

    esp_err_t err = esp_timer_create(&timer_args, &s_timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_create that bai: %s", esp_err_to_name(err));
        return;
    }

    err = esp_timer_start_periodic(s_timer, 30 * 1000 * 1000ULL);  // 30s, don vi us
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_timer_start_periodic that bai: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Scheduler da khoi dong, tick moi 30s");
}

void SchedulerTask::Stop() {
    if (s_timer) {
        esp_timer_stop(s_timer);
        esp_timer_delete(s_timer);
        s_timer = nullptr;
    }
    s_manager = nullptr;
}
