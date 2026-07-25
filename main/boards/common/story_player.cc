#include "story_player.h"
#include "media_library.h"
#include "mcp_server.h"

#include <esp_log.h>
#include <stdexcept>

#define TAG "StoryPlayer"

StoryPlayer& StoryPlayer::GetInstance() {
    static StoryPlayer instance;
    return instance;
}

void StoryPlayer::Initialize() {
    RegisterMcpTools();
}

void StoryPlayer::RegisterMcpTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.story.play",
        "Play a story/audio track for the child from the shared GitHub library "
        "(entries whose category is NOT \"song\" - use self.music.play for songs). "
        "Pass `story_id` if you already know the exact id (e.g. from "
        "`self.story.debug_list_index`). Otherwise pass `query` with a "
        "natural-language description and the best matching story will be "
        "found automatically. While it plays, downloading and playback happen "
        "at the same time, so it starts quickly even for long stories.",
        PropertyList({
            Property("story_id", kPropertyTypeString, std::string("")),
            Property("query", kPropertyTypeString, std::string("")),
            Property("category", kPropertyTypeString, std::string("")),
            Property("age", kPropertyTypeString, std::string(""))
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto id = properties["story_id"].value<std::string>();
            auto query = properties["query"].value<std::string>();
            auto category = properties["category"].value<std::string>();
            auto age = properties["age"].value<std::string>();

            auto& lib = MediaLibrary::GetInstance();
            lib.EnsureIndexLoaded();

            const StoryEntry* entry = nullptr;
            if (!id.empty()) {
                entry = lib.FindById(id);
                if (!entry) {
                    throw std::runtime_error("Khong tim thay story_id=" + id);
                }
            } else if (!query.empty()) {
                auto filter = [&category](const StoryEntry& e) {
                    if (!category.empty()) return e.category == category;
                    return e.category != "song";   // mac dinh: truyen, khong phai bai hat
                };
                entry = lib.FindBestMatch(query, age, filter);
                if (!entry) {
                    throw std::runtime_error("Khong tim thay truyen nao phu hop voi: " + query);
                }
            } else {
                throw std::runtime_error("Can truyen story_id hoac query");
            }

            if (!lib.Play(*entry)) {
                throw std::runtime_error("Khong phat duoc: " + entry->name);
            }
            return true;
        });

    mcp.AddTool("self.story.control",
        "Control the story that is currently playing, or check its status. "
        "`action` must be one of: \"stop\", \"pause\", \"resume\", \"status\". "
        "Always use \"status\" first if you're not sure whether a story is "
        "currently playing. Note: story and music share the same audio "
        "player, so this also reports/controls music playback.",
        PropertyList({
            Property("action", kPropertyTypeString)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto& lib = MediaLibrary::GetInstance();
            auto action = properties["action"].value<std::string>();
            if (action == "stop") {
                lib.Stop();
            } else if (action == "pause") {
                lib.Pause();
            } else if (action == "resume") {
                lib.Resume();
            } else if (action != "status") {
                throw std::runtime_error("action khong hop le: " + action + " (chi nhan stop/pause/resume/status)");
            }

            cJSON* json = cJSON_CreateObject();
            const char* state_str = "idle";
            switch (lib.state()) {
                case MediaPlayerState::kBuffering: state_str = "buffering"; break;
                case MediaPlayerState::kPlaying:   state_str = "playing"; break;
                case MediaPlayerState::kPaused:    state_str = "paused"; break;
                case MediaPlayerState::kStopped:   state_str = "stopped"; break;
                case MediaPlayerState::kError:     state_str = "error"; break;
                default: break;
            }
            cJSON_AddStringToObject(json, "state", state_str);
            cJSON_AddStringToObject(json, "current_name", lib.current_name().c_str());
            return json;
        });

    // ---- Tool quan tri: chi hien cho nguoi dung (app cau hinh) ----

    mcp.AddUserOnlyTool("self.story.debug_list_index",
        "Debug: list every story/song entry currently loaded in RAM from "
        "index.json (id, name, category, path, tags). Use this to verify the "
        "shared library loaded correctly.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            auto& lib = MediaLibrary::GetInstance();
            lib.EnsureIndexLoaded();
            std::string base_url;
            auto entries = lib.DebugListAll(&base_url);

            cJSON* arr = cJSON_CreateArray();
            for (auto& e : entries) {
                cJSON* item = cJSON_CreateObject();
                cJSON_AddStringToObject(item, "id", e.id.c_str());
                cJSON_AddStringToObject(item, "name", e.name.c_str());
                cJSON_AddStringToObject(item, "category", e.category.c_str());
                cJSON_AddStringToObject(item, "path", e.path.c_str());
                cJSON* tags = cJSON_CreateArray();
                for (auto& t : e.tags) cJSON_AddItemToArray(tags, cJSON_CreateString(t.c_str()));
                cJSON_AddItemToObject(item, "tags", tags);
                cJSON_AddItemToArray(arr, item);
            }
            cJSON* root = cJSON_CreateObject();
            cJSON_AddNumberToObject(root, "count", (int)entries.size());
            cJSON_AddStringToObject(root, "base_url", base_url.c_str());
            cJSON_AddItemToObject(root, "entries", arr);
            return root;
        });

    mcp.AddUserOnlyTool("self.story.refresh_index",
        "Re-download index.json from GitHub to pick up newly added stories/songs.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            return MediaLibrary::GetInstance().RefreshIndex();
        });

    mcp.AddUserOnlyTool("self.story.set_base_url",
        "Set the base URL of the shared GitHub library (raw.githubusercontent.com "
        "root). Applies to BOTH stories and songs since they now share one repo.",
        PropertyList({
            Property("url", kPropertyTypeString)
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto url = properties["url"].value<std::string>();
            auto& lib = MediaLibrary::GetInstance();
            lib.SetBaseUrl(url);
            return lib.RefreshIndex();
        });
}