#include "alarm_storage.h"
#include "nvs.h"
#include "esp_log.h"
#include "cJSON.h"
#include <vector>

static const char* TAG = "AlarmStorage";
static const char* NVS_NAMESPACE = "alarms";
static const char* NVS_KEY = "list";

namespace AlarmStorage {

bool Save(const std::vector<Alarm>& alarms) {
    cJSON* root = cJSON_CreateArray();
    if (!root) return false;

    for (const auto& a : alarms) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", a.id);
        cJSON_AddNumberToObject(item, "hour", a.hour);
        cJSON_AddNumberToObject(item, "minute", a.minute);
        cJSON_AddStringToObject(item, "repeat", RepeatModeToString(a.repeat));
        cJSON_AddNumberToObject(item, "wmask", a.weekday_mask);
        cJSON_AddStringToObject(item, "msg", a.message.c_str());
        cJSON_AddStringToObject(item, "action", a.action.c_str());
        cJSON_AddStringToObject(item, "mcp_tool", a.mcp_tool_name.c_str());
        cJSON_AddStringToObject(item, "mcp_args", a.mcp_tool_args.c_str());
        cJSON_AddBoolToObject(item, "enabled", a.enabled);
        cJSON_AddItemToArray(root, item);
    }

    char* json_str = cJSON_PrintUnformatted(root);
    bool ok = false;

    if (json_str) {
        nvs_handle_t handle;
        esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
        if (err == ESP_OK) {
            err = nvs_set_str(handle, NVS_KEY, json_str);
            if (err == ESP_OK) err = nvs_commit(handle);
            nvs_close(handle);
            ok = (err == ESP_OK);
            if (!ok) {
                ESP_LOGE(TAG, "Loi ghi NVS: %s", esp_err_to_name(err));
            }
        } else {
            ESP_LOGE(TAG, "Khong mo duoc NVS namespace: %s", esp_err_to_name(err));
        }
    }

    cJSON_free(json_str);
    cJSON_Delete(root);
    return ok;
}

std::vector<Alarm> Load() {
    std::vector<Alarm> result;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        // Chưa có namespace nào từng ghi -> danh sách rỗng là bình thường.
        return result;
    }

    size_t len = 0;
    err = nvs_get_str(handle, NVS_KEY, nullptr, &len);
    if (err != ESP_OK || len == 0) {
        nvs_close(handle);
        return result;
    }

    std::vector<char> buf(len);
    err = nvs_get_str(handle, NVS_KEY, buf.data(), &len);
    nvs_close(handle);
    if (err != ESP_OK) return result;

    cJSON* root = cJSON_Parse(buf.data());
    if (!root) {
        ESP_LOGE(TAG, "Loi parse JSON alarm tu NVS");
        return result;
    }

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root) {
        Alarm a;
        cJSON* v;

        v = cJSON_GetObjectItem(item, "id");
        a.id = v ? (uint32_t)v->valueint : 0;

        v = cJSON_GetObjectItem(item, "hour");
        a.hour = v ? (uint8_t)v->valueint : 0;

        v = cJSON_GetObjectItem(item, "minute");
        a.minute = v ? (uint8_t)v->valueint : 0;

        v = cJSON_GetObjectItem(item, "repeat");
        a.repeat = v && cJSON_IsString(v) ? ParseRepeatMode(v->valuestring) : RepeatMode::ONCE;

        v = cJSON_GetObjectItem(item, "wmask");
        a.weekday_mask = v ? (uint8_t)v->valueint : 0;

        v = cJSON_GetObjectItem(item, "msg");
        a.message = (v && cJSON_IsString(v)) ? v->valuestring : "";

        v = cJSON_GetObjectItem(item, "action");
        a.action = (v && cJSON_IsString(v)) ? v->valuestring : "notify";

        v = cJSON_GetObjectItem(item, "mcp_tool");
        a.mcp_tool_name = (v && cJSON_IsString(v)) ? v->valuestring : "";

        v = cJSON_GetObjectItem(item, "mcp_args");
        a.mcp_tool_args = (v && cJSON_IsString(v)) ? v->valuestring : "{}";

        v = cJSON_GetObjectItem(item, "enabled");
        a.enabled = v ? cJSON_IsTrue(v) : true;

        a.last_triggered = 0;
        result.push_back(a);
    }

    cJSON_Delete(root);
    return result;
}

}  // namespace AlarmStorage