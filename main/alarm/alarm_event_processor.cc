// Task rieng, doc AlarmEvent tu queue va thuc hien hanh dong khi den gio:
// - Luon HIEN THI thong bao qua Application::Alert() (am thanh dang TAM
//   TAT, xem TODO ben duoi).
// - Neu action == "mcp_tool": goi lai 1 MCP tool DA CO SAN qua
//   McpServer::InvokeToolDirect() (API moi them, xem mcp_server.h/.cc) -
//   nhan lai callback thanh/bai RO RANG, roi HIEN KET QUA do len man
//   hinh. Khac voi ban truoc (dung ParseMessage() + JSON-RPC gia), cach
//   nay khong phu thuoc kenh SendMcpMessage() (kenh do khong ai lang nghe
//   luc alarm tu trigger, nen loi se bien mat am tham neu dung ParseMessage).

#include "alarm_event_processor.h"
#include "application.h"
#include "board.h"
#include "display/display.h"
#include "mcp_server.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include <cJSON.h>
#include <cinttypes>

static const char* TAG = "AlarmEventProcessor";

// Message mac dinh khi alarm khong co noi dung cu the.
static const char* kDefaultAlarmMessage = "Da den gio hen!";

// Goi lai 1 MCP tool da duoc dang ky san o noi khac trong code (vd
// self.wled.turn_on, home.device.set_power...), dung API InvokeToolDirect
// moi them vao McpServer - nhan duoc ket qua thanh/bai ro rang qua callback,
// khong con bi "mat tich" nhu cach dung ParseMessage() truoc day.
static void InvokeExistingMcpTool(uint32_t alarm_id, const std::string& tool_name,
                                   const std::string& args_json) {
    std::string safe_args = args_json.empty() ? "{}" : args_json;
    cJSON* args = cJSON_Parse(safe_args.c_str());
    if (args == nullptr) {
        ESP_LOGE(TAG, "Alarm id=%" PRIu32 ": device_args khong phai JSON hop le: %s",
                 alarm_id, safe_args.c_str());
        auto& app = Application::GetInstance();
        app.Schedule([&app, tool_name]() {
            app.Alert("Loi lich thiet bi",
                      ("Tham so khong hop le cho " + tool_name).c_str(),
                      "sad", std::string_view());
        });
        return;
    }

    ESP_LOGI(TAG, "Alarm id=%" PRIu32 ": goi MCP tool '%s' voi args %s",
             alarm_id, tool_name.c_str(), safe_args.c_str());

    McpServer::GetInstance().InvokeToolDirect(
        tool_name, args,
        [tool_name](bool success, const std::string& msg) {
            auto& app = Application::GetInstance();
            app.Schedule([&app, tool_name, success, msg]() {
                if (success) {
                    std::string display_msg = "Da thuc hien: " + tool_name;
                    app.Alert("Lich thiet bi", display_msg.c_str(), "happy", std::string_view());
                } else {
                    std::string display_msg = tool_name + ": " + msg;
                    ESP_LOGE(TAG, "Loi khi goi %s: %s", tool_name.c_str(), msg.c_str());
                    app.Alert("Loi lich thiet bi", display_msg.c_str(), "sad", std::string_view());
                }
            });
        });

    // InvokeToolDirect() dung xong "args" truoc khi return (phan validate
    // tham so chay dong bo; phan thuc thi tool that duoc Schedule() rieng
    // dua tren PropertyList da copy gia tri, khong con giu tham chieu toi
    // "args" nay nua) - nen xoa ngay o day la an toan, khong rac bo nho.
    cJSON_Delete(args);
}

static void ProcessorTaskFn(void* arg) {
    auto* manager = static_cast<AlarmManager*>(arg);
    QueueHandle_t queue = manager->event_queue();

    AlarmEvent evt;
    for (;;) {
        if (xQueueReceive(queue, &evt, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        // Fallback neu message rong (AI tao alarm ma khong co noi dung).
        const std::string& display_message =
            evt.message.empty() ? std::string(kDefaultAlarmMessage) : evt.message;

        ESP_LOGI(TAG, "Xu ly alarm event id=%" PRIu32 " action=%s: %s",
                 evt.alarm_id, evt.action.c_str(), display_message.c_str());

        auto& app = Application::GetInstance();

        // TAM THOI CHI HIEN THI LEN MAN HINH, KHONG PHAT AM THANH.
        // sound de rong (std::string_view()) -> Alert() se bo qua nhanh
        // audio_service_.PlaySound(). Khi nao xong phan debug am thanh,
        // doi std::string_view() thanh Lang::Sounds::OGG_SUCCESS (hoac
        // hang so OGG_XXX khac ban chon) de bat lai tieng bao.
        app.Schedule([&app, display_message]() {
            app.Alert("Bao thuc", display_message.c_str(), "happy", std::string_view());
        });

        // Neu alarm nay duoc tao de dieu khien 1 thiet bi da co san
        // (self.alarm.set voi device_tool khac rong) - goi lai tool that,
        // hien ket qua thanh/bai len man hinh.
        if (evt.action == "mcp_tool" && !evt.mcp_tool_name.empty()) {
            InvokeExistingMcpTool(evt.alarm_id, evt.mcp_tool_name, evt.mcp_tool_args);
        }
    }
}

namespace AlarmEventProcessor {

void Start(AlarmManager* manager, uint32_t stack_size, UBaseType_t priority) {
    xTaskCreate(ProcessorTaskFn, "alarm_evt_proc", stack_size, manager, priority, nullptr);
    ESP_LOGI(TAG, "AlarmEventProcessor task da khoi dong");
}

}  // namespace AlarmEventProcessor
