// Module khoi tao he thong alarm - goi InitAlarmSubsystem() tu app_main()
// that cua ban, SAU khi McpServer/DynamicToolLoader da san sang (thu tu
// nay khong con quan trong voi phan NTP nua - xem giai thich duoi day).
//
// FIX quan trong (2026-09): esp_sntp_init() KHONG duoc goi truc tiep trong
// InitAlarmSubsystem() nua, vi luc do stack mang (esp_netif/lwIP task) co
// the chua duoc khoi tao xong -> gay crash "assert failed: tcpip_callback
// ... Invalid mbox" ngay khi boot. Thay vao do, ta dang ky lang nghe
// IP_EVENT_STA_GOT_IP va chi goi InitNtp() sau khi Wi-Fi THAT SU co IP -
// dam bao stack mang da san sang, bat ke InitAlarmSubsystem() duoc goi
// som hay muon trong app_main().

#include "alarm_integration.h"
#include "alarm_manager.h"
#include "alarm_event_processor.h"
#include "scheduler_task.h"
#include "esp_sntp.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"

static const char* TAG = "AlarmIntegration";

void RegisterAlarmMcpTools();  // khai bao trong alarm_mcp_tools.cc

static void InitNtp() {
    setenv("TZ", "ICT-7", 1);  // gio Viet Nam, UTC+7, khong co DST
    tzset();

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "vn.pool.ntp.org");
    esp_sntp_setservername(1, "pool.ntp.org");
    esp_sntp_init();

    ESP_LOGI(TAG, "Da khoi dong dong bo NTP (sau khi co IP)");
}

// Chay khi Wi-Fi that su co IP - luc nay stack mang chac chan da san sang.
static void OnGotIp(void* /*arg*/, esp_event_base_t /*event_base*/,
                     int32_t /*event_id*/, void* /*event_data*/) {
    InitNtp();

    // Chi can sync 1 lan luc co IP dau tien - huy dang ky de tranh goi lai
    // esp_sntp_init() nhieu lan neu Wi-Fi reconnect (esp_sntp_init da chay
    // roi thi cu de no tu dong poll dinh ky, khong can init lai).
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &OnGotIp);
}

void InitAlarmSubsystem() {
    // 1. Dang ky cho Wi-Fi co IP roi moi dong bo NTP - KHONG goi InitNtp()
    //    truc tiep o day nua (xem ghi chu FIX o dau file).
    //    Yeu cau: esp_event_loop_create_default() phai da duoc goi truoc
    //    do (thuong la dong dau tien trong app_main() cua xiaozhi-esp32).
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &OnGotIp, nullptr);

    // 2. Load danh sach alarm tu NVS + tao event queue - khong phu thuoc
    //    mang, an toan goi som.
    g_alarm_manager.Init();

    // 3. Dang ky cac MCP tool de AI co the tao/xoa/liet ke alarm bang giong noi.
    RegisterAlarmMcpTools();

    // 4. Khoi dong task xu ly event (Alert/hien thi khi den gio).
    AlarmEventProcessor::Start(&g_alarm_manager);

    // 5. Khoi dong scheduler nen, tick moi 30s. SchedulerTask tu bo qua
    //    kiem tra cho toi khi thoi gian he thong hop le (nam >= 2024),
    //    tuc la cho toi khi OnGotIp() -> InitNtp() sync xong.
    SchedulerTask::Start(&g_alarm_manager);

    ESP_LOGI(TAG, "He thong alarm da san sang (cho Wi-Fi de dong bo gio)");
}