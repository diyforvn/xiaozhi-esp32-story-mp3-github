#include "automation_integration.h"

#include "schedule/automation_schedule_manager.h"
#include "scene_manager.h"

#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_sntp.h>

static const char* TAG = "Automation";

void RegisterSceneMcpTools();

static bool s_initialized = false;

static void InitNtp() {
    setenv("TZ", "ICT-7", 1);
    tzset();

    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "vn.pool.ntp.org");
    esp_sntp_setservername(1, "pool.ntp.org");
    esp_sntp_init();
    ESP_LOGI(TAG, "NTP da khoi dong sau khi Wi-Fi co IP");
}

static void OnGotIp(void*, esp_event_base_t, int32_t, void*) {
    InitNtp();
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &OnGotIp);
}

void InitAutomationSubsystem() {
    if (s_initialized) {
        ESP_LOGW(TAG, "Automation subsystem da duoc khoi tao");
        return;
    }
    s_initialized = true;

    // Alarm scheduler can NTP; dang ky IP callback truoc khi bat dau Wi-Fi.
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &OnGotIp, nullptr);

    SceneManager::GetInstance().Init();
#if CONFIG_MCP_ENABLE_SCENE_TOOLS
    RegisterSceneMcpTools();
#endif

    AutomationScheduleManager::GetInstance().Start();
    ESP_LOGI(TAG, "Unified scheduler va scene runner da khoi tao");
}
