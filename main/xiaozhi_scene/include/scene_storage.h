#pragma once

#include <vector>
#include "scene_manager.h"

// Doc / ghi danh sach Scene xuong NVS, serialize dang JSON (dung cJSON).
// Cung pattern voi AlarmStorage (namespace rieng, 1 key luu ca mang JSON).
namespace SceneStorage {

    bool Save(const std::vector<Scene>& scenes);
    std::vector<Scene> Load();

}  // namespace SceneStorage
