#pragma once

#include "alarm_manager.h"

// Task rieng, doc AlarmEvent tu queue cua AlarmManager va thuc hien
// hanh dong "chu dong" (TTS / doi bieu cam Mochi / goi MCP tool noi bo).
// Chay o mot FreeRTOS task rieng de khong block esp_timer callback va
// khong dung LVGL truc tiep tu context timer (LVGL khong thread-safe).
namespace AlarmEventProcessor {

    // stack_size tinh bang byte, priority theo thang FreeRTOS thong thuong
    // cua project (vd tskIDLE_PRIORITY + 3).
    void Start(AlarmManager* manager, uint32_t stack_size = 4096, UBaseType_t priority = 5);

}  // namespace AlarmEventProcessor
