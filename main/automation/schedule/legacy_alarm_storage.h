#pragma once

#include <cstdint>
#include <string>
#include <vector>

struct LegacyAlarm {
    uint32_t id = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    std::string repeat;
    uint8_t weekday_mask = 0;
    std::string message;
    std::string action;
    std::string device_tool;
    std::string device_args;
    bool enabled = true;
};

namespace LegacyAlarmStorage {
std::vector<LegacyAlarm> Load();
bool Clear();
}  // namespace LegacyAlarmStorage
