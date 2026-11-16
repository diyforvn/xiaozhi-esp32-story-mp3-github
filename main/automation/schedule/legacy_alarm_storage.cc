#include "legacy_alarm_storage.h"

#include <cJSON.h>
#include <esp_log.h>
#include <nvs.h>

static constexpr const char* TAG = "LegacyAlarmStorage";
static constexpr const char* NVS_NAMESPACE = "alarms";
static constexpr const char* NVS_KEY = "list";

std::vector<LegacyAlarm> LegacyAlarmStorage::Load() {
    std::vector<LegacyAlarm> result;
    nvs_handle_t handle;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) return result;

    size_t length = 0;
    esp_err_t err = nvs_get_str(handle, NVS_KEY, nullptr, &length);
    if (err != ESP_OK || length == 0) {
        nvs_close(handle);
        return result;
    }

    std::vector<char> buffer(length);
    err = nvs_get_str(handle, NVS_KEY, buffer.data(), &length);
    nvs_close(handle);
    if (err != ESP_OK) return result;

    cJSON* root = cJSON_Parse(buffer.data());
    if (!cJSON_IsArray(root)) {
        ESP_LOGE(TAG, "Legacy alarm data is not a JSON array");
        cJSON_Delete(root);
        return result;
    }

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root) {
        LegacyAlarm alarm;
        cJSON* value = cJSON_GetObjectItem(item, "id");
        if (cJSON_IsNumber(value)) alarm.id = static_cast<uint32_t>(value->valueint);
        value = cJSON_GetObjectItem(item, "hour");
        if (cJSON_IsNumber(value)) alarm.hour = static_cast<uint8_t>(value->valueint);
        value = cJSON_GetObjectItem(item, "minute");
        if (cJSON_IsNumber(value)) alarm.minute = static_cast<uint8_t>(value->valueint);
        value = cJSON_GetObjectItem(item, "repeat");
        if (cJSON_IsString(value)) alarm.repeat = value->valuestring;
        value = cJSON_GetObjectItem(item, "wmask");
        if (cJSON_IsNumber(value)) alarm.weekday_mask = static_cast<uint8_t>(value->valueint);
        value = cJSON_GetObjectItem(item, "msg");
        if (cJSON_IsString(value)) alarm.message = value->valuestring;
        value = cJSON_GetObjectItem(item, "action");
        if (cJSON_IsString(value)) alarm.action = value->valuestring;
        value = cJSON_GetObjectItem(item, "mcp_tool");
        if (cJSON_IsString(value)) alarm.device_tool = value->valuestring;
        value = cJSON_GetObjectItem(item, "mcp_args");
        if (cJSON_IsString(value)) alarm.device_args = value->valuestring;
        value = cJSON_GetObjectItem(item, "enabled");
        if (value != nullptr) alarm.enabled = cJSON_IsTrue(value);
        if (alarm.id > 0 && alarm.hour < 24 && alarm.minute < 60) result.push_back(std::move(alarm));
    }

    cJSON_Delete(root);
    return result;
}

bool LegacyAlarmStorage::Clear() {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) return true;
    if (err != ESP_OK) return false;
    err = nvs_erase_key(handle, NVS_KEY);
    if (err == ESP_ERR_NVS_NOT_FOUND) err = ESP_OK;
    if (err == ESP_OK) err = nvs_commit(handle);
    nvs_close(handle);
    return err == ESP_OK;
}
