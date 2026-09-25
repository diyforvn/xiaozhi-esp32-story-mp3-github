#ifndef NETWORK_DEVICE_CONTROLLER_H
#define NETWORK_DEVICE_CONTROLLER_H

#include <string>
#include <vector>
#include <map>
#include <cstdint>

/*
 * NetworkDeviceController
 * Điều khiển các thiết bị HTTP trong mạng LAN:
 *   - Relay board / smart plug (HTTP GET toggle)
 *   - ESPHome device (REST API)
 *   - Tasmota (HTTP command)
 *   - Generic HTTP endpoint
 *
 * Thiết bị được lưu vào NVS theo dạng:
 *   netdev/0/name  = "đèn phòng khách"
 *   netdev/0/host  = "192.168.1.101"
 *   netdev/0/type  = "tasmota" | "esphome" | "relay" | "generic"
 *   netdev/0/extra = (tuỳ loại, vd: auth token ESPHome)
 */

struct NetworkDevice {
    int         id;
    std::string name;   // tên thân thiện, vd: "đèn phòng khách"
    std::string host;   // IP hoặc hostname
    std::string type;   // "tasmota" | "esphome" | "relay" | "generic"
    std::string extra;  // dữ liệu thêm: ESPHome API key, relay index, v.v.
    std::string room;   // PHASE 3: nhóm theo phòng, vd "phong_khach", "" = chưa gán
};

class NetworkDeviceController {
public:
    static NetworkDeviceController& GetInstance();

    void Initialize(); // load thiết bị từ NVS, đăng ký MCP tools

private:
    NetworkDeviceController() = default;

    std::vector<NetworkDevice> devices_;

    // NVS helpers
    void LoadDevices();
    void SaveDevice(const NetworkDevice& dev);
    void DeleteDevice(int id);
    int  NextId() const;

    // HTTP helper chung
    std::string HttpGet(const std::string& host, const std::string& path,
                        int timeout_s = 4);
    bool        HttpPost(const std::string& host, const std::string& path,
                         const std::string& body, int timeout_s = 4);

    // Online check (ping /  HEAD đơn giản)
    bool Ping(const std::string& host, int timeout_s = 2);

    // Ping CO CACHE theo tung device id (10 giay, giong WledController) -
    // dung cho moi lenh dieu khien de tranh cong them ~2s do ping that
    // truoc MOI lan bat/tat thiet bi. force=true bo qua cache.
    bool IsDeviceOnline(int device_id, const std::string& host, bool force = false);
    struct OnlineCacheEntry {
        bool online = false;
        int64_t last_check_us = 0;
    };
    std::map<int, OnlineCacheEntry> online_cache_;
    static constexpr int64_t kOnlineCacheUs = 10LL * 1000000; // 10 giay, giong WledController

    // Điều khiển từng loại thiết bị
    std::string ControlTasmota(const NetworkDevice& dev,
                                const std::string& command,
                                const std::string& value);
    std::string ControlEspHome(const NetworkDevice& dev,
                                const std::string& domain,
                                const std::string& entity_id,
                                const std::string& action);
    std::string ControlRelay(const NetworkDevice& dev,
                              int relay_index, bool state);
    std::string ControlGeneric(const NetworkDevice& dev,
                                const std::string& path,
                                const std::string& method,
                                const std::string& body);

    // Dieu khien from/toi (Tasmota/ESPHome/relay/generic) - dispatch theo
    // dev.type, dung chung cho home.device.set_power (1 thiet bi) va
    // ControlRoom() (nhieu thiet bi). Nem exception neu loi (giu nguyen
    // hanh vi cac Control* rieng le).
    std::string SetDevicePower(NetworkDevice& dev, bool state);

    // PHASE 3: dieu khien tat ca thiet bi cung 1 "room". Tra ve JSON tom
    // tat ket qua tung thiet bi (khong ném exception ra ngoai cho ca
    // nhom - 1 thiet bi loi khong duoc lam hong ket qua cac thiet bi con
    // lai, vi vay bat loi tung device rieng le ben trong).
    std::string ControlRoom(const std::string& room, bool state);

    void RegisterTools();
};

#endif // NETWORK_DEVICE_CONTROLLER_H
