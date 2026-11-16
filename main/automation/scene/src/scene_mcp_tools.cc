// Dang ky cac MCP tool lien quan den Scene: self.scene.create,
// self.scene.run, self.scene.list, self.scene.delete.

#include "mcp_server.h"
#include "scene_manager.h"
#include "application.h"
#include <cJSON.h>
#include <cinttypes>
#include <cstring>
#include <string>

// Parse tham so "steps" (chuoi JSON array) thanh vector<SceneStep>.
// Dinh dang mong doi: [{"tool":"self.wled.turn_off","args":"{}"}, ...]
// AI tu soan noi dung nay dua tren danh sach tool no biet (tools/list).
static bool ParseStepsJson(const std::string& steps_json, std::vector<SceneStep>& out,
                            std::string& error_out) {
    cJSON* root = cJSON_Parse(steps_json.c_str());
    if (!root || !cJSON_IsArray(root)) {
        error_out = "Tham so 'steps' phai la 1 chuoi JSON array hop le";
        if (root) cJSON_Delete(root);
        return false;
    }

    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, root) {
        cJSON* tool_v = cJSON_GetObjectItem(item, "tool");
        if (!tool_v || !cJSON_IsString(tool_v) || strlen(tool_v->valuestring) == 0) {
            error_out = "Moi buoc trong 'steps' can co truong 'tool' (ten MCP tool)";
            cJSON_Delete(root);
            return false;
        }

        SceneStep step;
        step.tool_name = tool_v->valuestring;

        cJSON* args_v = cJSON_GetObjectItem(item, "args");
        if (args_v && cJSON_IsString(args_v) && strlen(args_v->valuestring) > 0) {
            step.args_json = args_v->valuestring;
        } else {
            step.args_json = "{}";
        }

        out.push_back(step);
    }

    cJSON_Delete(root);
    return true;
}

void RegisterSceneMcpTools() {
    auto& mcp_server = McpServer::GetInstance();

    // ---- self.scene.create ----
    mcp_server.AddTool(
        "self.scene.create",
        "Tao (hoac ghi de neu trung ten) 1 kich ban goi nhieu MCP tool "
        "lien tiep khi duoc kich hoat, vd 'di_ngu' se tat den + tat quat. "
        "Tham so: name (ten kich ban, dung de goi lai qua self.scene.run); "
        "steps (chuoi JSON array cac buoc, moi buoc dang "
        "{\"tool\":\"ten_tool_da_co_san\",\"args\":\"{...}\"} - PHAI dung "
        "dung ten tool va tham so nhu trong danh sach tool ban dang co, "
        "khong duoc bia ten tool khong ton tai).",
        PropertyList({
            Property("name", kPropertyTypeString),
            Property("steps", kPropertyTypeString),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            std::string name = properties["name"].value<std::string>();
            std::string steps_json = properties["steps"].value<std::string>();

            std::vector<SceneStep> steps;
            std::string error;
            if (!ParseStepsJson(steps_json, steps, error)) {
                return error;
            }
            if (steps.empty()) {
                return std::string("Kich ban can it nhat 1 buoc trong 'steps'");
            }

            uint32_t id = g_scene_manager.CreateScene(name, steps);
            char buf[128];
            snprintf(buf, sizeof(buf), "Da tao kich ban '%s' voi %d buoc (id=%" PRIu32 ")",
                     name.c_str(), (int)steps.size(), id);
            return std::string(buf);
        });

    // ---- self.scene.run ----
    mcp_server.AddTool(
        "self.scene.run",
        "Chay 1 kich ban da tao truoc do bang self.scene.create, theo dung "
        "ten. Cac buoc se chay TUAN TU (doi buoc truoc xong moi chay buoc "
        "sau). Ham nay tra loi ngay 'dang thuc hien', ket qua thanh/bai "
        "cuoi cung se duoc HIEN LEN MAN HINH sau khi chay xong, khong tra "
        "ve truc tiep trong ket qua tool nay.",
        PropertyList({
            Property("name", kPropertyTypeString),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            std::string name = properties["name"].value<std::string>();

            bool started = g_scene_manager.RunScene(
                name,
                [name](bool success, const std::string& msg) {
                    auto& app = Application::GetInstance();
                    app.Schedule([&app, name, success, msg]() {
                        if (success) {
                            std::string text = "Da chay xong kich ban '" + name + "'";
                            app.Alert("Kich ban hoan tat", text.c_str(), "happy", std::string_view());
                        } else {
                            app.Alert("Loi kich ban", msg.c_str(), "sad", std::string_view());
                        }
                    });
                });

            if (!started) {
                return std::string("Khong tim thay kich ban '" + name + "'");
            }

            auto scene = g_scene_manager.FindSceneByName(name);
            int step_count = scene ? (int)scene->steps.size() : 0;
            char buf[128];
            snprintf(buf, sizeof(buf), "Dang thuc hien kich ban '%s' (%d buoc)",
                     name.c_str(), step_count);
            return std::string(buf);
        });

    // ---- self.scene.list ----
    mcp_server.AddTool(
        "self.scene.list",
        "Liet ke tat ca kich ban (scene) da tao, kem so buoc trong moi kich ban",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            std::string json = "[";
            bool first = true;
            for (const auto& s : g_scene_manager.GetScenes()) {
                if (!first) json += ",";
                first = false;
                char item[256];
                snprintf(item, sizeof(item),
                    "{\"id\":%" PRIu32 ",\"name\":\"%s\",\"step_count\":%d}",
                    s.id, s.name.c_str(), (int)s.steps.size());
                json += item;
            }
            json += "]";
            return json;
        });

    // ---- self.scene.delete ----
    mcp_server.AddTool(
        "self.scene.delete",
        "Xoa 1 kich ban theo ten",
        PropertyList({
            Property("name", kPropertyTypeString),
        }),
        [](const PropertyList& properties) -> ReturnValue {
            std::string name = properties["name"].value<std::string>();
            bool ok = g_scene_manager.RemoveSceneByName(name);
            return ok ? std::string("Da xoa kich ban '" + name + "'")
                      : std::string("Khong tim thay kich ban '" + name + "'");
        });
}
