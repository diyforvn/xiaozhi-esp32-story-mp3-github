#include "scene_manager.h"
#include "scene_storage.h"
#include "mcp_server.h"
#include "esp_log.h"
#include <cJSON.h>
#include <memory>

static const char* TAG = "SceneManager";

SceneManager& g_scene_manager = SceneManager::GetInstance();

void SceneManager::Init() {
    scenes_ = SceneStorage::Load();

    for (auto& s : scenes_) {
        if (s.id >= next_id_) next_id_ = s.id + 1;
    }

    ESP_LOGI(TAG, "Da load %d scene tu NVS", (int)scenes_.size());
}

uint32_t SceneManager::GenerateSceneId() {
    return next_id_++;
}

uint32_t SceneManager::CreateScene(const std::string& name, const std::vector<SceneStep>& steps) {
    // Neu scene cung ten da ton tai -> ghi de (xoa cai cu, tao cai moi)
    // de nguoi dung co the "sua lai kich ban" bang cach tao lai cung ten,
    // khong can nho id.
    RemoveSceneByName(name);

    Scene s;
    s.id = GenerateSceneId();
    s.name = name;
    s.steps = steps;
    scenes_.push_back(s);
    PersistNow();

    ESP_LOGI(TAG, "Tao scene '%s' (id=%u, %d buoc)", name.c_str(), s.id, (int)steps.size());
    return s.id;
}

bool SceneManager::RemoveSceneByName(const std::string& name) {
    for (auto it = scenes_.begin(); it != scenes_.end(); ++it) {
        if (it->name == name) {
            scenes_.erase(it);
            PersistNow();
            return true;
        }
    }
    return false;
}

std::optional<Scene> SceneManager::FindSceneByName(const std::string& name) const {
    for (const auto& s : scenes_) {
        if (s.name == name) return s;
    }
    return std::nullopt;
}

void SceneManager::PersistNow() {
    SceneStorage::Save(scenes_);
}

// Chay tuan tu tung buoc cua 1 scene, dung con tro chia se (shared_ptr)
// de Scene song sot xuyen suot chuoi callback bat dong bo (moi buoc goi
// qua McpServer::InvokeToolDirect deu tra ve KHONG DONG BO qua Schedule()).
static void RunSceneStepsSequential(std::shared_ptr<Scene> scene, size_t index,
                                     SceneRunCallback on_finish) {
    if (index >= scene->steps.size()) {
        if (on_finish) on_finish(true, "");
        return;
    }

    const SceneStep& step = scene->steps[index];
    std::string args_str = step.args_json.empty() ? "{}" : step.args_json;
    cJSON* args = cJSON_Parse(args_str.c_str());
    if (args == nullptr) {
        if (on_finish) {
            on_finish(false, "Buoc " + std::to_string(index + 1) + " (" + step.tool_name +
                                  "): tham so JSON khong hop le");
        }
        return;
    }

    McpServer::GetInstance().InvokeToolDirect(
        step.tool_name, args,
        [scene, index, on_finish](bool success, const std::string& msg) {
            if (!success) {
                if (on_finish) {
                    on_finish(false, "Buoc " + std::to_string(index + 1) + " (" +
                                          scene->steps[index].tool_name + ") that bai: " + msg);
                }
                return;
            }
            // Buoc nay xong, chay tiep buoc ke - de day tren cung
            // "duong day" callback, khong bi gioi han do sau de quy vi
            // moi lan goi la 1 lan Schedule() moi (khong phai stack
            // truyen thong), an toan ngay ca scene co nhieu buoc.
            RunSceneStepsSequential(scene, index + 1, on_finish);
        });

    // InvokeToolDirect() dung xong "args" (dong bo, phan validate tham so)
    // truoc khi return - phan thuc thi that su da duoc Schedule() rieng
    // dua tren PropertyList da copy gia tri - nen xoa ngay o day an toan.
    cJSON_Delete(args);
}

bool SceneManager::RunScene(const std::string& name, SceneRunCallback callback) {
    auto found = FindSceneByName(name);
    if (!found) {
        return false;
    }

    auto scene_ptr = std::make_shared<Scene>(*found);
    ESP_LOGI(TAG, "Chay scene '%s' (%d buoc)", name.c_str(), (int)scene_ptr->steps.size());
    RunSceneStepsSequential(scene_ptr, 0, callback);
    return true;
}
