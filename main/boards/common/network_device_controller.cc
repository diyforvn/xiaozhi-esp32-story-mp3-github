/*
 * NetworkDeviceController
 * Điều khiển thiết bị HTTP trong mạng LAN qua MCP tools.
 *
 * Hỗ trợ:
 *   tasmota  — Tasmota firmware (relay, switch, plug)
 *   esphome  — ESPHome REST API
 *   relay    — ESP relay board HTTP đơn giản
 *   generic  — Bất kỳ HTTP endpoint nào
 */

#include "network_device_controller.h"
#include "mcp_server.h"
#include "board.h"
#include "settings.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <cJSON.h>
#include <algorithm>

#define TAG "NetDev"
#define MAX_DEVICES 16

// Helper: build full URL from host and path. If host already contains a scheme,
// use it directly; otherwise prepend http://. Also avoid duplicate slashes.
static std::string BuildUrlFromHost(const std::string& host, const std::string& path) {
    std::string base = host;
    if (base.rfind("http://", 0) != 0 && base.rfind("https://", 0) != 0) {
        base = "http://" + base;
    }
    if (!base.empty() && base.back() == '/' && !path.empty() && path.front() == '/') {
        return base.substr(0, base.size() - 1) + path;
    }
    return base + path;
}

// Helper: parse int an toan tu chuoi "extra" cua device, nem loi RO RANG
// thay vi de std::stoi tu nem std::invalid_argument mac dinh (thong bao
// loi kieu "stoi" khong co y nghia gi voi AI/nguoi dung).
static int ParseIntSafe(const std::string& s, const std::string& field_hint) {
    if (s.empty()) return 0;
    try {
        size_t pos = 0;
        int value = std::stoi(s, &pos);
        if (pos != s.size()) {
            throw std::runtime_error(
                "Invalid " + field_hint + ": '" + s + "' is not a plain integer");
        }
        return value;
    } catch (const std::invalid_argument&) {
        throw std::runtime_error(
            "Invalid " + field_hint + ": '" + s + "' is not a number. "
            "Check the device's 'extra' field (expected format: relay index, "
            "e.g. '0' or '0:onoff').");
    } catch (const std::out_of_range&) {
        throw std::runtime_error("Invalid " + field_hint + ": '" + s + "' is out of range");
    }
}

// ─────────────────────────────────────────────────────────────
// Singleton
// ─────────────────────────────────────────────────────────────

NetworkDeviceController& NetworkDeviceController::GetInstance() {
    static NetworkDeviceController instance;
    return instance;
}

void NetworkDeviceController::Initialize() {
    LoadDevices();
    RegisterTools();
    ESP_LOGI(TAG, "NetworkDeviceController initialized, %d device(s) loaded",
             (int)devices_.size());
}

// ─────────────────────────────────────────────────────────────
// NVS persistence
// ─────────────────────────────────────────────────────────────

void NetworkDeviceController::LoadDevices() {
    devices_.clear();
    for (int i = 0; i < MAX_DEVICES; i++) {
        std::string ns = "netdev_" + std::to_string(i);
        Settings s(ns);
        std::string name = s.GetString("name");
        if (name.empty()) continue;

        NetworkDevice dev;
        dev.id    = i;
        dev.name  = name;
        dev.host  = s.GetString("host");
        dev.type  = s.GetString("type", "generic");
        dev.extra = s.GetString("extra");
        dev.room  = s.GetString("room"); // PHASE 3: thiet bi cu chua co key nay -> "" (chua gan phong)
        devices_.push_back(dev);
        ESP_LOGI(TAG, "Loaded device[%d]: %s (%s) @ %s room=%s",
                 i, dev.name.c_str(), dev.type.c_str(), dev.host.c_str(), dev.room.c_str());
    }
}

void NetworkDeviceController::SaveDevice(const NetworkDevice& dev) {
    std::string ns = "netdev_" + std::to_string(dev.id);
    Settings s(ns, true);
    s.SetString("name",  dev.name);
    s.SetString("host",  dev.host);
    s.SetString("type",  dev.type);
    s.SetString("extra", dev.extra);
    s.SetString("room",  dev.room); // PHASE 3
}

void NetworkDeviceController::DeleteDevice(int id) {
    std::string ns = "netdev_" + std::to_string(id);
    Settings s(ns, true);
    s.SetString("name", ""); // đánh dấu slot trống
    s.EraseKey("host");
    s.EraseKey("type");
    s.EraseKey("extra");
    s.EraseKey("room"); // PHASE 3
}

int NetworkDeviceController::NextId() const {
    // Tìm slot trống nhỏ nhất
    for (int i = 0; i < MAX_DEVICES; i++) {
        bool used = false;
        for (auto& d : devices_) {
            if (d.id == i) { used = true; break; }
        }
        if (!used) return i;
    }
    return -1; // đầy
}

// ─────────────────────────────────────────────────────────────
// HTTP helpers
// ─────────────────────────────────────────────────────────────

bool NetworkDeviceController::Ping(const std::string& host, int timeout_s) {
    try {
        auto http = Board::GetInstance().GetNetwork()->CreateHttp(timeout_s);
        if (!http->Open("GET", BuildUrlFromHost(host, "/"))) {
            return false;
        }
        int code = http->GetStatusCode();
        http->Close();
        return (code > 0);
    } catch (...) {
        return false;
    }
}

bool NetworkDeviceController::IsDeviceOnline(int device_id, const std::string& host, bool force) {
    int64_t now = esp_timer_get_time();
    auto it = online_cache_.find(device_id);

    if (!force && it != online_cache_.end() &&
        (now - it->second.last_check_us) < kOnlineCacheUs) {
        return it->second.online;
    }

    bool reachable = Ping(host);
    online_cache_[device_id] = OnlineCacheEntry{reachable, now};
    ESP_LOGI(TAG, "Online cache updated: device[%d] @ %s -> %s",
             device_id, host.c_str(), reachable ? "ONLINE" : "OFFLINE");
    return reachable;
}

std::string NetworkDeviceController::HttpGet(const std::string& host,
                                              const std::string& path,
                                              int timeout_s) {
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(timeout_s);
    std::string url = BuildUrlFromHost(host, path);
    if (!http->Open("GET", url)) {
        throw std::runtime_error("Cannot connect to " + host);
    }
    int code = http->GetStatusCode();
    if (code != 200) {
        http->Close();
        throw std::runtime_error("HTTP " + std::to_string(code) + " from " + host);
    }
    std::string resp = http->ReadAll();
    http->Close();
    return resp;
}

bool NetworkDeviceController::HttpPost(const std::string& host,
                                        const std::string& path,
                                        const std::string& body,
                                        int timeout_s) {
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(timeout_s);
    http->SetHeader("Content-Type", "application/json");
    std::string url = BuildUrlFromHost(host, path);
    if (!http->Open("POST", url)) {
        throw std::runtime_error("Cannot connect to " + host);
    }
    // Write the JSON body after opening the connection
    http->Write(body.data(), body.size());
    http->Write("", 0); // Signal end of request body
    int code = http->GetStatusCode();
    http->Close();
    return (code == 200 || code == 204);
}

// ─────────────────────────────────────────────────────────────
// Device-specific control
// ─────────────────────────────────────────────────────────────

// Tasmota: GET /cm?cmnd=Power%20ON  hoặc  /cm?cmnd=Power%20TOGGLE
std::string NetworkDeviceController::ControlTasmota(const NetworkDevice& dev,
                                                     const std::string& command,
                                                     const std::string& value) {
    std::string path = "/cm?cmnd=" + command;
    if (!value.empty()) path += "%20" + value;
    ESP_LOGI(TAG, "Tasmota [%s] %s %s", dev.name.c_str(), command.c_str(), value.c_str());
    return HttpGet(dev.host, path);
}

// ESPHome REST API: POST /light/kitchen_light/turn_on
std::string NetworkDeviceController::ControlEspHome(const NetworkDevice& dev,
                                                     const std::string& domain,
                                                     const std::string& entity_id,
                                                     const std::string& action) {
    // extra field chứa API key nếu có
    std::string path = "/" + domain + "/" + entity_id + "/" + action;
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(5);
    if (!dev.extra.empty()) {
        http->SetHeader("Authorization", "Bearer " + dev.extra);
    }
    http->SetHeader("Content-Type", "application/json");
    std::string url = BuildUrlFromHost(dev.host, path);
    if (!http->Open("POST", url)) {
        throw std::runtime_error("Cannot connect to ESPHome at " + dev.host);
    }
    // Write the JSON body after opening the connection
    std::string empty_body = "{}";
    http->Write(empty_body.data(), empty_body.size());
    http->Write("", 0); // Signal end of request body
    int code = http->GetStatusCode();
    std::string resp = http->ReadAll();
    http->Close();
    if (code != 200) {
        throw std::runtime_error("ESPHome error " + std::to_string(code));
    }
    ESP_LOGI(TAG, "ESPHome [%s] %s/%s → %s",
             dev.name.c_str(), domain.c_str(), entity_id.c_str(), action.c_str());
    return resp;
}

// Relay board đơn giản: GET /relay/0?state=1  hoặc /relay/0/on
// extra = "simple" (default) | "onoff"  (tuỳ firmware relay board)
std::string NetworkDeviceController::ControlRelay(const NetworkDevice& dev,
                                                   int relay_index, bool state) {
    std::string path;
    if (dev.extra == "onoff") {
        // /relay/0/on  hoặc  /relay/0/off
        path = "/relay/" + std::to_string(relay_index) +
               (state ? "/on" : "/off");
    } else {
        // ?state=1 / ?state=0  (default)
        path = "/relay/" + std::to_string(relay_index) +
               "?state=" + (state ? "1" : "0");
    }
    ESP_LOGI(TAG, "Relay [%s] ch%d → %s",
             dev.name.c_str(), relay_index, state ? "ON" : "OFF");
    return HttpGet(dev.host, path);
}

// Generic: bất kỳ HTTP GET/POST
std::string NetworkDeviceController::ControlGeneric(const NetworkDevice& dev,
                                                     const std::string& path,
                                                     const std::string& method,
                                                     const std::string& body) {
    if (method == "POST") {
        HttpPost(dev.host, path, body);
        return "{\"ok\":true}";
    }
    return HttpGet(dev.host, path);
}

// Dispatch bat/tat theo loai thiet bi - tach ra tu logic von nam thang
// trong lambda "home.device.set_power" de ControlRoom() (Phase 3) dung
// lai duoc, khong sao chep code.
std::string NetworkDeviceController::SetDevicePower(NetworkDevice& dev, bool state) {
    if (dev.type == "tasmota") {
        return ControlTasmota(dev, "Power", state ? "ON" : "OFF");
    } else if (dev.type == "esphome") {
        // extra = "switch.my_switch" hoặc "light.my_light"
        auto& entity = dev.extra;
        auto dot = entity.find('.');
        std::string domain = (dot != std::string::npos) ? entity.substr(0, dot) : "switch";
        std::string eid    = (dot != std::string::npos) ? entity.substr(dot + 1) : entity;
        return ControlEspHome(dev, domain, eid, state ? "turn_on" : "turn_off");
    } else if (dev.type == "relay") {
        // extra = relay index, vd "0" hoặc "0:onoff"
        int relay_idx = 0;
        std::string relay_mode;
        auto colon = dev.extra.find(':');
        if (colon != std::string::npos) {
            relay_idx  = ParseIntSafe(dev.extra.substr(0, colon), "relay index in 'extra'");
            relay_mode = dev.extra.substr(colon + 1);
        } else if (!dev.extra.empty()) {
            relay_idx = ParseIntSafe(dev.extra, "relay index in 'extra'");
        }
        NetworkDevice dev_copy = dev;
        dev_copy.extra = relay_mode;
        return ControlRelay(dev_copy, relay_idx, state);
    } else {
        // generic: extra = path, vd "/switch?val=1"
        std::string path = dev.extra.empty() ? "/power" : dev.extra;
        return ControlGeneric(dev, path + (state ? "?state=1" : "?state=0"), "GET", "");
    }
}

// PHASE 3: dieu khien tat ca thiet bi cung 1 room. Loi tung thiet bi
// duoc bat rieng (khong throw ra ngoai) de 1 thiet bi hong khong lam
// dung ca nhom - AI/nguoi dung se biet duoc chinh xac thiet bi nao that
// bai va vi sao qua JSON tra ve.
std::string NetworkDeviceController::ControlRoom(const std::string& room, bool state) {
    cJSON* results = cJSON_CreateArray();
    int affected = 0;

    for (auto& dev : devices_) {
        if (dev.room != room) continue;
        affected++;

        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "name", dev.name.c_str());

        if (!IsDeviceOnline(dev.id, dev.host)) {
            cJSON_AddBoolToObject(item, "ok", false);
            cJSON_AddStringToObject(item, "error", "offline");
            cJSON_AddItemToArray(results, item);
            continue;
        }

        try {
            SetDevicePower(dev, state);
            cJSON_AddBoolToObject(item, "ok", true);
        } catch (const std::exception& e) {
            cJSON_AddBoolToObject(item, "ok", false);
            cJSON_AddStringToObject(item, "error", e.what());
        }
        cJSON_AddItemToArray(results, item);
    }

    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "room", room.c_str());
    cJSON_AddNumberToObject(root, "device_count", affected);
    cJSON_AddItemToObject(root, "results", results);

    char* s = cJSON_PrintUnformatted(root);
    std::string out(s);
    cJSON_free(s);
    cJSON_Delete(root);

    if (affected == 0) {
        ESP_LOGW(TAG, "ControlRoom: khong co thiet bi nao trong room '%s'", room.c_str());
    }
    return out;
}

// ─────────────────────────────────────────────────────────────
// MCP Tool registration
// ─────────────────────────────────────────────────────────────

void NetworkDeviceController::RegisterTools() {
    auto& mcp = McpServer::GetInstance();

    // ── LIST ────────────────────────────────────────────────
    mcp.AddTool("home.device.list",
        "List all registered smart home devices on the local network. "
        "Returns id, name, type, and host for each device. "
        "Call this first to find the device id before controlling a device. "
        "(Liệt kê thiết bị nhà thông minh đã đăng ký)",
        PropertyList(),
        [this](const PropertyList&) -> ReturnValue {
            cJSON* arr = cJSON_CreateArray();
            for (auto& dev : devices_) {
                cJSON* obj = cJSON_CreateObject();
                cJSON_AddNumberToObject(obj, "id",   dev.id);
                cJSON_AddStringToObject(obj, "name", dev.name.c_str());
                cJSON_AddStringToObject(obj, "type", dev.type.c_str());
                cJSON_AddStringToObject(obj, "host", dev.host.c_str());
                cJSON_AddStringToObject(obj, "room", dev.room.c_str());
                cJSON_AddItemToArray(arr, obj);
            }
            char* s = cJSON_PrintUnformatted(arr);
            std::string result(s);
            cJSON_free(s);
            cJSON_Delete(arr);
            return result;
        });

    // ── PING / STATUS ────────────────────────────────────────
    mcp.AddTool("home.device.ping",
        "Check if a specific device is reachable on the network. "
        "Use home.device.list to get device ids. "
        "(Kiểm tra thiết bị có online không)",
        PropertyList({
            Property("id", kPropertyTypeInteger, 0, MAX_DEVICES - 1)
        }),
        [this](const PropertyList& props) -> ReturnValue {
            int id = props["id"].value<int>();
            auto it = std::find_if(devices_.begin(), devices_.end(),
                [id](const NetworkDevice& d){ return d.id == id; });
            if (it == devices_.end()) {
                throw std::runtime_error("Device id " + std::to_string(id) + " not found");
            }
            bool online = IsDeviceOnline(it->id, it->host, /*force=*/true);
            return std::string(
                "{\"id\":" + std::to_string(id) +
                ",\"name\":\"" + it->name +
                "\",\"online\":" + (online ? "true" : "false") + "}");
        });

    // ── POWER (Tasmota / relay / generic ON/OFF) ─────────────
    mcp.AddTool("home.device.set_power",
        "Turn on or off a device on the local network. "
        "Supports Tasmota devices, relay boards, and ESPHome switches. "
        "Use home.device.list to get the device id. "
        "(Bật / tắt thiết bị trong mạng)",
        PropertyList({
            Property("id",    kPropertyTypeInteger, 0, MAX_DEVICES - 1),
            Property("state", kPropertyTypeBoolean)  // true=ON false=OFF
        }),
        [this](const PropertyList& props) -> ReturnValue {
            int  id    = props["id"].value<int>();
            bool state = props["state"].value<bool>();

            auto it = std::find_if(devices_.begin(), devices_.end(),
                [id](const NetworkDevice& d){ return d.id == id; });
            if (it == devices_.end()) {
                throw std::runtime_error("Device id " + std::to_string(id) + " not found");
            }

            if (!IsDeviceOnline(it->id, it->host)) {
                throw std::runtime_error(
                    "Device '" + it->name + "' is offline at " + it->host);
            }

            return SetDevicePower(*it, state);
        });

    // ── TASMOTA command tuỳ ý ────────────────────────────────
    mcp.AddTool("home.tasmota.command",
        "Send any Tasmota command to a registered device. "
        "Examples: command='Dimmer' value='80', command='Color' value='FF0000'. "
        "(Gửi lệnh Tasmota tuỳ ý)",
        PropertyList({
            Property("id",      kPropertyTypeInteger, 0, MAX_DEVICES - 1),
            Property("command", kPropertyTypeString),
            Property("value",   kPropertyTypeString, std::string(""))
        }),
        [this](const PropertyList& props) -> ReturnValue {
            int id = props["id"].value<int>();
            auto it = std::find_if(devices_.begin(), devices_.end(),
                [id](const NetworkDevice& d){ return d.id == id; });
            if (it == devices_.end()) {
                throw std::runtime_error("Device id " + std::to_string(id) + " not found");
            }
            if (it->type != "tasmota") {
                throw std::runtime_error("Device '" + it->name + "' is not a Tasmota device");
            }
            if (!IsDeviceOnline(it->id, it->host)) {
                throw std::runtime_error("Device '" + it->name + "' is offline");
            }
            return ControlTasmota(*it,
                props["command"].value<std::string>(),
                props["value"].value<std::string>());
        });

    // ── ESPHome action tuỳ ý ─────────────────────────────────
    mcp.AddTool("home.esphome.action",
        "Call any ESPHome REST API action on a registered device. "
        "Examples: domain='light' entity='kitchen' action='turn_on'. "
        "(Gọi ESPHome REST API)",
        PropertyList({
            Property("id",        kPropertyTypeInteger, 0, MAX_DEVICES - 1),
            Property("domain",    kPropertyTypeString), // light / switch / fan / climate
            Property("entity_id", kPropertyTypeString),
            Property("action",    kPropertyTypeString)  // turn_on / turn_off / toggle
        }),
        [this](const PropertyList& props) -> ReturnValue {
            int id = props["id"].value<int>();
            auto it = std::find_if(devices_.begin(), devices_.end(),
                [id](const NetworkDevice& d){ return d.id == id; });
            if (it == devices_.end()) {
                throw std::runtime_error("Device id " + std::to_string(id) + " not found");
            }
            if (it->type != "esphome") {
                throw std::runtime_error("Device '" + it->name + "' is not an ESPHome device");
            }
            if (!IsDeviceOnline(it->id, it->host)) {
                throw std::runtime_error("Device '" + it->name + "' is offline");
            }
            return ControlEspHome(*it,
                props["domain"].value<std::string>(),
                props["entity_id"].value<std::string>(),
                props["action"].value<std::string>());
        });

    // ── Generic HTTP call ─────────────────────────────────────
    mcp.AddTool("home.device.http_call",
        "Send a raw HTTP GET or POST request to a registered device. "
        "Useful for devices with custom firmware. "
        "(Gửi HTTP request tuỳ ý đến thiết bị)",
        PropertyList({
            Property("id",     kPropertyTypeInteger, 0, MAX_DEVICES - 1),
            Property("path",   kPropertyTypeString),
            Property("method", kPropertyTypeString, std::string("GET")),
            Property("body",   kPropertyTypeString, std::string(""))
        }),
        [this](const PropertyList& props) -> ReturnValue {
            int id = props["id"].value<int>();
            auto it = std::find_if(devices_.begin(), devices_.end(),
                [id](const NetworkDevice& d){ return d.id == id; });
            if (it == devices_.end()) {
                throw std::runtime_error("Device id " + std::to_string(id) + " not found");
            }
            if (!IsDeviceOnline(it->id, it->host)) {
                throw std::runtime_error("Device '" + it->name + "' is offline");
            }
            return ControlGeneric(*it,
                props["path"].value<std::string>(),
                props["method"].value<std::string>(),
                props["body"].value<std::string>());
        });

    // ── PHASE 3: ROOM CONTROL ─────────────────────────────────
    mcp.AddTool("home.room.control",
        "Turn on or off ALL devices assigned to the same room at once. "
        "Use home.device.list to see which room each device belongs to "
        "(field 'room' - devices with empty room are not in any group). "
        "(Bật/tắt tất cả thiết bị cùng 1 phòng)",
        PropertyList({
            Property("room",  kPropertyTypeString),
            Property("state", kPropertyTypeBoolean)
        }),
        [this](const PropertyList& props) -> ReturnValue {
            std::string room = props["room"].value<std::string>();
            bool state = props["state"].value<bool>();
            if (room.empty()) {
                throw std::runtime_error("Tham so 'room' khong duoc de trong");
            }
            return ControlRoom(room, state);
        });

    // ── REGISTER / UNREGISTER (user-only) ────────────────────
    mcp.AddUserOnlyTool("home.device.register",
        "Register a new smart home device. "
        "type: 'tasmota' | 'esphome' | 'relay' | 'generic'. "
        "extra: relay index for relay type (e.g. '0'), "
        "ESPHome entity for esphome type (e.g. 'switch.my_switch'), "
        "API key for ESPHome with auth. "
        "room: optional group name (e.g. 'phong_khach') for home.room.control. "
        "(Đăng ký thiết bị mới vào hệ thống)",
        PropertyList({
            Property("name",  kPropertyTypeString),
            Property("host",  kPropertyTypeString),
            Property("type",  kPropertyTypeString),
            Property("extra", kPropertyTypeString, std::string("")),
            Property("room",  kPropertyTypeString, std::string(""))
        }),
        [this](const PropertyList& props) -> ReturnValue {
            auto name  = props["name"].value<std::string>();
            auto host  = props["host"].value<std::string>();
            auto type  = props["type"].value<std::string>();
            auto extra = props["extra"].value<std::string>();
            auto room  = props["room"].value<std::string>();

            if (name.empty() || host.empty()) {
                throw std::runtime_error("name and host are required");
            }
            if (type != "tasmota" && type != "esphome" &&
                type != "relay"   && type != "generic") {
                throw std::runtime_error(
                    "Invalid type. Use: tasmota, esphome, relay, generic");
            }

            // Kiểm tra trùng tên
            for (auto& d : devices_) {
                if (d.name == name) {
                    throw std::runtime_error("Device '" + name + "' already registered");
                }
            }
            // Kiểm tra trùng host (cùng 1 địa chỉ IP đăng ký 2 lần với 2 tên
            // khác nhau sẽ gây nhầm lẫn khi AI liệt kê / điều khiển thiết bị)
            for (auto& d : devices_) {
                if (d.host == host) {
                    throw std::runtime_error(
                        "Host '" + host + "' is already registered as device '" + d.name + "'");
                }
            }

            int new_id = NextId();
            if (new_id < 0) {
                throw std::runtime_error("Device list is full (max " +
                                         std::to_string(MAX_DEVICES) + ")");
            }

            NetworkDevice dev{ new_id, name, host, type, extra, room };
            devices_.push_back(dev);
            SaveDevice(dev);

            // Ping để báo trạng thái ngay
            bool online = Ping(host);
            ESP_LOGI(TAG, "Registered device[%d]: %s (%s) @ %s, online=%s",
                     new_id, name.c_str(), type.c_str(), host.c_str(),
                     online ? "yes" : "no");

            return std::string(
                "{\"id\":" + std::to_string(new_id) +
                ",\"name\":\"" + name +
                "\",\"online\":" + (online ? "true" : "false") + "}");
        });

    mcp.AddUserOnlyTool("home.device.unregister",
        "Remove a registered device by id. (Xoá thiết bị đã đăng ký)",
        PropertyList({
            Property("id", kPropertyTypeInteger, 0, MAX_DEVICES - 1)
        }),
        [this](const PropertyList& props) -> ReturnValue {
            int id = props["id"].value<int>();
            auto it = std::find_if(devices_.begin(), devices_.end(),
                [id](const NetworkDevice& d){ return d.id == id; });
            if (it == devices_.end()) {
                throw std::runtime_error("Device id " + std::to_string(id) + " not found");
            }
            std::string name = it->name;
            devices_.erase(it);
            DeleteDevice(id);
            online_cache_.erase(id);
            ESP_LOGI(TAG, "Unregistered device[%d]: %s", id, name.c_str());
            return std::string("{\"removed\":\"" + name + "\"}");
        });
}
