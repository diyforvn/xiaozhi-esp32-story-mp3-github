#include "automation_schedule_manager.h"

#include "legacy_alarm_storage.h"

#include <algorithm>
#include <cctype>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <cJSON.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs.h>
#include <nvs_flash.h>

#include "application.h"
#include "mcp_server.h"
#include "assets/lang_config.h"

#define TAG "AutomationSchedule"

// ---- Điều chỉnh theo phiên bản firmware của bạn --------------------------
// Âm báo: bản mới dùng Lang::Sounds::OGG_POPUP, bản cũ dùng Lang::Sounds::P3_POPUP.
#ifndef REMINDER_SOUND
#define REMINDER_SOUND Lang::Sounds::OGG_POPUP
#endif
// ---------------------------------------------------------------------------

static constexpr const char* kNvsNamespace = "reminder";
static constexpr const char* kNvsKey = "list";
static constexpr int kTickSeconds = 5;
static constexpr int kOnceGraceSec = 300;     // trễ tối đa 5 phút vẫn nhắc bù (một lần)
static constexpr int kPendingMaxSec = 600;    // chờ thiết bị rảnh tối đa 10 phút
static constexpr int kMaxSchedules = 20;
static constexpr int kVoiceWaitSec = 15;      // chờ server phát giọng nói tối đa 15 giây
static constexpr time_t kMinValidEpoch = 1700000000;  // ~2023, coi như đã có giờ thật
static constexpr size_t kMaxStoredJsonSize = 3900;

static void InvokeScheduledTool(int id, const std::string& tool_name, const std::string& args_json) {
    cJSON* args = cJSON_Parse(args_json.empty() ? "{}" : args_json.c_str());
    if (!cJSON_IsObject(args)) {
        cJSON_Delete(args);
        ESP_LOGE(TAG, "Schedule #%d has invalid arguments for tool %s", id, tool_name.c_str());
        return;
    }
    McpServer::GetInstance().InvokeToolDirect(
        tool_name, args, [id, tool_name](bool success, const std::string& message) {
            auto& app = Application::GetInstance();
            app.Schedule([&app, id, tool_name, success, message]() {
                std::string text = success
                    ? "Đã thực hiện lịch #" + std::to_string(id) + ": " + tool_name
                    : "Lỗi lịch #" + std::to_string(id) + " " + tool_name + ": " + message;
                app.Alert(success ? "Lịch thiết bị" : "Lỗi lịch thiết bị", text.c_str(),
                          success ? "happy" : "sad", std::string_view());
            });
        });
    cJSON_Delete(args);
}

static bool ParseLocalDateTime(const std::string& date, int hour, int minute, time_t& when) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') return false;
    for (size_t i = 0; i < date.size(); ++i) {
        if (i != 4 && i != 7 && !std::isdigit(static_cast<unsigned char>(date[i]))) return false;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    if (std::sscanf(date.c_str(), "%4d-%2d-%2d", &year, &month, &day) != 3 ||
        year < 2023 || month < 1 || month > 12 || day < 1 || day > 31) {
        return false;
    }

    struct tm requested = {};
    requested.tm_year = year - 1900;
    requested.tm_mon = month - 1;
    requested.tm_mday = day;
    requested.tm_hour = hour;
    requested.tm_min = minute;
    requested.tm_isdst = -1;
    when = mktime(&requested);
    if (when < kMinValidEpoch) return false;

    struct tm normalized;
    if (localtime_r(&when, &normalized) == nullptr) return false;
    return normalized.tm_year == year - 1900 && normalized.tm_mon == month - 1 &&
           normalized.tm_mday == day && normalized.tm_hour == hour && normalized.tm_min == minute;
}

AutomationScheduleManager& AutomationScheduleManager::GetInstance() {
    static AutomationScheduleManager instance;
    return instance;
}

// ============================ Tiện ích =====================================

bool AutomationScheduleManager::ParseDays(const std::string& repeat, uint8_t& days) {
    std::string s;
    for (char c : repeat) {
        if (!isspace(static_cast<unsigned char>(c))) s += static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    if (s.empty() || s == "once") {
        days = 0;
        return true;
    }
    if (s == "daily" || s == "everyday") {
        days = 0x7F;
        return true;
    }
    if (s == "weekdays") {
        days = 0x3E;  // T2..T6
        return true;
    }
    if (s == "weekends") {
        days = 0x41;  // CN + T7
        return true;
    }
    if (s.front() == ',' || s.back() == ',' || s.find(",,") != std::string::npos) return false;

    uint8_t mask = 0;
    std::stringstream ss(s);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
        if (tok == "sun" || tok == "cn") mask |= 1 << 0;
        else if (tok == "mon" || tok == "t2") mask |= 1 << 1;
        else if (tok == "tue" || tok == "t3") mask |= 1 << 2;
        else if (tok == "wed" || tok == "t4") mask |= 1 << 3;
        else if (tok == "thu" || tok == "t5") mask |= 1 << 4;
        else if (tok == "fri" || tok == "t6") mask |= 1 << 5;
        else if (tok == "sat" || tok == "t7") mask |= 1 << 6;
        else return false;
    }
    if (mask == 0) return false;
    days = mask;
    return true;
}

std::string AutomationScheduleManager::NowString() {
    time_t now = time(nullptr);
    if (now < kMinValidEpoch) return "Chưa đồng bộ được giờ";
    struct tm t;
    localtime_r(&now, &t);
    static const char* kWday[] = {"Chủ nhật", "Thứ 2", "Thứ 3", "Thứ 4", "Thứ 5", "Thứ 6", "Thứ 7"};
    char buf[96];
    snprintf(buf, sizeof(buf), "%s, %02d/%02d/%04d, %02d:%02d:%02d", kWday[t.tm_wday], t.tm_mday,
             t.tm_mon + 1, t.tm_year + 1900, t.tm_hour, t.tm_min, t.tm_sec);
    return buf;
}

// ============================ NVS ==========================================

void AutomationScheduleManager::Load() {
    std::lock_guard<std::mutex> lock(mutex_);
    schedules_.clear();

    nvs_handle_t h;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READONLY, &h);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        MigrateLegacyAlarmsLocked();
        return;
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Không mở được NVS để nạp nhắc nhở: %s", esp_err_to_name(err));
        return;
    }

    size_t len = 0;
    err = nvs_get_str(h, kNvsKey, nullptr, &len);
    if (err == ESP_OK && len > 1) {
        std::string json(len, '\0');
        err = nvs_get_str(h, kNvsKey, json.data(), &len);
        if (err == ESP_OK) {
            cJSON* arr = cJSON_Parse(json.c_str());
            if (cJSON_IsArray(arr)) {
                cJSON* it = nullptr;
                cJSON_ArrayForEach(it, arr) {
                    ScheduledAction r;
                    cJSON* v;
                    if ((v = cJSON_GetObjectItem(it, "id"))) r.id = v->valueint;
                    if ((v = cJSON_GetObjectItem(it, "h"))) r.hour = v->valueint;
                    if ((v = cJSON_GetObjectItem(it, "m"))) r.minute = v->valueint;
                    if ((v = cJSON_GetObjectItem(it, "d"))) r.days = static_cast<uint8_t>(v->valueint);
                    if ((v = cJSON_GetObjectItem(it, "at"))) r.once_at = static_cast<time_t>(v->valuedouble);
                    if ((v = cJSON_GetObjectItem(it, "msg")) && cJSON_IsString(v)) r.message = v->valuestring;
                    if ((v = cJSON_GetObjectItem(it, "sp"))) r.speak = cJSON_IsTrue(v);
                    if ((v = cJSON_GetObjectItem(it, "en"))) r.enabled = cJSON_IsTrue(v);
                    if ((v = cJSON_GetObjectItem(it, "tool")) && cJSON_IsString(v)) r.device_tool = v->valuestring;
                    if ((v = cJSON_GetObjectItem(it, "args")) && cJSON_IsString(v)) r.device_args = v->valuestring;
                    if ((v = cJSON_GetObjectItem(it, "last"))) r.last_fired_ymd = v->valueint;
                    if ((v = cJSON_GetObjectItem(it, "legacy_alarm_id"))) r.legacy_alarm_id = static_cast<uint32_t>(v->valueint);
                    if (r.id > 0) {
                        next_id_ = std::max(next_id_, r.id + 1);
                        schedules_.push_back(std::move(r));
                    }
                }
            } else {
                ESP_LOGE(TAG, "Dữ liệu nhắc nhở trong NVS không phải JSON array hợp lệ");
            }
            if (arr) cJSON_Delete(arr);
        } else {
            ESP_LOGE(TAG, "Không đọc được danh sách nhắc nhở từ NVS: %s", esp_err_to_name(err));
        }
    } else if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Không đọc được danh sách nhắc nhở từ NVS: %s", esp_err_to_name(err));
    }
    nvs_close(h);
    ESP_LOGI(TAG, "Đã nạp %d nhắc nhở", static_cast<int>(schedules_.size()));
    MigrateLegacyAlarmsLocked();
}

bool AutomationScheduleManager::SaveLocked() {
    cJSON* arr = cJSON_CreateArray();
    if (!arr) {
        ESP_LOGE(TAG, "Không cấp phát được JSON khi lưu nhắc nhở");
        return false;
    }
    for (const auto& r : schedules_) {
        cJSON* o = cJSON_CreateObject();
        if (!o) {
            cJSON_Delete(arr);
            ESP_LOGE(TAG, "Không cấp phát được JSON khi lưu nhắc nhở");
            return false;
        }
        cJSON_AddNumberToObject(o, "id", r.id);
        cJSON_AddNumberToObject(o, "h", r.hour);
        cJSON_AddNumberToObject(o, "m", r.minute);
        cJSON_AddNumberToObject(o, "d", r.days);
        cJSON_AddNumberToObject(o, "at", static_cast<double>(r.once_at));
        cJSON_AddStringToObject(o, "msg", r.message.c_str());
        cJSON_AddBoolToObject(o, "sp", r.speak);
        cJSON_AddBoolToObject(o, "en", r.enabled);
        cJSON_AddStringToObject(o, "tool", r.device_tool.c_str());
        cJSON_AddStringToObject(o, "args", r.device_args.c_str());
        cJSON_AddNumberToObject(o, "last", r.last_fired_ymd);
        cJSON_AddNumberToObject(o, "legacy_alarm_id", r.legacy_alarm_id);
        cJSON_AddItemToArray(arr, o);
    }
    char* json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (!json) {
        ESP_LOGE(TAG, "Không serialize được danh sách nhắc nhở");
        return false;
    }
    if (std::strlen(json) >= kMaxStoredJsonSize) {
        ESP_LOGE(TAG, "Danh sách nhắc nhở vượt giới hạn lưu trữ NVS");
        cJSON_free(json);
        return false;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(kNvsNamespace, NVS_READWRITE, &h);
    if (err == ESP_OK) {
        err = nvs_set_str(h, kNvsKey, json);
        if (err == ESP_OK) err = nvs_commit(h);
        nvs_close(h);
    } else {
        ESP_LOGE(TAG, "Không mở được NVS để lưu: %s", esp_err_to_name(err));
    }
    cJSON_free(json);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Không lưu được nhắc nhở vào NVS: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

void AutomationScheduleManager::MigrateLegacyAlarmsLocked() {
    const auto old_alarms = LegacyAlarmStorage::Load();
    if (old_alarms.empty()) {
        legacy_migration_pending_ = false;
        return;
    }
    legacy_migration_pending_ = true;

    const size_t original_size = schedules_.size();
    bool migration_complete = true;
    for (const auto& old : old_alarms) {
        if (!old.enabled) continue;

        uint8_t days = 0;
        if (old.repeat == "daily") days = 0x7F;
        else if (old.repeat == "weekdays") days = 0x3E;
        else if (old.repeat == "weekends") days = 0x41;
        else if (old.repeat == "custom") days = old.weekday_mask;

        const std::string tool = old.action == "mcp_tool" ? old.device_tool : "";
        const std::string args = old.device_args.empty() ? "{}" : old.device_args;
        if (old.repeat == "custom" && days == 0) {
            ESP_LOGW(TAG, "Skipping legacy alarm id=%" PRIu32 " with an empty custom weekday mask", old.id);
            continue;
        }
        const bool exists = std::any_of(schedules_.begin(), schedules_.end(), [&](const ScheduledAction& item) {
            return item.legacy_alarm_id == old.id;
        });
        if (exists) continue;
        if (schedules_.size() >= kMaxSchedules) {
            migration_complete = false;
            continue;
        }

        ScheduledAction item;
        item.id = next_id_++;
        item.hour = old.hour;
        item.minute = old.minute;
        item.days = days;
        item.message = old.message;
        item.speak = true;
        item.device_tool = tool;
        item.device_args = args;
        item.legacy_alarm_id = old.id;
        // Resolve a one-time alarm after SNTP has supplied a valid clock.
        item.once_at = 0;
        schedules_.push_back(std::move(item));
    }

    if (schedules_.size() != original_size && !SaveLocked()) {
        schedules_.resize(original_size);
        next_id_ = 1;
        for (const auto& item : schedules_) next_id_ = std::max(next_id_, item.id + 1);
        ESP_LOGE(TAG, "Legacy alarm migration could not be persisted; keeping old NVS data");
        return;
    }

    if (!migration_complete) {
        ESP_LOGW(TAG, "Unified schedule store is full; keeping remaining legacy alarm entries in NVS");
        return;
    }

    if (!LegacyAlarmStorage::Clear()) {
        ESP_LOGW(TAG, "Could not clear migrated legacy alarm NVS; duplicate-safe migration will retry");
    } else {
        legacy_migration_pending_ = false;
        ESP_LOGI(TAG, "Migrated legacy alarm entries into the unified schedule store");
    }
}

// ============================ API ==========================================

int AutomationScheduleManager::Add(int hour, int minute, uint8_t days, const std::string& message, bool speak,
                                  const std::string& device_tool, const std::string& device_args) {
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59 || message.empty() ||
        (days & 0x80) != 0) return -1;

    ScheduledAction r;
    r.hour = hour;
    r.minute = minute;
    r.days = days;
    r.message = message;
    r.speak = speak;
    r.device_tool = device_tool;
    r.device_args = device_args.empty() ? "{}" : device_args;

    if (days == 0) {
        // Nhắc một lần: tính sẵn lần xuất hiện kế tiếp của hh:mm
        time_t now = time(nullptr);
        if (now < kMinValidEpoch) return -2;  // chưa có giờ thật
        struct tm t;
        localtime_r(&now, &t);
        t.tm_hour = hour;
        t.tm_min = minute;
        t.tm_sec = 0;
        t.tm_isdst = -1;
        time_t at = mktime(&t);
        if (at <= now) at += 24 * 3600;
        r.once_at = at;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<int>(schedules_.size()) >= kMaxSchedules) return -3;
    r.id = next_id_++;
    schedules_.push_back(r);
    if (!SaveLocked()) {
        schedules_.pop_back();
        --next_id_;
        return -4;
    }
    ESP_LOGI(TAG, "Thêm nhắc #%d lúc %02d:%02d (days=0x%02X): %s", r.id, hour, minute, days, message.c_str());
    return r.id;
}

int AutomationScheduleManager::AddAt(time_t when, const std::string& message, bool speak,
                                     const std::string& device_tool, const std::string& device_args) {
    time_t now = time(nullptr);
    if (message.empty() || now < kMinValidEpoch) return now < kMinValidEpoch ? -2 : -1;
    if (when <= now) return -1;

    struct tm scheduled;
    if (localtime_r(&when, &scheduled) == nullptr) return -1;

    ScheduledAction r;
    r.hour = scheduled.tm_hour;
    r.minute = scheduled.tm_min;
    r.once_at = when;
    r.message = message;
    r.speak = speak;
    r.device_tool = device_tool;
    r.device_args = device_args.empty() ? "{}" : device_args;

    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<int>(schedules_.size()) >= kMaxSchedules) return -3;
    r.id = next_id_++;
    schedules_.push_back(r);
    if (!SaveLocked()) {
        schedules_.pop_back();
        --next_id_;
        return -4;
    }
    ESP_LOGI(TAG, "Thêm nhắc #%d lúc %04d-%02d-%02d %02d:%02d: %s", r.id,
             scheduled.tm_year + 1900, scheduled.tm_mon + 1, scheduled.tm_mday,
             r.hour, r.minute, message.c_str());
    return r.id;
}

int AutomationScheduleManager::AddIn(int minutes_from_now, const std::string& message, bool speak,
                                     const std::string& device_tool, const std::string& device_args) {
    if (minutes_from_now < 1 || minutes_from_now > 7 * 24 * 60 || message.empty()) return -1;
    time_t now = time(nullptr);
    if (now < kMinValidEpoch) return -2;

    time_t at = now + static_cast<time_t>(minutes_from_now) * 60;
    struct tm t;
    localtime_r(&at, &t);

    ScheduledAction r;
    r.hour = t.tm_hour;
    r.minute = t.tm_min;
    r.days = 0;
    r.once_at = at;
    r.message = message;
    r.speak = speak;
    r.device_tool = device_tool;
    r.device_args = device_args.empty() ? "{}" : device_args;

    std::lock_guard<std::mutex> lock(mutex_);
    if (static_cast<int>(schedules_.size()) >= kMaxSchedules) return -3;
    r.id = next_id_++;
    schedules_.push_back(r);
    if (!SaveLocked()) {
        schedules_.pop_back();
        --next_id_;
        return -4;
    }
    ESP_LOGI(TAG, "Thêm nhắc #%d sau %d phút: %s", r.id, minutes_from_now, message.c_str());
    return r.id;
}

int AutomationScheduleManager::Remove(int id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(schedules_.begin(), schedules_.end(),
                           [id](const ScheduledAction& r) { return r.id == id; });
    if (it == schedules_.end()) return 0;
    size_t index = static_cast<size_t>(it - schedules_.begin());
    ScheduledAction removed = *it;
    schedules_.erase(it);
    if (!SaveLocked()) {
        schedules_.insert(schedules_.begin() + index, std::move(removed));
        return -1;
    }
    return 1;
}

int AutomationScheduleManager::RemoveAt(int hour, int minute) {
    if (hour < 0 || hour > 23 || minute < 0 || minute > 59) return -1;
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(schedules_.begin(), schedules_.end(), [hour, minute](const ScheduledAction& item) {
        return item.hour == hour && item.minute == minute;
    });
    if (it == schedules_.end()) return 0;
    const size_t index = static_cast<size_t>(it - schedules_.begin());
    ScheduledAction removed = *it;
    schedules_.erase(it);
    if (!SaveLocked()) {
        schedules_.insert(schedules_.begin() + index, std::move(removed));
        return -1;
    }
    return 1;
}

bool AutomationScheduleManager::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto previous_schedules = schedules_;
    auto previous_pending = pending_;
    schedules_.clear();
    pending_.clear();
    if (!SaveLocked()) {
        schedules_ = std::move(previous_schedules);
        pending_ = std::move(previous_pending);
        return false;
    }
    if (!LegacyAlarmStorage::Clear()) {
        schedules_ = std::move(previous_schedules);
        pending_ = std::move(previous_pending);
        SaveLocked();
        ESP_LOGE(TAG, "Could not clear legacy alarm data while clearing unified schedules");
        return false;
    }
    return true;
}

std::string AutomationScheduleManager::ListJson() {
    std::lock_guard<std::mutex> lock(mutex_);
    cJSON* arr = cJSON_CreateArray();
    for (const auto& r : schedules_) {
        cJSON* o = cJSON_CreateObject();
        cJSON_AddNumberToObject(o, "id", r.id);
        cJSON_AddNumberToObject(o, "hour", r.hour);
        cJSON_AddNumberToObject(o, "minute", r.minute);
        char tbuf[8];
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d", r.hour, r.minute);
        cJSON_AddStringToObject(o, "time", tbuf);
        const char* repeat = r.days == 0 ? "once" : r.days == 0x7F ? "daily" :
                             r.days == 0x3E ? "weekdays" : r.days == 0x41 ? "weekends" : "custom";
        cJSON_AddStringToObject(o, "repeat", repeat);
        cJSON_AddNumberToObject(o, "days_mask", r.days);
        if (r.days == 0 && r.once_at > 0) {
            struct tm scheduled;
            if (localtime_r(&r.once_at, &scheduled) != nullptr) {
                char date_buf[64];
                snprintf(date_buf, sizeof(date_buf), "%04d-%02d-%02d %02d:%02d",
                         scheduled.tm_year + 1900, scheduled.tm_mon + 1, scheduled.tm_mday,
                         scheduled.tm_hour, scheduled.tm_min);
                cJSON_AddStringToObject(o, "scheduled_at", date_buf);
            }
        }
        cJSON_AddStringToObject(o, "message", r.message.c_str());
        cJSON_AddStringToObject(o, "action", r.device_tool.empty() ? "notify" : "mcp_tool");
        cJSON_AddBoolToObject(o, "speak", r.speak);
        cJSON_AddStringToObject(o, "device_tool", r.device_tool.c_str());
        cJSON_AddStringToObject(o, "device_args", r.device_args.c_str());
        cJSON_AddBoolToObject(o, "enabled", r.enabled);
        cJSON_AddItemToArray(arr, o);
    }
    char* s = cJSON_PrintUnformatted(arr);
    std::string out = s ? s : "[]";
    if (s) cJSON_free(s);
    cJSON_Delete(arr);
    return out;
}

// ============================ Vòng kiểm tra =================================

void AutomationScheduleManager::TaskEntry(void* arg) {
    auto* self = static_cast<AutomationScheduleManager*>(arg);
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(kTickSeconds * 1000));
        self->Tick();
    }
}

void AutomationScheduleManager::Tick() {
    time_t now = time(nullptr);
    if (now < kMinValidEpoch) return;  // chưa đồng bộ SNTP

    struct tm t;
    localtime_r(&now, &t);
    int ymd = (t.tm_year + 1900) * 10000 + (t.tm_mon + 1) * 100 + t.tm_mday;

    Pending to_fire{};
    bool has_fire = false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        bool changed = false;

        if (legacy_migration_pending_ && schedules_.size() < kMaxSchedules) {
            MigrateLegacyAlarmsLocked();
        }

        for (auto it = schedules_.begin(); it != schedules_.end();) {
            bool due = false;
            bool remove = false;

            if (!it->enabled) {
                ++it;
                continue;
            }

            if (it->days == 0) {
                if (it->once_at == 0) {
                    struct tm next = t;
                    next.tm_hour = it->hour;
                    next.tm_min = it->minute;
                    next.tm_sec = 0;
                    next.tm_isdst = -1;
                    it->once_at = mktime(&next);
                    if (it->once_at <= now) it->once_at += 24 * 3600;
                    changed = true;
                } else if (now >= it->once_at) {
                    due = (now - it->once_at) <= kOnceGraceSec;
                    remove = true;  // dù nhắc hay quá hạn đều xoá
                }
            } else if (((it->days >> t.tm_wday) & 1) && t.tm_hour == it->hour && t.tm_min == it->minute &&
                       it->last_fired_ymd != ymd) {
                due = true;
                it->last_fired_ymd = ymd;
                changed = true;
            }

            if (due) pending_.push_back({it->id, it->message, it->speak,
                                         it->device_tool, it->device_args, now});

            if (remove) {
                it = schedules_.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }

        // Bỏ các nhắc chờ quá lâu
        while (!pending_.empty() && now - pending_.front().queued_at > kPendingMaxSec) {
            ESP_LOGW(TAG, "Bỏ nhắc quá hạn chờ: %s", pending_.front().message.c_str());
            pending_.pop_front();
        }

        // Chỉ phát khi thiết bị rảnh, mỗi lượt 1 nhắc
        if (!pending_.empty() && Application::GetInstance().GetDeviceState() == kDeviceStateIdle) {
            to_fire = pending_.front();
            pending_.pop_front();
            has_fire = true;
        }

        if (changed) SaveLocked();
    }

    if (has_fire) Fire(to_fire);
}

void AutomationScheduleManager::Fire(const Pending& p) {
    const std::string msg = p.message.empty() ? "Đến giờ thực hiện lịch!" : p.message;
    ESP_LOGI(TAG, "Đến hạn schedule #%d: %s", p.id, msg.c_str());

    // 1) Chuông + hiện chữ trên màn hình (chạy offline)
    Application::GetInstance().Schedule([msg]() {
        Application::GetInstance().Alert("Nhắc nhở", msg.c_str(), "happy", REMINDER_SOUND);
    });

    if (!p.device_tool.empty()) {
        InvokeScheduledTool(p.id, p.device_tool, p.device_args);
    }

    // 2) Nhờ server đọc thành tiếng
    if (p.speak) {
        vTaskDelay(pdMS_TO_TICKS(1500));  // đợi chuông phát xong (task này tự chặn, không chặn main loop)
        auto& app = Application::GetInstance();
        if (app.GetDeviceState() != kDeviceStateIdle) {
            ESP_LOGI(TAG, "Bỏ đọc nhắc vì thiết bị đang bận: %s", msg.c_str());
            return;
        }
        ESP_LOGI(TAG, "Yêu cầu server đọc schedule id=%d: %s", p.id, msg.c_str());
        app.RequestScheduledTts(static_cast<uint32_t>(p.id), msg);

        // Watchdog: chờ server trả giọng nói (thiết bị chuyển sang Speaking).
        bool spoke = false;
        DeviceState last_state = app.GetDeviceState();
        for (int i = 0; i < kVoiceWaitSec * 2; ++i) {
            vTaskDelay(pdMS_TO_TICKS(500));
            DeviceState st = app.GetDeviceState();
            if (st != last_state) {
                ESP_LOGI(TAG, "State: %d -> %d (sau %d ms)", static_cast<int>(last_state),
                         static_cast<int>(st), (i + 1) * 500);
                last_state = st;
            }
            if (st == kDeviceStateSpeaking) {
                spoke = true;
                break;
            }
        }

        if (spoke) {
            // Đợi server đọc xong rồi mới thoát (tối đa 60 giây)
            for (int i = 0; i < 120 && app.GetDeviceState() == kDeviceStateSpeaking; ++i) {
                vTaskDelay(pdMS_TO_TICKS(500));
            }
            return;
        }

        // Server không đọc hoặc chưa hỗ trợ alarm/speak: đóng phiên trước khi fallback.
        ESP_LOGW(TAG, "Server không phát giọng nói sau %d giây (state=%d). Dùng chuông dự phòng.",
                 kVoiceWaitSec, static_cast<int>(app.GetDeviceState()));
        if (app.GetDeviceState() != kDeviceStateIdle) {
            app.CancelScheduledTts();
        }
        for (int i = 0; i < 10 && app.GetDeviceState() != kDeviceStateIdle; ++i) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
        for (int n = 0; n < 2; ++n) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            app.Schedule([msg]() {
                Application::GetInstance().Alert("Nhắc nhở", msg.c_str(), "happy", REMINDER_SOUND);
            });
        }
    }
}

// ============================ MCP tools =====================================

void AutomationScheduleManager::RegisterMcpTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.reminder.get_time",
                "Lấy ngày giờ hiện tại của thiết bị (giờ Việt Nam). "
                "PHẢI gọi tool này trước khi đặt nhắc nhở khi người dùng nói giờ tương đối như "
                "'ngày mai', 'tối nay', 'thứ hai tuần sau'.",
                PropertyList(), [](const PropertyList&) -> ReturnValue { return NowString(); });

    mcp.AddTool("self.reminder.add",
                "Đặt nhắc nhở vào một giờ cố định trong ngày. "
                "repeat: 'once' (một lần, vào lần kế tiếp của giờ đó), 'daily' (mỗi ngày), "
                "'weekdays' (T2-T6), 'weekends' (T7+CN), hoặc danh sách thứ cách nhau dấu phẩy "
                "như 't2,t4,t6' / 'mon,wed,fri' / 'cn'. Với lịch một lần vào ngày cụ thể, "
                "truyền date theo YYYY-MM-DD; hãy gọi self.reminder.get_time trước để xác định ngày. "
                "message là nội dung nhắc, viết ngắn gọn; có thể truyền device_tool/device_args để chạy hành động MCP "
                "khi đến hạn, và speak=false nếu không cần giọng đọc.",
                PropertyList({Property("hour", kPropertyTypeInteger, 0, 23),
                              Property("minute", kPropertyTypeInteger, 0, 59),
                              Property("message", kPropertyTypeString),
                              Property("repeat", kPropertyTypeString, std::string("once")),
                              Property("date", kPropertyTypeString, std::string("")),
                              Property("speak", kPropertyTypeBoolean, true),
                              Property("device_tool", kPropertyTypeString, std::string("")),
                              Property("device_args", kPropertyTypeString, std::string("{}"))}),
                [](const PropertyList& props) -> ReturnValue {
                    const int hour = props["hour"].value<int>();
                    const int minute = props["minute"].value<int>();
                    const std::string repeat = props["repeat"].value<std::string>();
                    const std::string message = props["message"].value<std::string>();
                    uint8_t days = 0;
                    if (!AutomationScheduleManager::ParseDays(repeat, days)) {
                        throw std::runtime_error("Chu kỳ lặp không hợp lệ");
                    }

                    int id;
                    const std::string date = props["date"].value<std::string>();
                    const std::string tool = props["device_tool"].value<std::string>();
                    const std::string args = props["device_args"].value<std::string>();
                    if (!date.empty()) {
                        if (days != 0) {
                            throw std::runtime_error("Không thể kết hợp ngày cụ thể với lịch lặp");
                        }
                        time_t when;
                        if (!ParseLocalDateTime(date, hour, minute, when)) {
                            throw std::runtime_error("Ngày không hợp lệ; dùng định dạng YYYY-MM-DD");
                        }
                        if (when <= time(nullptr)) {
                            throw std::runtime_error("Thời điểm nhắc phải ở trong tương lai");
                        }
                        id = AutomationScheduleManager::GetInstance().AddAt(
                            when, message, props["speak"].value<bool>(), tool, args);
                    } else {
                        id = AutomationScheduleManager::GetInstance().Add(
                            hour, minute, days, message, props["speak"].value<bool>(), tool, args);
                    }
                    if (id == -2) throw std::runtime_error("Thiết bị chưa đồng bộ được giờ, thử lại sau");
                    if (id == -3) throw std::runtime_error("Đã đủ số nhắc nhở tối đa");
                    if (id == -4) throw std::runtime_error("Không lưu được nhắc nhở vào bộ nhớ thiết bị");
                    if (id < 0) throw std::runtime_error("Tham số không hợp lệ");
                    return "Đã đặt nhắc nhở #" + std::to_string(id);
                });

    mcp.AddTool("self.reminder.add_in",
                "Đặt nhắc nhở sau một khoảng thời gian tính từ bây giờ, ví dụ 'nhắc tôi sau 30 phút'. "
                "minutes là số phút (1 đến 10080).",
                PropertyList({Property("minutes", kPropertyTypeInteger, 1, 10080),
                              Property("message", kPropertyTypeString),
                              Property("speak", kPropertyTypeBoolean, true),
                              Property("device_tool", kPropertyTypeString, std::string("")),
                              Property("device_args", kPropertyTypeString, std::string("{}"))}),
                [](const PropertyList& props) -> ReturnValue {
                    int id = AutomationScheduleManager::GetInstance().AddIn(
                        props["minutes"].value<int>(), props["message"].value<std::string>(),
                        props["speak"].value<bool>(), props["device_tool"].value<std::string>(),
                        props["device_args"].value<std::string>());
                    if (id == -2) throw std::runtime_error("Thiết bị chưa đồng bộ được giờ, thử lại sau");
                    if (id == -3) throw std::runtime_error("Đã đủ số nhắc nhở tối đa");
                    if (id == -4) throw std::runtime_error("Không lưu được nhắc nhở vào bộ nhớ thiết bị");
                    if (id < 0) throw std::runtime_error("Tham số không hợp lệ");
                    return "Đã đặt nhắc nhở #" + std::to_string(id);
                });

    mcp.AddTool("self.reminder.list", "Liệt kê tất cả nhắc nhở đang có (id, giờ, lặp lại, nội dung).",
                PropertyList(),
                [](const PropertyList&) -> ReturnValue { return AutomationScheduleManager::GetInstance().ListJson(); });

    mcp.AddTool("self.reminder.delete", "Xoá một nhắc nhở theo id (lấy id từ self.reminder.list).",
                PropertyList({Property("id", kPropertyTypeInteger, 1, 100000)}),
                [](const PropertyList& props) -> ReturnValue {
                    int result = AutomationScheduleManager::GetInstance().Remove(props["id"].value<int>());
                    if (result == 0)
                        throw std::runtime_error("Không tìm thấy nhắc nhở này");
                    if (result < 0)
                        throw std::runtime_error("Không lưu được thay đổi vào bộ nhớ thiết bị");
                    return true;
                });

    mcp.AddTool("self.reminder.clear", "Xoá toàn bộ nhắc nhở. Chỉ dùng khi người dùng yêu cầu rõ ràng.",
                PropertyList(), [](const PropertyList&) -> ReturnValue {
                    if (!AutomationScheduleManager::GetInstance().Clear())
                        throw std::runtime_error("Không lưu được thay đổi vào bộ nhớ thiết bị");
                    return true;
                });

    // Canonical unified API. The old alarm/reminder names below remain aliases.
    mcp.AddTool("self.schedule.now", "Lấy ngày giờ hiện tại của thiết bị để đặt lịch chính xác.",
                PropertyList(), [](const PropertyList&) -> ReturnValue { return NowString(); });
    mcp.AddTool("self.schedule.add",
                "Tạo một lịch duy nhất: đặt theo giờ lặp/giờ cụ thể, theo ngày YYYY-MM-DD, hoặc sau after_minutes. "
                "Mỗi lịch có thể đọc nội dung bằng TTS và/hoặc gọi device_tool với device_args khi đến hạn. "
                "Dùng repeat daily/weekdays/weekends hoặc danh sách thứ; đặt after_minutes > 0 cho lịch tương đối.",
                PropertyList({Property("hour", kPropertyTypeInteger, -1, -1, 23),
                              Property("minute", kPropertyTypeInteger, -1, -1, 59),
                              Property("message", kPropertyTypeString),
                              Property("repeat", kPropertyTypeString, std::string("once")),
                              Property("date", kPropertyTypeString, std::string("")),
                              Property("after_minutes", kPropertyTypeInteger, 0, 0, 10080),
                              Property("speak", kPropertyTypeBoolean, true),
                              Property("device_tool", kPropertyTypeString, std::string("")),
                              Property("device_args", kPropertyTypeString, std::string("{}"))}),
                [](const PropertyList& props) -> ReturnValue {
                    auto& manager = AutomationScheduleManager::GetInstance();
                    const std::string message = props["message"].value<std::string>();
                    const std::string device_tool = props["device_tool"].value<std::string>();
                    const std::string device_args = props["device_args"].value<std::string>();
                    const bool speak = props["speak"].value<bool>();
                    const int after = props["after_minutes"].value<int>();
                    int id = -1;
                    if (after > 0) {
                        if (!props["date"].value<std::string>().empty())
                            throw std::runtime_error("Không thể kết hợp after_minutes với date");
                        id = manager.AddIn(after, message, speak, device_tool, device_args);
                    } else {
                        const int hour = props["hour"].value<int>();
                        const int minute = props["minute"].value<int>();
                        if (hour < 0 || minute < 0)
                            throw std::runtime_error("Cần hour và minute, hoặc after_minutes");
                        uint8_t days = 0;
                        if (!AutomationScheduleManager::ParseDays(props["repeat"].value<std::string>(), days))
                            throw std::runtime_error("Chu kỳ lặp không hợp lệ");
                        const std::string date = props["date"].value<std::string>();
                        if (!date.empty()) {
                            if (days != 0) throw std::runtime_error("Không thể kết hợp date với lịch lặp");
                            time_t when;
                            if (!ParseLocalDateTime(date, hour, minute, when) || when <= time(nullptr))
                                throw std::runtime_error("Ngày giờ phải hợp lệ và ở trong tương lai");
                            id = manager.AddAt(when, message, speak, device_tool, device_args);
                        } else {
                            id = manager.Add(hour, minute, days, message, speak, device_tool, device_args);
                        }
                    }
                    if (id == -2) throw std::runtime_error("Thiết bị chưa đồng bộ được giờ, thử lại sau");
                    if (id == -3) throw std::runtime_error("Đã đủ số lịch tối đa");
                    if (id == -4) throw std::runtime_error("Không lưu được lịch vào bộ nhớ thiết bị");
                    if (id < 0) throw std::runtime_error("Tham số lịch không hợp lệ");
                    return "Đã tạo lịch #" + std::to_string(id);
                });
    mcp.AddTool("self.schedule.list", "Liệt kê toàn bộ lịch nhắc và hành động đã đặt.",
                PropertyList(), [](const PropertyList&) -> ReturnValue { return AutomationScheduleManager::GetInstance().ListJson(); });
    mcp.AddTool("self.schedule.delete", "Xóa một lịch theo id từ self.schedule.list.",
                PropertyList({Property("id", kPropertyTypeInteger, 1, 100000)}),
                [](const PropertyList& props) -> ReturnValue {
                    const int result = AutomationScheduleManager::GetInstance().Remove(props["id"].value<int>());
                    if (result == 0) throw std::runtime_error("Không tìm thấy lịch");
                    if (result < 0) throw std::runtime_error("Không lưu được thay đổi");
                    return true;
                });
    mcp.AddTool("self.schedule.clear", "Xóa toàn bộ lịch. Chỉ dùng khi người dùng yêu cầu rõ ràng.",
                PropertyList(), [](const PropertyList&) -> ReturnValue {
                    if (!AutomationScheduleManager::GetInstance().Clear()) throw std::runtime_error("Không lưu được thay đổi");
                    return true;
                });

    mcp.AddTool("self.alarm.set",
                "Alias tương thích: đặt lịch báo thức theo giờ. Hỗ trợ repeat once/daily/weekdays/weekends/custom, "
                "days_mask cho custom, message, device_tool/device_args và speak.",
                PropertyList({Property("hour", kPropertyTypeInteger, 0, 23),
                              Property("minute", kPropertyTypeInteger, 0, 59),
                              Property("message", kPropertyTypeString, std::string("Đến giờ báo thức!")),
                              Property("repeat", kPropertyTypeString, std::string("once")),
                              Property("days_mask", kPropertyTypeInteger, 0, 0, 127),
                              Property("speak", kPropertyTypeBoolean, true),
                              Property("device_tool", kPropertyTypeString, std::string("")),
                              Property("device_args", kPropertyTypeString, std::string("{}"))}),
                [](const PropertyList& props) -> ReturnValue {
                    uint8_t days = 0;
                    const std::string repeat = props["repeat"].value<std::string>();
                    if (repeat == "custom") {
                        days = static_cast<uint8_t>(props["days_mask"].value<int>());
                        if (days == 0) throw std::runtime_error("custom cần days_mask khác 0");
                    }
                    else if (!AutomationScheduleManager::ParseDays(repeat, days))
                        throw std::runtime_error("Chu kỳ lặp không hợp lệ");
                    const int id = AutomationScheduleManager::GetInstance().Add(
                        props["hour"].value<int>(), props["minute"].value<int>(), days,
                        props["message"].value<std::string>(), props["speak"].value<bool>(),
                        props["device_tool"].value<std::string>(), props["device_args"].value<std::string>());
                    if (id < 0) throw std::runtime_error("Không tạo được lịch; kiểm tra giờ và đồng bộ thời gian");
                    return "Đã đặt lịch #" + std::to_string(id);
                });
    mcp.AddTool("self.alarm.list", "Alias tương thích: liệt kê lịch trong bộ lập lịch chung.",
                PropertyList(), [](const PropertyList&) -> ReturnValue { return AutomationScheduleManager::GetInstance().ListJson(); });
    mcp.AddTool("self.alarm.delete", "Alias tương thích: xóa theo id hoặc theo hour và minute.",
                PropertyList({Property("id", kPropertyTypeInteger, -1, -1, 100000),
                              Property("hour", kPropertyTypeInteger, -1, -1, 23),
                              Property("minute", kPropertyTypeInteger, -1, -1, 59)}),
                [](const PropertyList& props) -> ReturnValue {
                    auto& manager = AutomationScheduleManager::GetInstance();
                    const int id = props["id"].value<int>();
                    const int result = id >= 0 ? manager.Remove(id) :
                        manager.RemoveAt(props["hour"].value<int>(), props["minute"].value<int>());
                    if (result == 0) throw std::runtime_error("Không tìm thấy lịch");
                    if (result < 0) throw std::runtime_error("Cần id hoặc cả hour và minute");
                    return true;
                });
}

// ============================ Start =========================================

void AutomationScheduleManager::Start() {
    if (started_) return;
    started_ = true;

    // Múi giờ Việt Nam (nếu firmware đã đặt TZ rồi thì dòng này vô hại)
    setenv("TZ", "ICT-7", 1);
    tzset();

    Load();
    RegisterMcpTools();
    if (xTaskCreate(&AutomationScheduleManager::TaskEntry, "automation_sched", 6144, this, 2, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Không tạo được unified scheduler task");
        return;
    }
    ESP_LOGI(TAG, "AutomationScheduleManager đã chạy");
}



