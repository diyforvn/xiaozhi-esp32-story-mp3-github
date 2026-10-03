#pragma once

#include <atomic>
#include <mutex>
#include <string>

#include <lvgl.h>

// Xem camera giao thông TP.HCM trên màn hình TFT qua MCP.
// Tải ảnh JPEG trực tiếp từ giaothong.hochiminhcity.gov.vn, giải mã, hiện toàn màn hình,
// tự làm mới mỗi 10 giây, tự tắt sau 5 phút (chỉnh ở đầu traffic_cam.cc).
class TrafficCam {
public:
    static TrafficCam& GetInstance();

    // Gọi 1 lần khi khởi tạo MCP (cuối McpServer::AddCommonTools).
    void RegisterMcpTools();

    // Trả về chuỗi thông báo cho AI đọc lại.
    std::string Show(const std::string& query);
    void Close();

private:
    TrafficCam() = default;

    static void TaskEntry(void* arg);
    void Run();

    bool FetchJpeg(const std::string& id, size_t* out_len);
    bool Decode(size_t jpeg_len, uint8_t* out, int* w, int* h);
    void ShowFrame(uint8_t* buf, int w, int h);
    void HideOverlay();

    std::mutex mu_;
    std::string cam_id_;
    std::string cam_name_;
    int64_t deadline_us_ = 0;
    bool running_ = false;  // được bảo vệ bởi mu_
    std::atomic<bool> stop_{false};
    std::atomic<bool> refresh_now_{false};

    uint8_t* jpeg_buf_ = nullptr;  // chỉ task dùng


    lv_obj_t* overlay_ = nullptr;
    lv_obj_t* img_ = nullptr;
#if LVGL_VERSION_MAJOR >= 9
    lv_image_dsc_t dsc_{};
#else
    lv_img_dsc_t dsc_{};
#endif
};