#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <lvgl.h>

struct CryptoQuote {
    int coin = 0;  // chỉ số trong bảng kCoins (market_crypto.cc)
    double last = 0;
    double pct = 0;   // % thay đổi 24 giờ
    double high = 0;  // cao nhất 24 giờ
    double low = 0;   // thấp nhất 24 giờ
};

// Xem giá Bitcoin / crypto qua MCP: lấy từ Binance, hiện thẻ giá trên TFT,
// trả câu tiếng Việt cho AI đọc. Thẻ tự làm mới và tự tắt (chỉnh ở đầu market_crypto.cc).
class MarketCrypto {
public:
    static MarketCrypto& GetInstance();

    // Gọi 1 lần khi khởi tạo MCP (cạnh TrafficCam::RegisterMcpTools).
    void RegisterMcpTools();

    // all = true: cả danh sách coin theo dõi, bỏ qua query.
    std::string Show(const std::string& query, bool all);
    void Close();

private:
    MarketCrypto();

    static void TaskEntry(void* arg);
    void Run();
    void Loop();

    bool FetchQuotes(const std::vector<int>& coins, std::vector<CryptoQuote>* out,
                     std::string* err);
    void RefreshVnd();
    bool FetchSpark(int coin, std::vector<float>* out);
    void RefreshSpark(const std::vector<CryptoQuote>& quotes);
    const std::vector<float>* SparkFor(const std::vector<CryptoQuote>& quotes) const;
    void DrawCard(const std::vector<CryptoQuote>& quotes, const std::vector<float>* spark);
    void HideOverlay();

    std::mutex mu_;
    std::vector<int> coins_;
    int64_t deadline_us_ = 0;
    bool running_ = false;       // được bảo vệ bởi mu_
    bool reply_pending_ = false; // được bảo vệ bởi mu_
    std::string result_text_;    // được bảo vệ bởi mu_
    SemaphoreHandle_t reply_sem_ = nullptr;
    std::atomic<bool> stop_{false};
    std::atomic<bool> refresh_now_{false};

    double usdt_vnd_ = 0;      // chỉ task dùng
    int64_t vnd_time_us_ = 0;  // chỉ task dùng
    int64_t next_refresh_us_ = 0;  // thời điểm làm mới tiếp theo (cho thanh tiến độ)

    std::vector<float> spark_;  // giá đóng cửa 24 nến 1 giờ của spark_coin_
    int spark_coin_ = -1;
    int64_t spark_time_us_ = 0;

    lv_obj_t* overlay_ = nullptr;  // chỉ đụng vào khi đã giữ khóa display
};