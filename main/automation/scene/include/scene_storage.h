#pragma once

#include <vector>
#include "scene_manager.h"

// Doc / ghi danh sach Scene xuong NVS, serialize dang JSON (dung cJSON).
// Dung namespace rieng va mot key JSON, doc lap voi schedule store.
namespace SceneStorage {

    bool Save(const std::vector<Scene>& scenes);
    std::vector<Scene> Load();

}  // namespace SceneStorage
