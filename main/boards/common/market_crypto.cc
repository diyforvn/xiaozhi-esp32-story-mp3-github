#include "market_crypto.h"

#include <cJSON.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "board.h"
#include "display.h"
#include "mcp_server.h"
#if CONFIG_MCP_ENABLE_TRAFFIC_CAM_TOOLS
#include "traffic_cam.h"
#endif

#define TAG "MarketCrypto"

// ======================= CẤU HÌNH =======================

static constexpr int kRefreshMs = 30000;       // làm mới giá khi thẻ đang hiện
static constexpr int kAutoCloseMinutes = 2;    // tự tắt thẻ
static constexpr bool kShowVnd = true;         // đọc thêm giá quy đổi VND (lấy tỷ giá USDT từ CoinGecko)
static constexpr int64_t kVndCacheUs = 10LL * 60 * 1000000;  // 10 phút
static constexpr int64_t kSparkCacheUs = 5LL * 60 * 1000000;  // 5 phút: lấy lại biểu đồ 24 giờ
// Dùng mũi tên ▲▼ (ký hiệu có sẵn trong font Montserrat của LVGL). Nếu thấy ô vuông thì đổi thành false.
static constexpr bool kUseArrowSymbols = true;

// Danh sách coin: THÊM TRỰC TIẾP Ở ĐÂY (symbol là cặp USDT trên Binance).
// Danh sách này cũng là danh sách "theo dõi": tối đa 6 coin để thẻ vừa màn 320x240.
struct Coin {
    const char* name;
    const char* ticker;
    const char* symbol;
    uint32_t color;  // màu huy hiệu
};
static const Coin kCoins[] = {
    {"Bitcoin", "BTC", "BTCUSDT", 0xF7931A},
    {"Ethereum", "ETH", "ETHUSDT", 0x627EEA},
    {"BNB", "BNB", "BNBUSDT", 0xF3BA2F},
    {"Solana", "SOL", "SOLUSDT", 0x9945FF},
    {"XRP", "XRP", "XRPUSDT", 0x3C4A5C},
    {"Dogecoin", "DOGE", "DOGEUSDT", 0xC2A633},
};
static constexpr int kNumCoins = sizeof(kCoins) / sizeof(kCoins[0]);

// Màu giao diện
static constexpr uint32_t kColUp = 0x16C784;
static constexpr uint32_t kColDown = 0xEA3943;
static constexpr uint32_t kColFlat = 0xF0B90B;
static constexpr uint32_t kColGray = 0x8A93A8;
static constexpr uint32_t kColWhite = 0xFFFFFF;
static constexpr uint32_t kBgTop = 0x0B1020;
static constexpr uint32_t kBgBottom = 0x1A2348;
static constexpr uint32_t kRowBg = 0x1C2547;
static constexpr uint32_t kAccent = 0x4C6FFF;
static constexpr uint32_t kBarTrack = 0x222B4A;

// Font: dùng Montserrat nếu bản LVGL của bạn đã bật, không thì rơi về font mặc định.
// Muốn đẹp nhất, bật trong menuconfig: LVGL -> Font usage -> Montserrat 14, 16, 20, 28, 40.
#if LV_FONT_MONTSERRAT_40
#define FONT_HUGE (&lv_font_montserrat_40)
#elif LV_FONT_MONTSERRAT_32
#define FONT_HUGE (&lv_font_montserrat_32)
#elif LV_FONT_MONTSERRAT_28
#define FONT_HUGE (&lv_font_montserrat_28)
#else
#define FONT_HUGE LV_FONT_DEFAULT
#endif
#if LV_FONT_MONTSERRAT_20
#define FONT_MED (&lv_font_montserrat_20)
#elif LV_FONT_MONTSERRAT_18
#define FONT_MED (&lv_font_montserrat_18)
#elif LV_FONT_MONTSERRAT_16
#define FONT_MED (&lv_font_montserrat_16)
#else
#define FONT_MED LV_FONT_DEFAULT
#endif
#if LV_FONT_MONTSERRAT_16
#define FONT_SMALL (&lv_font_montserrat_16)
#elif LV_FONT_MONTSERRAT_14
#define FONT_SMALL (&lv_font_montserrat_14)
#else
#define FONT_SMALL LV_FONT_DEFAULT
#endif

#if LVGL_VERSION_MAJOR >= 9
#define MC_NO_SCROLL(o) lv_obj_remove_flag((o), LV_OBJ_FLAG_SCROLLABLE)
#else
#define MC_NO_SCROLL(o) lv_obj_clear_flag((o), LV_OBJ_FLAG_SCROLLABLE)
#endif

// ======================= ĐỊNH DẠNG SỐ / CÂU =======================
// BEGIN-PURE

// Định dạng số: vi = true -> 67.421,33 ; vi = false -> 67,421.33
static std::string FmtNum(double v, int dec, bool vi) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%.*f", dec, std::fabs(v));
    std::string s = buf, ip = s, fp;
    size_t dot = s.find('.');
    if (dot != std::string::npos) {
        ip = s.substr(0, dot);
        fp = s.substr(dot + 1);
    }
    std::string out;
    int cnt = 0;
    for (int i = (int)ip.size() - 1; i >= 0; i--) {
        out.insert(out.begin(), ip[i]);
        if (++cnt % 3 == 0 && i > 0) out.insert(out.begin(), vi ? '.' : ',');
    }
    if (!fp.empty()) {
        out += vi ? ',' : '.';
        out += fp;
    }
    if (v < 0) out.insert(out.begin(), '-');
    return out;
}

// Số chữ số thập phân khi đọc bằng giọng nói.
static int VoiceDecimals(double p) {
    if (p >= 1000) return 0;
    if (p >= 100) return 1;
    if (p >= 1) return 2;
    if (p >= 0.1) return 4;
    return 6;
}

// Số chữ số thập phân trên thẻ.
static int CardDecimals(double p) {
    if (p >= 1) return 2;
    if (p >= 0.1) return 4;
    return 6;
}

static std::string FmtVnd(double v) {
    if (v >= 1e9) return FmtNum(v / 1e9, 2, true) + " tỷ đồng";
    if (v >= 1e6) return FmtNum(v / 1e6, 1, true) + " triệu đồng";
    return FmtNum(v, 0, true) + " đồng";
}

static std::string PctVoice(double pct) {
    if (std::fabs(pct) < 0.005) return "gần như đi ngang";
    return std::string(pct > 0 ? "tăng " : "giảm ") + FmtNum(std::fabs(pct), 2, true) + " phần trăm";
}

static std::string LongSentence(const Coin& c, const CryptoQuote& q, double usdt_vnd) {
    int d = VoiceDecimals(q.last);
    std::string s = std::string(c.name) + " (" + c.ticker + "): " + FmtNum(q.last, d, true) +
                    " USD, " + PctVoice(q.pct) + " trong 24 giờ. Cao nhất " +
                    FmtNum(q.high, d, true) + ", thấp nhất " + FmtNum(q.low, d, true) + " USD";
    if (usdt_vnd > 0) s += ". Khoảng " + FmtVnd(q.last * usdt_vnd);
    s += ".";
    return s;
}

static std::string ShortSentence(const Coin& c, const CryptoQuote& q) {
    return std::string(c.name) + " " + FmtNum(q.last, VoiceDecimals(q.last), true) + " USD, " +
           PctVoice(q.pct) + ".";
}
// END-PURE

static std::string BuildText(const std::vector<CryptoQuote>& qs, double usdt_vnd) {
    std::string out;
    if (qs.size() == 1) {
        out = LongSentence(kCoins[qs[0].coin], qs[0], usdt_vnd);
    } else {
        out = "Giá 24 giờ: ";
        for (const auto& q : qs) out += ShortSentence(kCoins[q.coin], q) + " ";
    }
    out += " Nguồn Binance.";
    return out;
}

static std::string LowerAscii(std::string s) {
    for (auto& c : s) c = std::tolower(static_cast<unsigned char>(c));
    return s;
}

static int FindCoin(const std::string& query) {
    const std::string q = LowerAscii(query);
    if (q.empty()) return -1;
    for (int i = 0; i < kNumCoins; i++) {
        if (LowerAscii(kCoins[i].name) == q || LowerAscii(kCoins[i].ticker) == q) return i;
    }
    for (int i = 0; i < kNumCoins; i++) {
        const std::string n = LowerAscii(kCoins[i].name);
        if (n.find(q) != std::string::npos || q.find(n) != std::string::npos) return i;
    }
    return -1;
}

static std::string CoinNames() {
    std::string s;
    for (const auto& c : kCoins) {
        s += c.name;
        s += "; ";
    }
    return s;
}

// ======================= HTTP =======================

// Trả về HTTP status (hoặc -1 lỗi kết nối, -2 dữ liệu quá lớn).
static int HttpGet(const std::string& url, std::string* body, size_t max_bytes) {
    esp_http_client_config_t cfg = {};
    cfg.url = url.c_str();
    cfg.timeout_ms = 8000;
    cfg.buffer_size = 2048;
    cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return -1;

    int status = -1;
    if (esp_http_client_open(c, 0) == ESP_OK) {
        esp_http_client_fetch_headers(c);
        status = esp_http_client_get_status_code(c);
        std::unique_ptr<char[]> chunk(new char[1024]);
        while (true) {
            int n = esp_http_client_read(c, chunk.get(), 1024);
            if (n <= 0) break;
            if (body->size() + n > max_bytes) {
                status = -2;
                break;
            }
            body->append(chunk.get(), n);
        }
    }
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    return status;
}

static double JsonNum(const cJSON* obj, const char* key) {
    const cJSON* v = cJSON_GetObjectItem(const_cast<cJSON*>(obj), key);
    if (cJSON_IsString(v) && v->valuestring) return atof(v->valuestring);
    if (cJSON_IsNumber(v)) return v->valuedouble;
    return 0;
}

// ======================= LỚP CHÍNH =======================

MarketCrypto::MarketCrypto() {
    reply_sem_ = xSemaphoreCreateBinary();
}

MarketCrypto& MarketCrypto::GetInstance() {
    static MarketCrypto inst;
    return inst;
}

void MarketCrypto::RegisterMcpTools() {
    auto& mcp = McpServer::GetInstance();

    mcp.AddTool(
        "self.market.crypto",
        "Xem giá Bitcoin hoặc một đồng tiền điện tử, hiện thẻ giá lên màn hình và tự làm mới. "
        "Các coin hiện có: " + CoinNames() +
        "Tham số coin là tên hoặc mã coin. Chỉ đọc số liệu cho người dùng, "
        "không đưa lời khuyên mua bán hay dự đoán giá.",
        PropertyList({Property("coin", kPropertyTypeString)}),
        [this](const PropertyList& properties) -> ReturnValue {
            return Show(properties["coin"].value<std::string>(), false);
        });

    mcp.AddTool("self.market.crypto_list",
                "Xem giá cả danh sách coin theo dõi (" + CoinNames() +
                    ") cùng lúc, hiện bảng giá lên màn hình. Chỉ đọc số liệu, "
                    "không đưa lời khuyên mua bán hay dự đoán giá.",
                PropertyList(), [this](const PropertyList&) -> ReturnValue {
                    return Show("", true);
                });

    mcp.AddTool("self.market.close", "Tắt thẻ giá tiền điện tử trên màn hình.", PropertyList(),
                [this](const PropertyList&) -> ReturnValue {
                    Close();
                    return true;
                });
}

std::string MarketCrypto::Show(const std::string& query, bool all) {
    std::vector<int> coins;
    if (all) {
        for (int i = 0; i < kNumCoins; i++) coins.push_back(i);
    } else {
        int i = FindCoin(query);
        if (i < 0) return "Không tìm thấy coin này. Các coin có: " + CoinNames();
        coins.push_back(i);
    }

    // Cả hai tính năng cùng vẽ lên lớp trên cùng, nên tắt camera trước (bỏ dòng này nếu không cần).
    TrafficCam::GetInstance().Close();

    xSemaphoreTake(reply_sem_, 0);  // xóa tín hiệu cũ
    {
        std::lock_guard<std::mutex> lk(mu_);
        coins_ = coins;
        deadline_us_ = esp_timer_get_time() + int64_t(kAutoCloseMinutes) * 60 * 1000000;
        refresh_now_ = true;
        reply_pending_ = true;
        stop_ = false;
        if (!running_) {
            running_ = true;
            if (xTaskCreate(&TaskEntry, "market_crypto", 12 * 1024, this, 3, nullptr) != pdPASS) {
                running_ = false;
                reply_pending_ = false;
                return "Không tạo được tác vụ lấy giá (thiếu RAM).";
            }
        }
    }

    // Chờ task lấy giá xong để trả lời ngay cho AI đọc.
    if (xSemaphoreTake(reply_sem_, pdMS_TO_TICKS(12000)) == pdTRUE) {
        std::lock_guard<std::mutex> lk(mu_);
        return result_text_;
    }
    return "Chưa lấy được giá, bạn thử lại sau nhé.";
}

void MarketCrypto::Close() {
    stop_ = true;
}

void MarketCrypto::TaskEntry(void* arg) {
    static_cast<MarketCrypto*>(arg)->Run();
    vTaskDelete(nullptr);
}

void MarketCrypto::Run() {
    bool again;
    do {
        Loop();
        HideOverlay();
        std::lock_guard<std::mutex> lk(mu_);
        // Nếu có yêu cầu mới đến đúng lúc task đang thoát thì chạy tiếp.
        again = refresh_now_ && !stop_;
        if (!again) {
            if (reply_pending_) {  // không để Show() chờ vô ích
                result_text_ = "Đã tắt thẻ giá.";
                reply_pending_ = false;
                xSemaphoreGive(reply_sem_);
            }
            running_ = false;
        }
    } while (again);
}

void MarketCrypto::Loop() {
    while (!stop_) {
        std::vector<int> coins;
        int64_t deadline;
        {
            std::lock_guard<std::mutex> lk(mu_);
            coins = coins_;
            deadline = deadline_us_;
        }
        if (esp_timer_get_time() > deadline) break;
        refresh_now_ = false;

        std::vector<CryptoQuote> quotes;
        std::string err, text;
        bool ok = FetchQuotes(coins, &quotes, &err);
        if (ok) {
            text = BuildText(quotes, usdt_vnd_);
            next_refresh_us_ = esp_timer_get_time() + int64_t(kRefreshMs) * 1000;
            DrawCard(quotes, SparkFor(quotes));
        } else {
            ESP_LOGW(TAG, "Lấy giá lỗi: %s", err.c_str());
            text = "Không lấy được giá lúc này (" + err + ").";
        }

        {
            std::lock_guard<std::mutex> lk(mu_);
            if (reply_pending_) {
                result_text_ = text;
                reply_pending_ = false;
                xSemaphoreGive(reply_sem_);
            }
        }

        // Tỷ giá VND lấy sau khi đã trả lời, để AI nói sớm hơn.
        if (ok) RefreshVnd();
        // Biểu đồ 24 giờ (chỉ khi xem 1 coin), vẽ bổ sung sau khi đã trả lời.
#if LV_USE_CHART    // Nếu LVGL không bật chart thì bỏ qua, không vẽ biểu đồ. Cách bật xem trong menuconfig: Component config → LVGL configuration → Widget Usage → Chart
        if (ok && quotes.size() == 1) RefreshSpark(quotes);
#endif

        for (int i = 0; i < kRefreshMs / 200 && !stop_ && !refresh_now_; i++) {
            vTaskDelay(pdMS_TO_TICKS(200));
        }
    }
}

bool MarketCrypto::FetchQuotes(const std::vector<int>& coins, std::vector<CryptoQuote>* out,
                               std::string* err) {
    // symbols=["BTCUSDT","ETHUSDT"] đã mã hóa URL
    std::string url = "https://api.binance.com/api/v3/ticker/24hr?symbols=%5B";
    for (size_t i = 0; i < coins.size(); i++) {
        if (i) url += "%2C";
        url += "%22";
        url += kCoins[coins[i]].symbol;
        url += "%22";
    }
    url += "%5D";

    std::string body;
    int st = HttpGet(url, &body, 16 * 1024);
    if (st != 200) {
        *err = st == -1 ? "không kết nối được" : (st == -2 ? "dữ liệu quá lớn" : "HTTP " + std::to_string(st));
        return false;
    }

    cJSON* root = cJSON_Parse(body.c_str());
    if (!root || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        *err = "dữ liệu không hợp lệ";
        return false;
    }

    out->clear();
    for (int idx : coins) {
        cJSON* it = nullptr;
        cJSON_ArrayForEach(it, root) {
            cJSON* sym = cJSON_GetObjectItem(it, "symbol");
            if (cJSON_IsString(sym) && sym->valuestring &&
                strcmp(sym->valuestring, kCoins[idx].symbol) == 0) {
                CryptoQuote q;
                q.coin = idx;
                q.last = JsonNum(it, "lastPrice");
                q.pct = JsonNum(it, "priceChangePercent");
                q.high = JsonNum(it, "highPrice");
                q.low = JsonNum(it, "lowPrice");
                out->push_back(q);
                break;
            }
        }
    }
    cJSON_Delete(root);

    if (out->empty()) {
        *err = "không có dữ liệu";
        return false;
    }
    return true;
}

void MarketCrypto::RefreshVnd() {
    if (!kShowVnd) return;
    int64_t now = esp_timer_get_time();
    if (vnd_time_us_ != 0 && now - vnd_time_us_ < kVndCacheUs) return;
    vnd_time_us_ = now;  // đặt trước để lỗi cũng không bị gọi dồn

    std::string body;
    int st = HttpGet("https://api.coingecko.com/api/v3/simple/price?ids=tether&vs_currencies=vnd",
                     &body, 2048);
    if (st != 200) {
        ESP_LOGW(TAG, "Không lấy được tỷ giá VND (HTTP %d)", st);
        return;
    }
    cJSON* root = cJSON_Parse(body.c_str());
    if (root) {
        cJSON* tether = cJSON_GetObjectItem(root, "tether");
        double v = tether ? JsonNum(tether, "vnd") : 0;
        if (v > 1000) usdt_vnd_ = v;
        cJSON_Delete(root);
    }
}

// ======================= BIỂU ĐỒ 24 GIỜ =======================

bool MarketCrypto::FetchSpark(int coin, std::vector<float>* out) {
    std::string url = std::string("https://api.binance.com/api/v3/klines?symbol=") +
                      kCoins[coin].symbol + "&interval=1h&limit=24";
    std::string body;
    if (HttpGet(url, &body, 16 * 1024) != 200) return false;

    cJSON* root = cJSON_Parse(body.c_str());
    if (!root || !cJSON_IsArray(root)) {
        cJSON_Delete(root);
        return false;
    }
    out->clear();
    cJSON* it = nullptr;
    cJSON_ArrayForEach(it, root) {
        cJSON* close = cJSON_GetArrayItem(it, 4);  // [openTime, open, high, low, close, ...]
        if (cJSON_IsString(close) && close->valuestring) {
            out->push_back(static_cast<float>(atof(close->valuestring)));
        }
    }
    cJSON_Delete(root);
    return out->size() >= 2;
}

const std::vector<float>* MarketCrypto::SparkFor(const std::vector<CryptoQuote>& qs) const {
    if (qs.size() == 1 && spark_coin_ == qs[0].coin && !spark_.empty()) return &spark_;
    return nullptr;
}

void MarketCrypto::RefreshSpark(const std::vector<CryptoQuote>& qs) {
    int coin = qs[0].coin;
    int64_t now = esp_timer_get_time();
    if (spark_coin_ == coin && !spark_.empty() && now - spark_time_us_ < kSparkCacheUs) return;

    std::vector<float> s;
    if (!FetchSpark(coin, &s) || stop_) return;
    spark_ = s;
    spark_coin_ = coin;
    spark_time_us_ = now;
    DrawCard(qs, &spark_);
}

// ======================= LVGL =======================

static lv_obj_t* MakeLabel(lv_obj_t* parent, const std::string& text, const lv_font_t* font,
                           uint32_t color) {
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, text.c_str());
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}

static lv_obj_t* MakeBox(lv_obj_t* parent, int w, int h, uint32_t color, int radius,
                         lv_opa_t opa = LV_OPA_COVER) {
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_radius(o, radius, 0);
    MC_NO_SCROLL(o);
    return o;
}

static uint32_t PctColor(double pct) {
    if (pct >= 0.005) return kColUp;
    if (pct <= -0.005) return kColDown;
    return kColFlat;
}

static std::string PctAbs(double pct) {
    char b[32];
    snprintf(b, sizeof(b), "%.2f%%", std::fabs(pct));
    return b;
}

static std::string PctSigned(double pct) {
    char b[32];
    snprintf(b, sizeof(b), "%+.2f%%", pct);
    return b;
}

// Nhãn phần trăm dạng "viên thuốc": nền cùng màu nhưng mờ.
static lv_obj_t* MakePill(lv_obj_t* parent, int w, int h, const std::string& text,
                          const lv_font_t* font, uint32_t color) {
    lv_obj_t* p = MakeBox(parent, w, h, color, h / 2, LV_OPA_30);
    lv_obj_t* l = MakeLabel(p, text, font, color);
    lv_obj_center(l);
    return p;
}

// Chữ trên huy hiệu: đen nếu nền sáng, trắng nếu nền tối.
static uint32_t TextOn(uint32_t c) {
    int r = (c >> 16) & 255, g = (c >> 8) & 255, b = c & 255;
    return (r * 299 + g * 587 + b * 114) / 1000 > 150 ? 0x0B1020 : 0xFFFFFF;
}

static lv_obj_t* MakeBadge(lv_obj_t* parent, const Coin& c, int size) {
    lv_obj_t* b = MakeBox(parent, size, size, c.color, size / 2);
    lv_obj_t* l = MakeLabel(b, c.ticker, FONT_SMALL, TextOn(c.color));
    lv_obj_center(l);
    return b;
}

#if LV_USE_CHART
static void SparkChart(lv_obj_t* root, const std::vector<float>& v, uint32_t color, int x, int y,
                       int w, int h) {
    float mn = v[0], mx = v[0];
    for (float f : v) {
        mn = std::min(mn, f);
        mx = std::max(mx, f);
    }
    if (mx - mn < 1e-9f) mx = mn + 1e-9f;

    lv_obj_t* chart = lv_chart_create(root);
    lv_obj_set_size(chart, w, h);
    lv_obj_align(chart, LV_ALIGN_TOP_LEFT, x, y);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, v.size());
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, 0, 1000);
    lv_chart_set_div_line_count(chart, 0, 0);
    lv_obj_set_style_bg_opa(chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(chart, 0, 0);
    lv_obj_set_style_pad_all(chart, 6, 0);
    lv_obj_set_style_line_width(chart, 3, LV_PART_ITEMS);
    lv_obj_set_style_line_rounded(chart, true, LV_PART_ITEMS);
#if LVGL_VERSION_MAJOR >= 9
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);  // ẩn chấm tròn trên đường
#else
    lv_obj_set_style_size(chart, 0, LV_PART_INDICATOR);
#endif

    lv_chart_series_t* ser = lv_chart_add_series(chart, lv_color_hex(color), LV_CHART_AXIS_PRIMARY_Y);
    for (float f : v) {
        lv_chart_set_next_value(chart, ser, (int32_t)((f - mn) / (mx - mn) * 1000.0f));
    }
    lv_chart_refresh(chart);
}
#endif

static void BuildSingle(lv_obj_t* root, const CryptoQuote& q, const std::vector<float>* spark) {
    const Coin& c = kCoins[q.coin];
    const uint32_t col = PctColor(q.pct);
    const int d = CardDecimals(q.last);

    // Hàng đầu: huy hiệu + tên + nhãn phần trăm
    lv_obj_t* badge = MakeBadge(root, c, 46);
    lv_obj_align(badge, LV_ALIGN_TOP_LEFT, 14, 10);

    lv_obj_t* l = MakeLabel(root, c.name, FONT_MED, kColWhite);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 70, 11);
    l = MakeLabel(root, std::string(c.ticker) + " / USDT", FONT_SMALL, kColGray);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 70, 36);

    std::string pct = PctAbs(q.pct);
    if (kUseArrowSymbols && std::fabs(q.pct) >= 0.005) {
        pct = std::string(q.pct > 0 ? LV_SYMBOL_UP : LV_SYMBOL_DOWN) + " " + pct;
    }
    lv_obj_t* pill = MakePill(root, 108, 30, pct, FONT_MED, col);
    lv_obj_align(pill, LV_ALIGN_TOP_RIGHT, -14, 17);

    // Giá lớn
    l = MakeLabel(root, "$" + FmtNum(q.last, d, false), FONT_HUGE, kColWhite);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 14, 62);

    // Biểu đồ 24 giờ
#if LV_USE_CHART
    if (spark && spark->size() >= 2) {
        SparkChart(root, *spark, col, 8, 112, 304, 80);
    } else {
        l = MakeLabel(root, "Loading chart...", FONT_SMALL, kColGray);
        lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 144);
    }
#else
    (void)spark;
#endif

    // Thấp / giữa / cao
    l = MakeLabel(root, "L " + FmtNum(q.low, d, false), FONT_SMALL, kColGray);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 14, 198);
    l = MakeLabel(root, "24h", FONT_SMALL, kAccent);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 198);
    l = MakeLabel(root, "H " + FmtNum(q.high, d, false), FONT_SMALL, kColGray);
    lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -14, 198);
}

static void BuildList(lv_obj_t* root, const std::vector<CryptoQuote>& qs) {
    // Tiêu đề
    lv_obj_t* dot = MakeBox(root, 9, 9, kColUp, 5);
    lv_obj_align(dot, LV_ALIGN_TOP_LEFT, 14, 15);
    lv_obj_t* l = MakeLabel(root, "CRYPTO", FONT_MED, kColWhite);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 30, 8);
    l = MakeLabel(root, "Binance  24h", FONT_SMALL, kColGray);
    lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -14, 11);

    int y = 40;
    for (const auto& q : qs) {
        const Coin& c = kCoins[q.coin];
        const uint32_t col = PctColor(q.pct);

        lv_obj_t* row = MakeBox(root, 304, 29, kRowBg, 10);
        lv_obj_align(row, LV_ALIGN_TOP_LEFT, 8, y);

        lv_obj_t* dotc = MakeBox(row, 10, 10, c.color, 5);
        lv_obj_align(dotc, LV_ALIGN_LEFT_MID, 10, 0);

        l = MakeLabel(row, c.ticker, FONT_SMALL, kColWhite);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 28, 0);

        l = MakeLabel(row, FmtNum(q.last, CardDecimals(q.last), false), FONT_SMALL, kColWhite);
        lv_obj_align(l, LV_ALIGN_RIGHT_MID, -84, 0);

        lv_obj_t* pill = MakePill(row, 72, 21, PctSigned(q.pct), FONT_SMALL, col);
        lv_obj_align(pill, LV_ALIGN_RIGHT_MID, -4, 0);
        y += 33;
    }
}

static void AnimBarCb(void* bar, int32_t v) {
    lv_bar_set_value(static_cast<lv_obj_t*>(bar), v, LV_ANIM_OFF);
}

// Thanh mỏng ở đáy màn hình chạy từ trái sang phải = thời gian tới lần làm mới tiếp theo.
static void AddRefreshBar(lv_obj_t* root, int64_t remaining_ms) {
    if (remaining_ms < 300) remaining_ms = 300;
    if (remaining_ms > kRefreshMs) remaining_ms = kRefreshMs;

    lv_obj_t* bar = lv_bar_create(root);
    lv_obj_remove_style_all(bar);
    lv_obj_set_size(bar, LV_PCT(100), 3);
    lv_obj_align(bar, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(kBarTrack), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_hex(kAccent), LV_PART_INDICATOR);
    lv_bar_set_range(bar, 0, 100);

    int start = (int)((kRefreshMs - remaining_ms) * 100 / kRefreshMs);
    lv_bar_set_value(bar, start, LV_ANIM_OFF);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, bar);
    lv_anim_set_values(&a, start, 100);
    lv_anim_set_time(&a, (uint32_t)remaining_ms);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_exec_cb(&a, AnimBarCb);
    lv_anim_start(&a);
}

void MarketCrypto::DrawCard(const std::vector<CryptoQuote>& qs, const std::vector<float>* spark) {
    auto display = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(display);

    if (!overlay_) {
        overlay_ = lv_obj_create(lv_layer_top());
        lv_obj_remove_style_all(overlay_);
        lv_obj_set_size(overlay_, LV_PCT(100), LV_PCT(100));
        lv_obj_set_style_bg_opa(overlay_, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(overlay_, lv_color_hex(kBgTop), 0);
        lv_obj_set_style_bg_grad_color(overlay_, lv_color_hex(kBgBottom), 0);
        lv_obj_set_style_bg_grad_dir(overlay_, LV_GRAD_DIR_VER, 0);
        MC_NO_SCROLL(overlay_);
    }
    lv_obj_clean(overlay_);

    if (qs.size() == 1) {
        BuildSingle(overlay_, qs[0], spark);
    } else {
        BuildList(overlay_, qs);
    }

    int64_t remaining_ms = (next_refresh_us_ - esp_timer_get_time()) / 1000;
    AddRefreshBar(overlay_, remaining_ms);
    lv_obj_invalidate(overlay_);
}

void MarketCrypto::HideOverlay() {
    auto display = Board::GetInstance().GetDisplay();
    DisplayLockGuard lock(display);
    if (overlay_) {
        lv_obj_del(overlay_);
        overlay_ = nullptr;
    }
}
