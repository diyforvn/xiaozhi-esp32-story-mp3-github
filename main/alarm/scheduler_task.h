#pragma once

#include "alarm_manager.h"

// Task nền: mỗi 30 giây so sánh giờ hệ thống (đã sync NTP) với danh sách
// alarm trong AlarmManager, và trigger khi khớp.
namespace SchedulerTask {

    // Gọi 1 lần sau khi AlarmManager::Init() và NTP đã sẵn sàng (hoặc sẽ
    // tự bỏ qua kiểm tra cho tới khi NTP sync xong, xem check tm_year).
    void Start(AlarmManager* manager);

    // Dừng scheduler (hiếm khi cần, chủ yếu cho test/OTA trước reboot).
    void Stop();

}  // namespace SchedulerTask
