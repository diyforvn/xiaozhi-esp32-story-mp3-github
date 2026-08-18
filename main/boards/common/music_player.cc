#include "music_player.h"
#include "media_library.h"
#include "mcp_server.h"

#include <esp_log.h>
#include <stdexcept>

#define TAG "MusicPlayer"

namespace {
// Chi coi la "bai hat" khi category == "song" trong index.json dung chung.
bool IsSongEntry(const StoryEntry& e) {
    return e.category == "song";
}
}  // namespace

MusicPlayer& MusicPlayer::GetInstance() {
    static MusicPlayer instance;
    return instance;
}

void MusicPlayer::Initialize() {
    RegisterMcpTools();
}

void MusicPlayer::RegisterMcpTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool("self.music.play",
        "Play a song for the child from the shared GitHub library (entries "
        "with category \"song\" - use self.story.play for non-song stories). "
        "Pass `song_id` if known (e.g. from `self.story.debug_list_index`), "
        "otherwise pass `query` with the song title/topic/artist and the best "
        "match will be found automatically. Downloading and playback happen "
        "at the same time, so it starts quickly.",
        PropertyList({
            Property("song_id", kPropertyTypeString, std::string("")),
            Property("query", kPropertyTypeString, std::string("")),
            Property("age", kPropertyTypeString, std::string(""))
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto id = properties["song_id"].value<std::string>();
            auto query = properties["query"].value<std::string>();
            auto age = properties["age"].value<std::string>();

            auto& lib = MediaLibrary::GetInstance();
            lib.EnsureIndexLoaded();

            const StoryEntry* entry = nullptr;
            if (!id.empty()) {
                entry = lib.FindById(id);
                if (!entry) {
                    throw std::runtime_error("Khong tim thay song_id=" + id);
                }
                if (!IsSongEntry(*entry)) {
                    throw std::runtime_error("id nay khong phai bai hat: " + id);
                }
            } else if (!query.empty()) {
                entry = lib.FindBestMatch(query, age, IsSongEntry);
                if (!entry) {
                    throw std::runtime_error("Khong tim thay bai hat nao phu hop voi: " + query);
                }
            } else {
                throw std::runtime_error("Can truyen song_id hoac query");
            }

            if (!lib.Play(*entry)) {
                throw std::runtime_error("Khong phat duoc: " + entry->name);
            }
            return true;
        });

    mcp.AddTool("self.music.control",
        "Control the song that is currently playing, or check its status. "
        "`action` must be one of: \"stop\", \"pause\", \"resume\", \"status\". "
        "Note: story and music share the same audio player, so this also "
        "reports/controls story playback.",
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

    mcp.AddUserOnlyTool("self.music.debug_list_index",
        "Debug: list every song entry (category == \"song\") currently loaded "
        "in RAM from the shared index.json.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            auto& lib = MediaLibrary::GetInstance();
            lib.EnsureIndexLoaded();
            std::string base_url;
            auto entries = lib.DebugListAll(&base_url);

            cJSON* arr = cJSON_CreateArray();
            int count = 0;
            for (auto& e : entries) {
                if (!IsSongEntry(e)) continue;
                count++;
                cJSON* item = cJSON_CreateObject();
                cJSON_AddStringToObject(item, "id", e.id.c_str());
                cJSON_AddStringToObject(item, "name", e.name.c_str());
                cJSON_AddStringToObject(item, "path", e.path.c_str());
                cJSON* tags = cJSON_CreateArray();
                for (auto& t : e.tags) cJSON_AddItemToArray(tags, cJSON_CreateString(t.c_str()));
                cJSON_AddItemToObject(item, "tags", tags);
                cJSON_AddItemToArray(arr, item);
            }
            cJSON* root = cJSON_CreateObject();
            cJSON_AddNumberToObject(root, "count", count);
            cJSON_AddStringToObject(root, "base_url", base_url.c_str());
            cJSON_AddItemToObject(root, "songs", arr);
            return root;
        });
}

   