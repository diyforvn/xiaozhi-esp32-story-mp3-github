#ifndef NETWORK_DEVICE_CONTROLLER_H
#define NETWORK_DEVICE_CONTROLLER_H

#include <string>
#include <vector>
#include <map>

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

    void RegisterTools();
};

#endif // NETWORK_DEVICE_CONTROLLER_H
