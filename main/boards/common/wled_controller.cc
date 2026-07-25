#include "wled_controller.h"

#include "sdkconfig.h"

#include <esp_log.h>
#include <esp_timer.h>

#define TAG "WledController"

WledController& WledController::GetInstance() {
    static WledController instance;
    return instance;
}

#if CONFIG_ENABLE_WLED

#include "board.h"
#include "mcp_server.h"
#include "settings.h"

#include <cJSON.h>

// ─────────────────────────────────────────────
// Initialize
// ─────────────────────────────────────────────

void WledController::Initialize() {
    Settings settings("wled");
    host_       = settings.GetString("host", CONFIG_WLED_DEFAULT_HOST);
    segment_id_ = settings.GetInt("segment", 0);

    RegisterTools();
    ESP_LOGI(TAG, "WLED controller initialized, host=%s, segment=%d",
             host_.c_str(), segment_id_);
}

void WledController::SetHost(const std::string& host) {
    host_ = host;
    // Reset cache khi đổi host
    is_online_    = false;
    last_check_us_ = 0;
    Settings settings("wled", true);
    settings.SetString("host", host_);
}

// ─────────────────────────────────────────────
// Online check
// ─────────────────────────────────────────────

bool WledController::CheckOnline(bool force) {
    if (host_.empty()) {
        is_online_ = false;
        return false;
    }

    int64_t now = esp_timer_get_time();

    // Dùng cache nếu chưa hết hạn và không force
    if (!force && last_check_us_ > 0 && (now - last_check_us_) < kCacheUs) {
        return is_online_;
    }

    // Ping thật: GET /json/info với timeout ngắn (2s)
    bool reachable = false;
    try {
        auto http = Board::GetInstance().GetNetwork()->CreateHttp(2); // 2s timeout
        auto url  = "http://" + host_ + "/json/info";
        if (http->Open("GET", url)) {
            reachable = (http->GetStatusCode() == 200);
            http->Close();
        }
    } catch (...) {
        reachable = false;
    }

    is_online_    = reachable;
    last_check_us_ = now;
    ESP_LOGI(TAG, "WLED online check: %s → %s",
             host_.c_str(), reachable ? "ONLINE" : "OFFLINE");
    return is_online_;
}

// ─────────────────────────────────────────────
// Safe wrappers — check online trước khi gọi
// ─────────────────────────────────────────────

std::string WledController::SafeHttpGet(const std::string& path) {
    if (!CheckOnline()) {
        throw std::runtime_error(
            "WLED device is offline or unreachable at " + host_ +
            ". Please check power and WiFi connection.");
    }
    return HttpGet(path);
}

bool WledController::SafeHttpPost(const std::string& path, const std::string& json_body) {
    if (!CheckOnline()) {
        throw std::runtime_error(
            "WLED device is offline or unreachable at " + host_ +
            ". Please check power and WiFi connection.");
    }
    bool ok = HttpPost(path, json_body);
    if (!ok) {
        // Nếu post thất bại, invalidate cache để lần sau check lại
        last_check_us_ = 0;
    }
    return ok;
}

// ─────────────────────────────────────────────
// Raw HTTP helpers (không đổi)
// ─────────────────────────────────────────────

std::string WledController::BuildUrl(const std::string& path) const {
    if (host_.empty()) {
        throw std::runtime_error("WLED host is not configured");
    }
    // If host_ already contains a scheme, use it as-is. Otherwise prepend http://
    std::string base = host_;
    if (base.rfind("http://", 0) != 0 && base.rfind("https://", 0) != 0) {
        base = "http://" + base;
    }
    // Avoid double slash when concatenating
    if (!base.empty() && base.back() == '/' && !path.empty() && path.front() == '/') {
        return base.substr(0, base.size() - 1) + path;
    }
    return base + path;
}

std::string WledController::HttpGet(const std::string& path) {
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(5);
    auto url  = BuildUrl(path);

    if (!http->Open("GET", url)) {
        last_check_us_ = 0; // invalidate cache
        throw std::runtime_error("Failed to connect to WLED at " + host_);
    }

    int status_code = http->GetStatusCode();
    if (status_code != 200) {
        http->Close();
        throw std::runtime_error("WLED request failed, status: " +
                                 std::to_string(status_code));
    }

    std::string response = http->ReadAll();
    http->Close();
    return response;
}

bool WledController::HttpPost(const std::string& path, const std::string& json_body) {
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(5);
    auto url  = BuildUrl(path);

    http->SetHeader("Content-Type", "application/json");

    if (!http->Open("POST", url)) {
        last_check_us_ = 0; // invalidate cache
        throw std::runtime_error("Failed to connect to WLED at " + host_);
    }

    // Write the JSON body after opening the connection
    http->Write(json_body.data(), json_body.size());

    // Signal end of request body
    http->Write("", 0);

    int status_code = http->GetStatusCode();
    http->Close();

    if (status_code != 200) {
        throw std::runtime_error("WLED request failed, status: " +
                                 std::to_string(status_code));
    }
    return true;
}

// ─────────────────────────────────────────────
// MCP Tool registration — đổi HttpGet/Post → SafeHttpGet/Post
// ─────────────────────────────────────────────

void WledController::RegisterTools() {
    auto& mcp = McpServer::GetInstance();

    // Ping tool — LLM dùng khi nghi ngờ thiết bị offline
    mcp.AddTool("self.wled.ping",
        "Check whether the WLED device is reachable on the local network. "
        "Returns online/offline status and the configured host. "
        "(Kiểm tra đèn WLED có kết nối mạng không)",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            bool online = CheckOnline(true); // force ping, bỏ qua cache
            std::string result = "{\"host\":\"" + host_ +
                                 "\",\"online\":" + (online ? "true" : "false") + "}";
            return result;
        });

    mcp.AddTool("self.wled.get_state",
        "Get the current state of the WLED smart light (power, brightness, color, effect). "
        "Use this before changing WLED settings if the current state is unknown. "
        "(Lấy trạng thái đèn WLED)",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            return SafeHttpGet("/json/state");
        });

    mcp.AddTool("self.wled.turn_on",
        "Turn on the WLED smart light. (Bật đèn WLED)",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            return SafeHttpPost("/json/state", "{\"on\":true}");
        });

    mcp.AddTool("self.wled.turn_off",
        "Turn off the WLED smart light. (Tắt đèn WLED)",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            return SafeHttpPost("/json/state", "{\"on\":false}");
        });

    mcp.AddTool("self.wled.set_brightness",
        "Set the brightness of the WLED smart light. "
        "Brightness range is 0 (off) to 255 (maximum). "
        "(Điều chỉnh độ sáng đèn WLED)",
        PropertyList({
            Property("brightness", kPropertyTypeInteger, 0, 255)
        }),
        [this](const PropertyList& props) -> ReturnValue {
            int bri = props["brightness"].value<int>();
            ESP_LOGI(TAG, "Set WLED brightness to %d", bri);
            return SafeHttpPost("/json/state",
                "{\"bri\":" + std::to_string(bri) + "}");
        });

    mcp.AddTool("self.wled.set_color",
        "Set the RGB color of the WLED smart light. "
        "Each channel (red, green, blue) ranges from 0 to 255. "
        "(Đặt màu RGB cho đèn WLED)",
        PropertyList({
            Property("red",   kPropertyTypeInteger, 0, 255),
            Property("green", kPropertyTypeInteger, 0, 255),
            Property("blue",  kPropertyTypeInteger, 0, 255)
        }),
        [this](const PropertyList& props) -> ReturnValue {
            int r = props["red"].value<int>();
            int g = props["green"].value<int>();
            int b = props["blue"].value<int>();

            cJSON* root     = cJSON_CreateObject();
            cJSON* segments = cJSON_CreateArray();
            cJSON* seg      = cJSON_CreateObject();
            cJSON* colors   = cJSON_CreateArray();
            cJSON* rgb      = cJSON_CreateArray();
            cJSON_AddItemToArray(rgb, cJSON_CreateNumber(r));
            cJSON_AddItemToArray(rgb, cJSON_CreateNumber(g));
            cJSON_AddItemToArray(rgb, cJSON_CreateNumber(b));
            cJSON_AddItemToArray(colors, rgb);
            cJSON_AddItemToObject(seg, "id",  cJSON_CreateNumber(segment_id_));
            cJSON_AddItemToObject(seg, "col", colors);
            cJSON_AddItemToArray(segments, seg);
            cJSON_AddItemToObject(root, "seg", segments);
            cJSON_AddBoolToObject(root, "on", true);

            char* s = cJSON_PrintUnformatted(root);
            std::string body(s);
            cJSON_free(s);
            cJSON_Delete(root);

            ESP_LOGI(TAG, "Set WLED color rgb(%d,%d,%d)", r, g, b);
            return SafeHttpPost("/json/state", body);
        });

    mcp.AddTool("self.wled.set_effect",
        "Set the lighting effect of the WLED smart light. "
        "Use self.wled.get_effects first to list available effect IDs. "
        "(Đặt hiệu ứng ánh sáng WLED)",
        PropertyList({
            Property("effect", kPropertyTypeInteger, 0, 255)
        }),
        [this](const PropertyList& props) -> ReturnValue {
            int fx = props["effect"].value<int>();
            std::string body = "{\"seg\":[{\"id\":" + std::to_string(segment_id_) +
                               ",\"fx\":" + std::to_string(fx) + "}],\"on\":true}";
            ESP_LOGI(TAG, "Set WLED effect %d", fx);
            return SafeHttpPost("/json/state", body);
        });

    mcp.AddTool("self.wled.get_effects",
        "List all available lighting effects on the WLED device. "
        "Returns effect names indexed by their numeric ID. "
        "(Liệt kê hiệu ứng ánh sáng WLED)",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            return SafeHttpGet("/json/effects");
        });

    mcp.AddTool("self.wled.set_preset",
        "Apply a saved preset on the WLED device by preset ID. "
        "(Áp dụng preset đã lưu trên WLED)",
        PropertyList({
            Property("preset", kPropertyTypeInteger, 0, 250)
        }),
        [this](const PropertyList& props) -> ReturnValue {
            int ps = props["preset"].value<int>();
            ESP_LOGI(TAG, "Apply WLED preset %d", ps);
            return SafeHttpPost("/json/state",
                "{\"ps\":" + std::to_string(ps) + "}");
        });

    // user-only: cấu hình host + segment
    mcp.AddUserOnlyTool("self.wled.configure",
        "Configure the WLED device connection. Set the IP address or hostname "
        "of the WLED device on the local network. (Cấu hình địa chỉ IP thiết bị WLED)",
        PropertyList({
            Property("host",    kPropertyTypeString),
            Property("segment", kPropertyTypeInteger, 0, 0, 15)
        }),
        [this](const PropertyList& props) -> ReturnValue {
            auto host = props["host"].value<std::string>();
            if (host.empty()) {
                throw std::runtime_error("WLED host cannot be empty");
            }
            SetHost(host);

            segment_id_ = props["segment"].value<int>();
            Settings settings("wled", true);
            settings.SetInt("segment", segment_id_);

            // Ping ngay sau khi cấu hình để báo kết quả
            bool online = CheckOnline(true);
            ESP_LOGI(TAG, "WLED configured: host=%s, segment=%d, online=%s",
                     host_.c_str(), segment_id_, online ? "yes" : "no");

            return std::string(
                "{\"host\":\"" + host_ +
                "\",\"segment\":" + std::to_string(segment_id_) +
                ",\"online\":" + (online ? "true" : "false") + "}");
        });
}

#else

void WledController::Initialize() {}
void WledController::SetHost(const std::string& host) { (void)host; }

#endif  // CONFIG_ENABLE_WLED
