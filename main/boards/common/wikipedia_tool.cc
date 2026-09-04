#include "wikipedia_tool.h"
#include "mcp_server.h"
#include "application.h"

#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <esp_log.h>
#include <cJSON.h>
#include <cctype>
#include <cstdio>

#define TAG "WikipediaTool"
 
namespace {

// Giới hạn kích thước buffer nhận về để tránh chiếm quá nhiều RAM/heap
// (bài tóm tắt REST API thường vài trăm đến vài nghìn byte, 8KB là dư).
constexpr size_t kMaxResponseSize = 8192;
constexpr int kHttpTimeoutMs = 5000;

struct HttpResponseBuffer {
    std::string data;
};

esp_err_t HttpEventHandler(esp_http_client_event_t* evt) {
    auto* buffer = static_cast<HttpResponseBuffer*>(evt->user_data);
    if (evt->event_id == HTTP_EVENT_ON_DATA && buffer != nullptr) {
        if (buffer->data.size() + evt->data_len <= kMaxResponseSize) {
            buffer->data.append(static_cast<char*>(evt->data), evt->data_len);
        } else {
            ESP_LOGW(TAG, "Response vượt giới hạn %zu byte, cắt bớt dữ liệu", kMaxResponseSize);
        }
    }
    return ESP_OK;
}

}  // namespace

std::string WikipediaTool::UrlEncode(const std::string& input) {
    std::string encoded;
    char buf[4];
    for (unsigned char c : input) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += static_cast<char>(c);
        } else if (c == ' ') {
            encoded += "%20";
        } else {
            std::snprintf(buf, sizeof(buf), "%%%02X", c);
            encoded += buf;
        }
    }
    return encoded;
}

std::string WikipediaTool::FetchSummary(const std::string& title) {
    HttpResponseBuffer buffer;
    std::string url = "https://vi.wikipedia.org/api/rest_v1/page/summary/" + UrlEncode(title);

    esp_http_client_config_t config = {};
    config.url = url.c_str();
    config.method = HTTP_METHOD_GET;
    config.crt_bundle_attach = esp_crt_bundle_attach;  // dùng cert bundle sẵn có của ESP-IDF cho HTTPS
    config.event_handler = HttpEventHandler;
    config.user_data = &buffer;
    config.timeout_ms = kHttpTimeoutMs;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == nullptr) {
        ESP_LOGE(TAG, "Không khởi tạo được HTTP client");
        return "";
    }

    // Bắt buộc: Wikimedia sẽ chặn/giới hạn request không có User-Agent định danh rõ ràng
    esp_http_client_set_header(client, "User-Agent", "XiaozhiRobotTest/0.1 (contact: your-email@example.com)");

    esp_err_t err = esp_http_client_perform(client);
    int status_code = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request thất bại: %s", esp_err_to_name(err));
        return "";
    }
    if (status_code != 200) {
        ESP_LOGW(TAG, "Wikipedia trả về status %d cho chủ đề \"%s\"", status_code, title.c_str());
        return "";
    }

    cJSON* json = cJSON_Parse(buffer.data.c_str());
    if (json == nullptr) {
        ESP_LOGE(TAG, "Không parse được JSON trả về từ Wikipedia");
        return "";
    }

    std::string extract;
    auto extract_item = cJSON_GetObjectItem(json, "extract");
    if (cJSON_IsString(extract_item)) {
        extract = extract_item->valuestring;
    }
    cJSON_Delete(json);

    return extract;
}

void WikipediaTool::Initialize() {
    auto& mcp_server = McpServer::GetInstance();

    mcp_server.AddTool(
        "self.wikipedia.search",
        "[CHỈ DÙNG ĐỂ TEST] Tra cứu tóm tắt một chủ đề trên Wikipedia tiếng Việt và đọc lại "
        "gần như nguyên văn đoạn tóm tắt. Công cụ này đọc thẳng nội dung Wikipedia, CHƯA qua "
        "kiểm duyệt hay biên tập cho trẻ em - chỉ dùng trong quá trình phát triển/test kỹ thuật, "
        "không bật trong bản sản phẩm giáo dục dành cho trẻ em.\n"
        "Args:\n"
        "  `chu_de`: Tên chủ đề cần tra cứu, ví dụ 'Sư tử', 'Hệ Mặt Trời', 'Núi lửa'.",
        PropertyList({
            Property("chu_de", kPropertyTypeString)
        }),
        [this](const PropertyList& properties) -> ReturnValue {
            // Gọi mạng có thể mất 1-3 giây, hạ ưu tiên tác vụ như tool camera.take_photo
            TaskPriorityReset priority_reset(1);

            auto chu_de = properties["chu_de"].value<std::string>();
            auto extract = FetchSummary(chu_de);

            if (extract.empty()) {
                return std::string("Không tìm thấy thông tin về \"" + chu_de +
                                    "\" trên Wikipedia, hoặc có lỗi kết nối mạng.");
            }
            return extract;
        });
}
