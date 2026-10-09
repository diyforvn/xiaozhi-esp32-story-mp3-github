#include "traffic_cam.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>

#include <esp_crc.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "jpeg_decoder.h"  // component espressif/esp_jpeg

#include "board.h"
#include "display.h"
#include "mcp_server.h"

#define TAG "TrafficCam"

// ======================= CẤU HÌNH =======================

// Kích thước màn hình
static constexpr int kLcdW = 320;
static constexpr int kLcdH = 240;

// Cách lấp đầy màn hình:
//  kCover   = phủ kín, cắt bớt hai mép ảnh (đẹp nhất, không méo)
//  kStretch = phủ kín, kéo giãn (không mất gì nhưng méo nhẹ với ảnh 16:9)
//  kContain = giữ nguyên tỷ lệ, còn dải đen trên/dưới (như bản trước)
enum class Fit { kCover, kStretch, kContain };
static constexpr Fit kFit = Fit::kCover;

// Số điểm ảnh tối đa của ảnh giải mã trước khi phóng/thu về màn hình.
// Đệm này nằm trong PSRAM: kDecMaxPixels * 2 byte (640*480 = khoảng 600 KB).
static constexpr size_t kDecMaxPixels = 640 * 480;

// Nếu màu bị sai (đỏ <-> xanh, ảnh nhiễu) thì đổi giá trị này.
static constexpr bool kSwapBytes = false;

// BẢNG THỬ MÀN HÌNH: true = không tải ảnh, chỉ vẽ dải xám + dải R/G/B để kiểm tra panel.
static constexpr bool kTestPattern = false;

// Chỉnh màu bằng phần mềm. Giá trị dưới đây = KHÔNG chỉnh gì (ảnh gốc).
// Chỉ tăng sau khi đã biết nguyên nhân thật.
static constexpr int kSaturationQ8 = 256;  // 256 = giữ nguyên (333 ≈ x1.3)
static constexpr int kContrastQ8 = 256;    // 256 = giữ nguyên (282 ≈ x1.1)
static constexpr int kBrightnessAdd = 0;   // cộng thêm vào mỗi kênh (-40..40)
// Gamma >100 làm vùng giữa/tối đậm hơn (chỉ làm TỐI đi). 100 = tắt.
static constexpr int kGammaX100 = 100;

// Chu kỳ hỏi server. Chỉ vẽ lại khi ảnh thực sự đổi.
// Nên chỉnh bằng đúng chu kỳ web tự tải ảnh (xem tab Network trong DevTools).
static constexpr int kRefreshMs = 10000;
static constexpr int kAutoCloseMinutes = 5;       // tự tắt
static constexpr size_t kMaxJpegBytes = 256 * 1024;

static const char* kBaseUrl =
    "https://giaothong.hochiminhcity.gov.vn/render/ImageHandler.ashx?id=";

// Danh sách camera: THÊM TRỰC TIẾP Ở ĐÂY. Tên càng rõ ràng, AI chọn càng đúng.
struct CamEntry {
    const char* name;
    const char* id;
};
static const CamEntry kCameras[] = {
    {"Luỹ Bán Bích", "6623f1046f998a001b2527db"},  // đổi tên theo đúng vị trí
    {"Ngã tư Hàng Xanh", "5d9ddd49766c880017188c94"},
    {"Cầu Sài Gòn", "58abbf72bd82540010390ba4"},
};

// ========================================================

// Bỏ dấu tiếng Việt (UTF-8, dạng NFC) và đưa về chữ thường để so khớp tên camera.
static std::string Fold(const std::string& s) {
    static const struct {
        const char* from;
        char to;
    } kMap[] = {
    {"À",'a'}, {"Á",'a'}, {"Â",'a'}, {"Ã",'a'}, {"Ä",'a'}, {"Å",'a'}, {"Ç",'c'}, {"È",'e'},
    {"É",'e'}, {"Ê",'e'}, {"Ë",'e'}, {"Ì",'i'}, {"Í",'i'}, {"Î",'i'}, {"Ï",'i'}, {"Ñ",'n'},
    {"Ò",'o'}, {"Ó",'o'}, {"Ô",'o'}, {"Õ",'o'}, {"Ö",'o'}, {"Ù",'u'}, {"Ú",'u'}, {"Û",'u'},
    {"Ü",'u'}, {"Ý",'y'}, {"à",'a'}, {"á",'a'}, {"â",'a'}, {"ã",'a'}, {"ä",'a'}, {"å",'a'},
    {"ç",'c'}, {"è",'e'}, {"é",'e'}, {"ê",'e'}, {"ë",'e'}, {"ì",'i'}, {"í",'i'}, {"î",'i'},
    {"ï",'i'}, {"ñ",'n'}, {"ò",'o'}, {"ó",'o'}, {"ô",'o'}, {"õ",'o'}, {"ö",'o'}, {"ù",'u'},
    {"ú",'u'}, {"û",'u'}, {"ü",'u'}, {"ý",'y'}, {"ÿ",'y'}, {"Ā",'a'}, {"ā",'a'}, {"Ă",'a'},
    {"ă",'a'}, {"Ą",'a'}, {"ą",'a'}, {"Ć",'c'}, {"ć",'c'}, {"Ĉ",'c'}, {"ĉ",'c'}, {"Ċ",'c'},
    {"ċ",'c'}, {"Č",'c'}, {"č",'c'}, {"Ď",'d'}, {"ď",'d'}, {"Đ",'d'}, {"đ",'d'}, {"Ē",'e'},
    {"ē",'e'}, {"Ĕ",'e'}, {"ĕ",'e'}, {"Ė",'e'}, {"ė",'e'}, {"Ę",'e'}, {"ę",'e'}, {"Ě",'e'},
    {"ě",'e'}, {"Ĝ",'g'}, {"ĝ",'g'}, {"Ğ",'g'}, {"ğ",'g'}, {"Ġ",'g'}, {"ġ",'g'}, {"Ģ",'g'},
    {"ģ",'g'}, {"Ĥ",'h'}, {"ĥ",'h'}, {"Ĩ",'i'}, {"ĩ",'i'}, {"Ī",'i'}, {"ī",'i'}, {"Ĭ",'i'},
    {"ĭ",'i'}, {"Į",'i'}, {"į",'i'}, {"İ",'i'}, {"Ĵ",'j'}, {"ĵ",'j'}, {"Ķ",'k'}, {"ķ",'k'},
    {"Ĺ",'l'}, {"ĺ",'l'}, {"Ļ",'l'}, {"ļ",'l'}, {"Ľ",'l'}, {"ľ",'l'}, {"Ń",'n'}, {"ń",'n'},
    {"Ņ",'n'}, {"ņ",'n'}, {"Ň",'n'}, {"ň",'n'}, {"Ō",'o'}, {"ō",'o'}, {"Ŏ",'o'}, {"ŏ",'o'},
    {"Ő",'o'}, {"ő",'o'}, {"Ŕ",'r'}, {"ŕ",'r'}, {"Ŗ",'r'}, {"ŗ",'r'}, {"Ř",'r'}, {"ř",'r'},
    {"Ś",'s'}, {"ś",'s'}, {"Ŝ",'s'}, {"ŝ",'s'}, {"Ş",'s'}, {"ş",'s'}, {"Š",'s'}, {"š",'s'},
    {"Ţ",'t'}, {"ţ",'t'}, {"Ť",'t'}, {"ť",'t'}, {"Ũ",'u'}, {"ũ",'u'}, {"Ū",'u'}, {"ū",'u'},
    {"Ŭ",'u'}, {"ŭ",'u'}, {"Ů",'u'}, {"ů",'u'}, {"Ű",'u'}, {"ű",'u'}, {"Ų",'u'}, {"ų",'u'},
    {"Ŵ",'w'}, {"ŵ",'w'}, {"Ŷ",'y'}, {"ŷ",'y'}, {"Ÿ",'y'}, {"Ź",'z'}, {"ź",'z'}, {"Ż",'z'},
    {"ż",'z'}, {"Ž",'z'}, {"ž",'z'}, {"Ơ",'o'}, {"ơ",'o'}, {"Ư",'u'}, {"ư",'u'}, {"Ạ",'a'},
    {"ạ",'a'}, {"Ả",'a'}, {"ả",'a'}, {"Ấ",'a'}, {"ấ",'a'}, {"Ầ",'a'}, {"ầ",'a'}, {"Ẩ",'a'},
    {"ẩ",'a'}, {"Ẫ",'a'}, {"ẫ",'a'}, {"Ậ",'a'}, {"ậ",'a'}, {"Ắ",'a'}, {"ắ",'a'}, {"Ằ",'a'},
    {"ằ",'a'}, {"Ẳ",'a'}, {"ẳ",'a'}, {"Ẵ",'a'}, {"ẵ",'a'}, {"Ặ",'a'}, {"ặ",'a'}, {"Ẹ",'e'},
    {"ẹ",'e'}, {"Ẻ",'e'}, {"ẻ",'e'}, {"Ẽ",'e'}, {"ẽ",'e'}, {"Ế",'e'}, {"ế",'e'}, {"Ề",'e'},
    {"ề",'e'}, {"Ể",'e'}, {"ể",'e'}, {"Ễ",'e'}, {"ễ",'e'}, {"Ệ",'e'}, {"ệ",'e'}, {"Ỉ",'i'},
    {"ỉ",'i'}, {"Ị",'i'}, {"ị",'i'}, {"Ọ",'o'}, {"ọ",'o'}, {"Ỏ",'o'}, {"ỏ",'o'}, {"Ố",'o'},
    {"ố",'o'}, {"Ồ",'o'}, {"ồ",'o'}, {"Ổ",'o'}, {"ổ",'o'}, {"Ỗ",'o'}, {"ỗ",'o'}, {"Ộ",'o'},
    {"ộ",'o'}, {"Ớ",'o'}, {"ớ",'o'}, {"Ờ",'o'}, {"ờ",'o'}, {"Ở",'o'}, {"ở",'o'}, {"Ỡ",'o'},
    {"ỡ",'o'}, {"Ợ",'o'}, {"ợ",'o'}, {"Ụ",'u'}, {"ụ",'u'}, {"Ủ",'u'}, {"ủ",'u'}, {"Ứ",'u'},
    {"ứ",'u'}, {"Ừ",'u'}, {"ừ",'u'}, {"Ử",'u'}, {"ử",'u'}, {"Ữ",'u'}, {"ữ",'u'}, {"Ự",'u'},
    {"ự",'u'}, {"Ỳ",'y'}, {"ỳ",'y'}, {"Ỵ",'y'}, {"ỵ",'y'}, {"Ỷ",'y'}, {"ỷ",'y'}, {"Ỹ",'y'},
    {"ỹ",'y'},
    };
    std::string out;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) {
            out += static_cast<char>(std::tolower(c));
            i++;
            continue;
        }
        bool matched = false;
        for (const auto& m : kMap) {
            size_t n = std::strlen(m.from);
            if (s.compare(i, n, m.from) == 0) {
                out += m.to;
                i += n;
                matched = true;
                break;
            }
        }
        if (!matched) {
            out += s[i];
            i++;
        }
    }
    return out;
}

static const CamEntry* FindCamera(const std::string& query) {
    const std::string q = Fold(query);
    for (const auto& c : kCameras) {
        if (Fold(c.name) == q) return &c;
    }
    for (const auto& c : kCameras) {
        if (Fold(c.name).find(q) != std::string::npos) return &c;
    }
    return nullptr;
}

static uint8_t* AllocPsram(size_t n) {
    void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return static_cast<uint8_t*>(p);
}

// Tăng độ bão hòa + tương phản trên buffer RGB565 (little-endian), đồng thời đổi byte nếu cần.
static void BoostColors(uint16_t* px, size_t n) {
    uint8_t lut[256];
    for (int i = 0; i < 256; i++) {
        int v = ((i - 128) * kContrastQ8 >> 8) + 128 + kBrightnessAdd;
        v = v < 0 ? 0 : (v > 255 ? 255 : v);
        float g = powf(v / 255.0f, kGammaX100 / 100.0f) * 255.0f + 0.5f;
        lut[i] = (uint8_t)g;
    }
    for (size_t i = 0; i < n; i++) {
        uint16_t p = px[i];
        int r = (p >> 11) & 0x1F, g = (p >> 5) & 0x3F, b = p & 0x1F;
        r = (r << 3) | (r >> 2);
        g = (g << 2) | (g >> 4);
        b = (b << 3) | (b >> 2);
        int y = (77 * r + 150 * g + 29 * b) >> 8;
        r = y + (((r - y) * kSaturationQ8) >> 8);
        g = y + (((g - y) * kSaturationQ8) >> 8);
        b = y + (((b - y) * kSaturationQ8) >> 8);
        r = lut[r < 0 ? 0 : (r > 255 ? 255 : r)];
        g = lut[g < 0 ? 0 : (g > 255 ? 255 : g)];
        b = lut[b < 0 ? 0 : (b > 255 ? 255 : b)];
        uint16_t o = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
        px[i] = kSwapBytes ? (uint16_t)((o >> 8) | (o << 8)) : o;
    }
}

// BEGIN-RESAMPLE
// Đưa ảnh đã giải mã (dw x dh, RGB565) vào buffer màn hình kLcdW x kLcdH theo kFit,
// nội suy song tuyến để ảnh mịn khi phóng hoặc thu.
static void ResampleToScreen(const uint16_t* src, int dw, int dh, uint16_t* dst) {
    float sx0 = 0, sy0 = 0, sw = (float)dw, sh = (float)dh;
    int ox = 0, oy = 0, ow = kLcdW, oh = kLcdH;
    if (kFit == Fit::kCover) {
        float s = std::max((float)kLcdW / dw, (float)kLcdH / dh);
        sw = kLcdW / s;
        sh = kLcdH / s;
        sx0 = (dw - sw) * 0.5f;
        sy0 = (dh - sh) * 0.5f;
    } else if (kFit == Fit::kContain) {
        float s = std::min((float)kLcdW / dw, (float)kLcdH / dh);
        ow = std::max(1, std::min(kLcdW, (int)(dw * s + 0.5f)));
        oh = std::max(1, std::min(kLcdH, (int)(dh * s + 0.5f)));
        ox = (kLcdW - ow) / 2;
        oy = (kLcdH - oh) / 2;
        memset(dst, 0, (size_t)kLcdW * kLcdH * 2);
    }

    const int step_x = (int)(sw * 65536.0f / ow);
    const int step_y = (int)(sh * 65536.0f / oh);
    const int base_x = (int)(sx0 * 65536.0f) - 32768 + step_x / 2;
    const int base_y = (int)(sy0 * 65536.0f) - 32768 + step_y / 2;
    const int max_x = (dw - 1) << 16;
    const int max_y = (dh - 1) << 16;

    for (int y = 0; y < oh; y++) {
        int fy = base_y + y * step_y;
        fy = fy < 0 ? 0 : (fy > max_y ? max_y : fy);
        const int y0 = fy >> 16;
        const int y1 = std::min(y0 + 1, dh - 1);
        const int wy = (fy >> 8) & 255;
        const uint16_t* r0 = src + (size_t)y0 * dw;
        const uint16_t* r1 = src + (size_t)y1 * dw;
        uint16_t* d = dst + (size_t)(oy + y) * kLcdW + ox;

        for (int x = 0; x < ow; x++) {
            int fx = base_x + x * step_x;
            fx = fx < 0 ? 0 : (fx > max_x ? max_x : fx);
            const int x0 = fx >> 16;
            const int x1 = std::min(x0 + 1, dw - 1);
            const int wx = (fx >> 8) & 255;
            const int p00 = r0[x0], p01 = r0[x1], p10 = r1[x0], p11 = r1[x1];

            auto blend = [&](int shift, int mask) {
                int top = ((p00 >> shift) & mask) * (256 - wx) + ((p01 >> shift) & mask) * wx;
                int bot = ((p10 >> shift) & mask) * (256 - wx) + ((p11 >> shift) & mask) * wx;
                return (top * (256 - wy) + bot * wy + 32768) >> 16;
            };
            d[x] = (uint16_t)((blend(11, 31) << 11) | (blend(5, 63) << 5) | blend(0, 31));
        }
    }
}
// END-RESAMPLE

// Dải thử: 4 dải (xám, đỏ, xanh lá, xanh dương) x 16 bậc từ đen đến sáng nhất.
static void FillTestPattern(uint8_t* buf) {
    uint16_t* px = reinterpret_cast<uint16_t*>(buf);
    for (int y = 0; y < kLcdH; y++) {
        int band = y / (kLcdH / 4);
        for (int x = 0; x < kLcdW; x++) {
            int step = x / (kLcdW / 16);
            int v = step * 17;  // 0..255
            int r = 0, g = 0, b = 0;
            if (band == 0) r = g = b = v;
            else if (band == 1) r = v;
            else if (band == 2) g = v;
            else b = v;
            uint16_t o = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
            px[y * kLcdW + x] = kSwapBytes ? (uint16_t)((o >> 8) | (o << 8)) : o;
        }
    }
}

TrafficCam& TrafficCam::GetInstance() {
    static TrafficCam inst;
    return inst;
}

// ---------------------- MCP ----------------------

void TrafficCam::RegisterMcpTools() {
    auto& mcp = McpServer::GetInstance();

    std::string names;
    for (const auto& c : kCameras) {
        names += c.name;
        names += "; ";
    }

    mcp.AddTool(
        "self.traffic_cam.show",
        "Hiển thị ảnh camera giao thông TP.HCM lên màn hình, tự làm mới mỗi " +
        std::to_string(kRefreshMs / 1000) + " giây và tự tắt sau " +
        std::to_string(kAutoCloseMinutes) + " phút. Các camera hiện có: " + names +
        "Tham số camera phải là một tên trong danh sách này.",
        PropertyList({Property("camera", kPropertyTypeString)}),
        [this](const PropertyList& properties) -> ReturnValue {
            return Show(properties["camera"].value<std::string>());
        });

    mcp.AddTool("self.traffic_cam.close",
                "Tắt màn hình camera giao thông và quay lại giao diện bình thường.",
                PropertyList(), [this](const PropertyList&) -> ReturnValue {
                    Close();
                    return true;
                });
}

std::string TrafficCam::Show(const std::string& query) {
    const CamEntry* cam = FindCamera(query);
    if (!cam) {
        std::string msg = "Không tìm thấy camera này. Các camera có: ";
        for (const auto& c : kCameras) {
            msg += c.name;
            msg += "; ";
        }
        return msg;
    }

    std::lock_guard<std::mutex> lk(mu_);
    cam_id_ = cam->id;
    cam_name_ = cam->name;
    deadline_us_ = esp_timer_get_time() + int64_t(kAutoCloseMinutes) * 60 * 1000000;
    refresh_now_ = true;

    if (!running_) {
        stop_ = false;
        running_ = true;
        if (xTaskCreate(&TaskEntry, "traffic_cam", 10 * 1024, this, 3, nullptr) != pdPASS) {
            running_ = false;
            return "Không tạo được tác vụ xem camera (thiếu RAM).";
        }
    }
    return std::string("Đang tải ảnh camera ") + cam->name;
}

void TrafficCam::Close() {
    stop_ = true;
}

// ---------------------- Task ----------------------

void TrafficCam::TaskEntry(void* arg) {
    static_cast<TrafficCam*>(arg)->Run();
    vTaskDelete(nullptr);
}

void TrafficCam::Run() {
    jpeg_buf_ = AllocPsram(kMaxJpegBytes);
    dec_buf_ = AllocPsram(kDecMaxPixels * 2);
    uint8_t* frames[2] = {AllocPsram(kLcdW * kLcdH * 2), AllocPsram(kLcdW * kLcdH * 2)};

    if (!jpeg_buf_ || !dec_buf_ || !frames[0] || !frames[1]) {
        ESP_LOGE(TAG, "Không đủ PSRAM");
    } else {
        int cur = 0;
        std::string last_id;
        uint32_t last_crc = 0;
        size_t last_len = 0;
        while (!stop_) {
            std::string id;
            int64_t deadline;
            {
                std::lock_guard<std::mutex> lk(mu_);
                id = cam_id_;
                deadline = deadline_us_;
            }
            if (esp_timer_get_time() > deadline) break;

            if (kTestPattern) {
                FillTestPattern(frames[0]);
                ShowFrame(frames[0], kLcdW, kLcdH);
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }

            refresh_now_ = false;
            size_t len = 0;
            int w = 0, h = 0;
            if (FetchJpeg(id, &len)) {
                uint32_t crc = esp_crc32_le(0, jpeg_buf_, len);
                if (id == last_id && len == last_len && crc == last_crc) {
                    ESP_LOGI(TAG, "Ảnh chưa đổi, bỏ qua");
                } else if (Decode(len, frames[cur], &w, &h)) {
                    ShowFrame(frames[cur], w, h);
                    cur ^= 1;
                    last_id = id;
                    last_len = len;
                    last_crc = crc;
                }
            }

            for (int i = 0; i < kRefreshMs / 200 && !stop_ && !refresh_now_; i++) {
                vTaskDelay(pdMS_TO_TICKS(200));
            }
        }
    }

    HideOverlay();
    heap_caps_free(jpeg_buf_);
    jpeg_buf_ = nullptr;
    heap_caps_free(dec_buf_);
    dec_buf_ = nullptr;
    heap_caps_free(frames[0]);
    heap_caps_free(frames[1]);

    std::lock_guard<std::mutex> lk(mu_);
    running_ = false;
}

// ---------------------- HTTP ----------------------

bool TrafficCam::FetchJpeg(const std::string& id, size_t* out_len) {
    const std::string url = std::string(kBaseUrl) + id;

    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 8000;
    cfg.buffer_size = 2048;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;

    bool ok = false;
    size_t total = 0;
    do {
        if (esp_http_client_open(c, 0) != ESP_OK) {
            ESP_LOGW(TAG, "Không kết nối được");
            break;
        }
        esp_http_client_fetch_headers(c);
        int status = esp_http_client_get_status_code(c);
        if (status != 200) {
            ESP_LOGW(TAG, "HTTP %d", status);
            break;
        }

        bool read_err = false;
        while (true) {
            if (total >= kMaxJpegBytes) {
                ESP_LOGW(TAG, "Ảnh lớn hơn %u byte", (unsigned)kMaxJpegBytes);
                read_err = true;
                break;
            }
            int n = esp_http_client_read(c, reinterpret_cast<char*>(jpeg_buf_) + total,
                                         kMaxJpegBytes - total);
            if (n < 0) {
                read_err = true;
                break;
            }
            if (n == 0) break;
            total += n;
        }
        if (read_err) break;

        if (total < 1000 || jpeg_buf_[0] != 0xFF || jpeg_buf_[1] != 0xD8) {
            ESP_LOGW(TAG, "Dữ liệu nhận về không phải JPEG (%u byte)", (unsigned)total);
            break;
        }
        ok = true;
    } while (false);

    esp_http_client_close(c);
    esp_http_client_cleanup(c);

    if (ok) {
        ESP_LOGI(TAG, "Tải xong %u byte", (unsigned)total);
        *out_len = total;
    }
    return ok;
}

// ---------------------- JPEG ----------------------

bool TrafficCam::Decode(size_t jpeg_len, uint8_t* out, int* w, int* h) {
    esp_jpeg_image_cfg_t cfg = {};
    cfg.indata = jpeg_buf_;
    cfg.indata_size = jpeg_len;

    esp_jpeg_image_output_t info = {};
    if (esp_jpeg_get_image_info(&cfg, &info) != ESP_OK) {
        ESP_LOGW(TAG, "Không đọc được header JPEG (có thể là progressive JPEG)");
        return false;
    }

    static const esp_jpeg_image_scale_t kScales[] = {JPEG_IMAGE_SCALE_0, JPEG_IMAGE_SCALE_1_2,
                                                     JPEG_IMAGE_SCALE_1_4, JPEG_IMAGE_SCALE_1_8};
    auto dim = [](int shift, int v) { return (v + (1 << shift) - 1) >> shift; };

    // Chọn mức thu nhỏ lớn nhất mà ảnh giải mã vẫn >= màn hình (giữ độ nét),
    // rồi thu thêm nếu vượt quá đệm giải mã.
    int pick = 0;
    for (int s = 3; s >= 0; s--) {
        if (dim(s, info.width) >= kLcdW && dim(s, info.height) >= kLcdH) {
            pick = s;
            break;
        }
    }
    while (pick < 3 && (size_t)dim(pick, info.width) * dim(pick, info.height) > kDecMaxPixels) {
        pick++;
    }
    if ((size_t)dim(pick, info.width) * dim(pick, info.height) > kDecMaxPixels) {
        ESP_LOGW(TAG, "Ảnh %dx%d quá lớn", (int)info.width, (int)info.height);
        return false;
    }

    cfg.outbuf = dec_buf_;
    cfg.outbuf_size = kDecMaxPixels * 2;
    cfg.out_format = JPEG_IMAGE_FORMAT_RGB565;
    cfg.out_scale = kScales[pick];
    cfg.flags.swap_color_bytes = 0;  // việc đổi byte làm trong BoostColors

    esp_jpeg_image_output_t res = {};
    if (esp_jpeg_decode(&cfg, &res) != ESP_OK) {
        ESP_LOGW(TAG, "Giải mã JPEG lỗi");
        return false;
    }

    uint16_t* out16 = reinterpret_cast<uint16_t*>(out);
    ResampleToScreen(reinterpret_cast<const uint16_t*>(dec_buf_), res.width, res.height, out16);
    BoostColors(out16, (size_t)kLcdW * kLcdH);
    *w = kLcdW;
    *h = kLcdH;
    ESP_LOGI(TAG, "Nguồn %dx%d -> giải mã %dx%d (1/%d) -> màn %dx%d", (int)info.width,
             (int)info.height, (int)res.width, (int)res.height, 1 << pick, kLcdW, kLcdH);
    return true;
}

// ---------------------- LVGL ----------------------

void TrafficCam::ShowFrame(uint8_t* buf, int w, int h) {
    auto display = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(display);

#if LVGL_VERSION_MAJOR >= 9
    if (!overlay_) {
        overlay_ = lv_obj_create(lv_layer_top());
        lv_obj_set_size(overlay_, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(overlay_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(overlay_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(overlay_, 0, 0);
        lv_obj_set_style_radius(overlay_, 0, 0);
        lv_obj_set_style_pad_all(overlay_, 0, 0);
        lv_obj_remove_flag(overlay_, LV_OBJ_FLAG_SCROLLABLE);
        img_ = lv_image_create(overlay_);
    }
    dsc_.header.magic = LV_IMAGE_HEADER_MAGIC;
    dsc_.header.cf = LV_COLOR_FORMAT_RGB565;
    dsc_.header.flags = 0;
    dsc_.header.w = w;
    dsc_.header.h = h;
    dsc_.header.stride = w * 2;
    dsc_.data_size = w * h * 2;
    dsc_.data = buf;
    lv_image_set_src(img_, &dsc_);
#else
    if (!overlay_) {
        overlay_ = lv_obj_create(lv_layer_top());
        lv_obj_set_size(overlay_, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_color(overlay_, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(overlay_, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(overlay_, 0, 0);
        lv_obj_set_style_radius(overlay_, 0, 0);
        lv_obj_set_style_pad_all(overlay_, 0, 0);
        lv_obj_clear_flag(overlay_, LV_OBJ_FLAG_SCROLLABLE);
        img_ = lv_img_create(overlay_);
    }
    dsc_.header.always_zero = 0;
    dsc_.header.cf = LV_IMG_CF_TRUE_COLOR;
    dsc_.header.w = w;
    dsc_.header.h = h;
    dsc_.data_size = w * h * 2;
    dsc_.data = buf;
    lv_img_set_src(img_, &dsc_);
#endif
    lv_obj_center(img_);
    lv_obj_invalidate(overlay_);
}

void TrafficCam::HideOverlay() {
    auto display = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(display);
    if (overlay_) {
        lv_obj_del(overlay_);
        overlay_ = nullptr;
        img_ = nullptr;
    }
}