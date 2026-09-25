#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <optional>

// 1 buoc trong scene: goi 1 MCP tool da co san voi 1 bo tham so JSON.
struct SceneStep {
    std::string tool_name;    // vd "self.wled.turn_off"
    std::string args_json;    // vd "{}" hoac "{\"r\":0,\"g\":0,\"b\":0}"
};

struct Scene {
    uint32_t id = 0;
    std::string name;              // vd "di_ngu" - dung de goi qua self.scene.run
    std::vector<SceneStep> steps;  // thu tu chay: dung index 0 truoc
};

// Callback bao ket qua chay 1 scene: success=true neu TAT CA cac buoc
// deu thanh cong; neu 1 buoc that bai, dung ngay (khong chay buoc sau)
// va success=false, message mo ta buoc nao that bai vi sao.
using SceneRunCallback = std::function<void(bool success, const std::string& message)>;

class SceneManager {
public:
    static SceneManager& GetInstance() {
        static SceneManager instance;
        return instance;
    }

    void Init();  // load danh sach scene tu NVS

    uint32_t CreateScene(const std::string& name, const std::vector<SceneStep>& steps);
    bool RemoveSceneByName(const std::string& name);
    std::optional<Scene> FindSceneByName(const std::string& name) const;
    const std::vector<Scene>& GetScenes() const { return scenes_; }

    // Chay 1 scene theo ten, TUAN TU tung buoc (doi buoc truoc xong moi
    // chay buoc sau). Ham nay return NGAY (khong block) - ket qua bao qua
    // "callback" sau khi (mot phan hoac toan bo) cac buoc da chay xong.
    // Return false ngay lap tuc (khong goi callback) neu khong tim thay
    // scene theo ten.
    bool RunScene(const std::string& name, SceneRunCallback callback);

private:
    SceneManager() = default;

    std::vector<Scene> scenes_;
    uint32_t next_id_ = 1;

    void PersistNow();
    uint32_t GenerateSceneId();
};

extern SceneManager& g_scene_manager;  // alias tien loi, tro toi GetInstance()
