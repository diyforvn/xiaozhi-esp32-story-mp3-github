#ifndef WLED_CONTROLLER_H
#define WLED_CONTROLLER_H

#include <string>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

class WledController {
public:
    static WledController& GetInstance();

    void Initialize();

    const std::string& GetHost() const { return host_; }
    void SetHost(const std::string& host);

    // Kiểm tra WLED có online không (non-blocking, dùng cache)
    bool IsOnline() const { return is_online_; }

private:
    WledController() = default;

    std::string host_;
    int segment_id_ = 0;

    // --- Online check state ---
    bool is_online_ = false;
    int64_t last_check_us_ = 0;          // thời điểm check cuối (esp_timer_get_time)
    static constexpr int64_t kCacheUs = 10LL * 1000000; // cache 10 giây

    // Ping WLED, cập nhật is_online_, trả về kết quả
    // force=true: bỏ qua cache, luôn ping thật
    bool CheckOnline(bool force = false);

    // Wrapper: check online rồi mới gọi, throw nếu offline
    std::string SafeHttpGet(const std::string& path);
    bool SafeHttpPost(const std::string& path, const std::string& json_body);

    std::string BuildUrl(const std::string& path) const;
    std::string HttpGet(const std::string& path);
    bool HttpPost(const std::string& path, const std::string& json_body);
    void RegisterTools();
};

#endif  // WLED_CONTROLLER_H
