#pragma once

#include <vector>
#include "alarm_manager.h"

// Đọc / ghi danh sách Alarm xuống NVS, serialize dạng JSON (dùng cJSON).
namespace AlarmStorage {

    bool Save(const std::vector<Alarm>& alarms);
    std::vector<Alarm> Load();

}  // namespace AlarmStorage
