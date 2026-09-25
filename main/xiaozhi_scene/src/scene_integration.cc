#include "scene_integration.h"
#include "scene_manager.h"
#include "esp_log.h"

static const char* TAG = "SceneIntegration";

void RegisterSceneMcpTools();  // khai bao trong scene_mcp_tools.cc

void InitSceneSubsystem() {
    g_scene_manager.Init();
    RegisterSceneMcpTools();
    ESP_LOGI(TAG, "He thong scene da san sang");
}
