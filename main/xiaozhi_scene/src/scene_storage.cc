#include "scene_storage.h"
#include "nvs.h"
#include "esp_log.h"
#include <cJSON.h>
#include <vector>

static const char* TAG = "SceneStorage";
static const char* NVS_NAMESPACE = "scenes";
static const char* NVS_KEY = "list";

namespace SceneStorage {

bool Save(const std::vector<Scene>& scenes) {
    cJSON* root = cJSON_CreateArray();
    if (!root) return false;

    for (const auto& s : scenes) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "id", s.id);
        cJSON_AddStringToObject(item, "name", s.name.c_str());

        cJSON* steps_arr = cJSON_CreateArray();
        for (const auto& step : s.steps) {
            cJSON* step_obj = cJSON_CreateObject();
            cJSON_AddStringToObject(step_obj, "tool", step.tool_name.c_str());
            cJSON_AddStringToObject(step_obj, "args", step.args_json.c_str());
            cJSON_AddItemToArray(steps_arr, step_obj);
        }
        cJSON_AddItemToObject(item, "steps", steps_arr);

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

std::vector<Scene> Load() {
    std::vector<Scene> result;

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        // Chua co namespace nao tung ghi -> danh sach rong la binh thuong.
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
        ESP_LOGE(TAG, "Loi parse JSON scene tu NVS");
        return result;
    }

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root) {
        Scene s;
        cJSON* v;

        v = cJSON_GetObjectItem(item, "id");
        s.id = v ? (uint32_t)v->valueint : 0;

        v = cJSON_GetObjectItem(item, "name");
        s.name = (v && cJSON_IsString(v)) ? v->valuestring : "";

        cJSON* steps_arr = cJSON_GetObjectItem(item, "steps");
        if (steps_arr && cJSON_IsArray(steps_arr)) {
            cJSON* step_item = nullptr;
            cJSON_ArrayForEach(step_item, steps_arr) {
                SceneStep step;
                cJSON* tool_v = cJSON_GetObjectItem(step_item, "tool");
                cJSON* args_v = cJSON_GetObjectItem(step_item, "args");
                step.tool_name = (tool_v && cJSON_IsString(tool_v)) ? tool_v->valuestring : "";
                step.args_json = (args_v && cJSON_IsString(args_v)) ? args_v->valuestring : "{}";
                if (!step.tool_name.empty()) {
                    s.steps.push_back(step);
                }
            }
        }

        result.push_back(s);
    }

    cJSON_Delete(root);
    return result;
}

}  // namespace SceneStorage
