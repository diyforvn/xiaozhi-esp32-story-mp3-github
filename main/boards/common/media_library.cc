#include "media_library.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>

#include "board.h"
#include "audio_codec.h"
#include "settings.h"

extern "C" {
#include "mp3dec.h"   // MP3InitDecoder / MP3Decode / MP3GetLastFrameInfo
}

#define TAG "MediaLibrary"

MediaBuffer::~MediaBuffer() {
    if (data) {
        heap_caps_free(data);
    }
}

MediaLibrary& MediaLibrary::GetInstance() {
    static MediaLibrary instance;
    return instance;
}

MediaLibrary::MediaLibrary() {
    
    Settings settings("story", false);
    base_url_ = settings.GetString("base_url",
        "https://raw.githubusercontent.com/coinmvtruong01-lab/story-library/main/");
}

MediaLibrary::~MediaLibrary() {
    Stop();
}

void MediaLibrary::SetBaseUrl(const std::string& url) {
    base_url_ = url;
    if (!base_url_.empty() && base_url_.back() != '/') {
        base_url_ += "/";
    }
    Settings settings("story", true);
    settings.SetString("base_url", base_url_);
}

// ============================================================================
// Index.json
// ============================================================================

void MediaLibrary::EnsureIndexLoaded() {
    bool empty;
    {
        std::lock_guard<std::mutex> lock(stories_mutex_);
        empty = stories_.empty();
    }
    if (empty) {
        RefreshIndex();
    }
}

bool MediaLibrary::RefreshIndex() {
    std::string json_text;
    if (!DownloadIndexJson(json_text)) {
        return false;
    }
    return ParseIndexJson(json_text);
}

bool MediaLibrary::DownloadIndexJson(std::string& out_json) {
    auto http = Board::GetInstance().GetNetwork()->CreateHttp(3);
    std::string url = base_url_ + index_path_;

    if (!http->Open("GET", url)) {
        ESP_LOGE(TAG, "Khong mo duoc %s", url.c_str());
        return false;
    }
    if (http->GetStatusCode() != 200) {
        ESP_LOGE(TAG, "index.json tra ve status %d", http->GetStatusCode());
        http->Close();
        return false;
    }
    out_json = http->ReadAll();
    http->Close();
    return !out_json.empty();
}

bool MediaLibrary::ParseIndexJson(const std::string& json_text) {
    cJSON* root = cJSON_Parse(json_text.c_str());
    if (root == nullptr || !cJSON_IsArray(root)) {
        ESP_LOGE(TAG, "index.json khong hop le");
        if (root) cJSON_Delete(root);
        return false;
    }

    std::vector<StoryEntry> parsed;
    cJSON* item;
    cJSON_ArrayForEach(item, root) {
        StoryEntry entry;

        auto id = cJSON_GetObjectItem(item, "id");
        auto name = cJSON_GetObjectItem(item, "name");
        if (!name) name = cJSON_GetObjectItem(item, "title");
        auto path = cJSON_GetObjectItem(item, "path");
        auto category = cJSON_GetObjectItem(item, "category");
        auto age = cJSON_GetObjectItem(item, "age");
        auto duration = cJSON_GetObjectItem(item, "duration");
        auto tags = cJSON_GetObjectItem(item, "tags");

        if (!cJSON_IsString(id) || !cJSON_IsString(path)) {
            continue;
        }

        entry.id = id->valuestring;
        entry.path = path->valuestring;
        entry.name = cJSON_IsString(name) ? name->valuestring : entry.id;
        entry.category = cJSON_IsString(category) ? category->valuestring : "";
        entry.age = cJSON_IsString(age) ? age->valuestring : "";
        entry.duration_sec = cJSON_IsNumber(duration) ? duration->valueint : 0;

        if (cJSON_IsArray(tags)) {
            cJSON* tag;
            cJSON_ArrayForEach(tag, tags) {
                if (cJSON_IsString(tag)) {
                    entry.tags.push_back(tag->valuestring);
                }
            }
        }
        parsed.push_back(std::move(entry));
    }
    cJSON_Delete(root);

    if (parsed.empty()) {
        ESP_LOGW(TAG, "index.json khong co entry nao hop le");
        return false;
    }

    std::lock_guard<std::mutex> lock(stories_mutex_);
    stories_ = std::move(parsed);
    ESP_LOGI(TAG, "Da nap %d entry tu index.json", (int)stories_.size());
    return true;
}

std::string MediaLibrary::BuildUrl(const StoryEntry& entry) const {
    return base_url_ + entry.path;
}

std::vector<StoryEntry> MediaLibrary::DebugListAll(std::string* out_base_url) const {
    std::lock_guard<std::mutex> lock(stories_mutex_);
    if (out_base_url) *out_base_url = base_url_;
    return stories_;
}

// ============================================================================
// Tim kiem (giu nguyen thuat toan chuan hoa tieng Viet + fuzzy match )
// ============================================================================

namespace {

struct DiacriticMap { const char* utf8; char ascii; };
static const DiacriticMap kViDiacriticMap[] = {
    {"á",'a'},{"à",'a'},{"ả",'a'},{"ã",'a'},{"ạ",'a'},
    {"ă",'a'},{"ắ",'a'},{"ằ",'a'},{"ẳ",'a'},{"ẵ",'a'},{"ặ",'a'},
    {"â",'a'},{"ấ",'a'},{"ầ",'a'},{"ẩ",'a'},{"ẫ",'a'},{"ậ",'a'},
    {"Á",'a'},{"À",'a'},{"Ả",'a'},{"Ã",'a'},{"Ạ",'a'},
    {"Ă",'a'},{"Ắ",'a'},{"Ằ",'a'},{"Ẳ",'a'},{"Ẵ",'a'},{"Ặ",'a'},
    {"Â",'a'},{"Ấ",'a'},{"Ầ",'a'},{"Ẩ",'a'},{"Ẫ",'a'},{"Ậ",'a'},
    {"é",'e'},{"è",'e'},{"ẻ",'e'},{"ẽ",'e'},{"ẹ",'e'},
    {"ê",'e'},{"ế",'e'},{"ề",'e'},{"ể",'e'},{"ễ",'e'},{"ệ",'e'},
    {"É",'e'},{"È",'e'},{"Ẻ",'e'},{"Ẽ",'e'},{"Ẹ",'e'},
    {"Ê",'e'},{"Ế",'e'},{"Ề",'e'},{"Ể",'e'},{"Ễ",'e'},{"Ệ",'e'},
    {"í",'i'},{"ì",'i'},{"ỉ",'i'},{"ĩ",'i'},{"ị",'i'},
    {"Í",'i'},{"Ì",'i'},{"Ỉ",'i'},{"Ĩ",'i'},{"Ị",'i'},
    {"ó",'o'},{"ò",'o'},{"ỏ",'o'},{"õ",'o'},{"ọ",'o'},
    {"ô",'o'},{"ố",'o'},{"ồ",'o'},{"ổ",'o'},{"ỗ",'o'},{"ộ",'o'},
    {"ơ",'o'},{"ớ",'o'},{"ờ",'o'},{"ở",'o'},{"ỡ",'o'},{"ợ",'o'},
    {"Ó",'o'},{"Ò",'o'},{"Ỏ",'o'},{"Õ",'o'},{"Ọ",'o'},
    {"Ô",'o'},{"Ố",'o'},{"Ồ",'o'},{"Ổ",'o'},{"Ỗ",'o'},{"Ộ",'o'},
    {"Ơ",'o'},{"Ớ",'o'},{"Ờ",'o'},{"Ở",'o'},{"Ỡ",'o'},{"Ợ",'o'},
    {"ú",'u'},{"ù",'u'},{"ủ",'u'},{"ũ",'u'},{"ụ",'u'},
    {"ư",'u'},{"ứ",'u'},{"ừ",'u'},{"ử",'u'},{"ữ",'u'},{"ự",'u'},
    {"Ú",'u'},{"Ù",'u'},{"Ủ",'u'},{"Ũ",'u'},{"Ụ",'u'},
    {"Ư",'u'},{"Ứ",'u'},{"Ừ",'u'},{"Ử",'u'},{"Ữ",'u'},{"Ự",'u'},
    {"ý",'y'},{"ỳ",'y'},{"ỷ",'y'},{"ỹ",'y'},{"ỵ",'y'},
    {"Ý",'y'},{"Ỳ",'y'},{"Ỷ",'y'},{"Ỹ",'y'},{"Ỵ",'y'},
    {"đ",'d'},{"Đ",'d'},
};

std::string NormalizeVi(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    size_t i = 0;
    while (i < s.size()) {
        unsigned char c = (unsigned char)s[i];
        bool matched = false;
        if (c >= 0xC0) {
            size_t seq_len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
            if (i + seq_len <= s.size()) {
                std::string candidate = s.substr(i, seq_len);
                for (auto& m : kViDiacriticMap) {
                    if (candidate == m.utf8) {
                        result.push_back(m.ascii);
                        i += seq_len;
                        matched = true;
                        break;
                    }
                }
            }
        }
        if (!matched) {
            result.push_back((char)std::tolower(c));
            i++;
        }
    }
    return result;
}

int EditDistance(const std::string& a, const std::string& b) {
    size_t n = a.size(), m = b.size();
    if (n == 0) return (int)m;
    if (m == 0) return (int)n;
    std::vector<int> prev(m + 1), cur(m + 1);
    for (size_t j = 0; j <= m; j++) prev[j] = (int)j;
    for (size_t i = 1; i <= n; i++) {
        cur[0] = (int)i;
        for (size_t j = 1; j <= m; j++) {
            int cost = (a[i - 1] == b[j - 1]) ? 0 : 1;
            cur[j] = std::min({prev[j] + 1, cur[j - 1] + 1, prev[j - 1] + cost});
        }
        std::swap(prev, cur);
    }
    return prev[m];
}

std::vector<std::string> SplitWords(const std::string& s) {
    std::vector<std::string> words;
    std::string cur;
    for (char c : s) {
        if (c == ' ') {
            if (!cur.empty()) { words.push_back(cur); cur.clear(); }
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) words.push_back(cur);
    return words;
}

int WordOverlapScore(const std::string& q, const std::string& name_norm) {
    auto q_words = SplitWords(q);
    auto n_words = SplitWords(name_norm);
    int score = 0;
    for (auto& qw : q_words) {
        int best_d = 1000;
        for (auto& nw : n_words) {
            int d = EditDistance(qw, nw);
            int max_len = (int)std::max(qw.size(), nw.size());
            int allowed = std::max(1, max_len / 3);
            if (d <= allowed && d < best_d) best_d = d;
        }
        if (best_d <= std::max(1, (int)qw.size() / 3)) {
            score += (best_d == 0) ? 3 : 2;
        }
    }
    return score;
}

}  // namespace

const StoryEntry* MediaLibrary::FindById(const std::string& id) const {
    std::lock_guard<std::mutex> lock(stories_mutex_);
    for (auto& e : stories_) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

const StoryEntry* MediaLibrary::FindBestMatch(const std::string& query,
                                               const std::string& age,
                                               const std::function<bool(const StoryEntry&)>& filter) const {
    std::lock_guard<std::mutex> lock(stories_mutex_);
    std::string q = NormalizeVi(query);

    const StoryEntry* best = nullptr;
    int best_score = 0;

    for (auto& e : stories_) {
        if (filter && !filter(e)) continue;
        if (!age.empty() && !e.age.empty() && e.age != age) continue;

        int score = 0;
        std::string name_norm = NormalizeVi(e.name);

        if (q.find(name_norm) != std::string::npos || name_norm.find(q) != std::string::npos) {
            score += 6;
        }
        score += WordOverlapScore(q, name_norm);

        int dist = EditDistance(q, name_norm);
        int max_len = (int)std::max(q.size(), name_norm.size());
        if (max_len > 0) {
            double ratio = 1.0 - (double)dist / (double)max_len;
            if (ratio > 0.4) {
                score += (int)(ratio * 4);
            }
        }

        for (auto& tag : e.tags) {
            if (q.find(NormalizeVi(tag)) != std::string::npos) {
                score += 2;
            }
        }
        if (score > best_score) {
            best_score = score;
            best = &e;
        }
    }
    if (best_score < 3) {
        return nullptr;
    }
    return best;
}

// ============================================================================
// Loc rac ID3v2 dau file
// ============================================================================

namespace {

size_t SkipId3v2Tag(const uint8_t* buf, size_t size) {
    if (size < 10 || buf[0] != 'I' || buf[1] != 'D' || buf[2] != '3') {
        return 0;
    }
    uint32_t tag_size = ((buf[6] & 0x7F) << 21) | ((buf[7] & 0x7F) << 14) |
                         ((buf[8] & 0x7F) << 7)  |  (buf[9] & 0x7F);
    size_t total = 10 + tag_size;
    if (buf[5] & 0x10) total += 10;
    return total;   // LUU Y: khong clamp o day - clamp o noi goi (con phai
                     // biet gia tri "that" de biet co nen cho tai them hay
                     // khong, xem DecoderTask)
}

bool IsPlausibleMp3Header(const uint8_t* p, int bytes_left) {
    if (bytes_left < 4) return false;
    if (p[0] != 0xFF || (p[1] & 0xE0) != 0xE0) return false;
    uint8_t version      = (p[1] >> 3) & 0x03;
    uint8_t layer        = (p[1] >> 1) & 0x03;
    uint8_t bitrate_idx  = (p[2] >> 4) & 0x0F;
    uint8_t samplerate_i = (p[2] >> 2) & 0x03;
    if (version == 0x01 || layer == 0x00) return false;
    if (bitrate_idx == 0x0F || bitrate_idx == 0x00) return false;
    if (samplerate_i == 0x03) return false;
    return true;
}

}  // namespace

// ============================================================================
// Dieu khien phat / dung / tam dung
// ============================================================================

std::string MediaLibrary::current_name() const {
    std::lock_guard<std::mutex> lock(name_mutex_);
    return current_name_;
}

void MediaLibrary::Pause() {
    if (state_.load() == MediaPlayerState::kPlaying) {
        state_.store(MediaPlayerState::kPaused);
    }
}

void MediaLibrary::Resume() {
    if (state_.load() == MediaPlayerState::kPaused) {
        state_.store(MediaPlayerState::kPlaying);
    }
    auto_paused_by_narration_.store(false);
}

void MediaLibrary::DuckForSpeech() {
  
    speech_active_.store(true);

    if (state_.load() == MediaPlayerState::kPlaying) {
        state_.store(MediaPlayerState::kPaused);
        auto_paused_by_narration_.store(true);
    }
}

void MediaLibrary::UnduckAfterSpeech() {
    speech_active_.store(false);

  
    bool expected = true;
    if (auto_paused_by_narration_.compare_exchange_strong(expected, false)) {
        if (state_.load() == MediaPlayerState::kPaused) {
            state_.store(MediaPlayerState::kPlaying);
        }
    }
}

void MediaLibrary::Stop() {
    StopAndWait();
}

void MediaLibrary::StopAndWait() {
    std::lock_guard<std::mutex> lock(playback_mutex_);
    if (state_.load() == MediaPlayerState::kIdle || state_.load() == MediaPlayerState::kStopped) {
        return;
    }
    stop_requested_.store(true);

    while (downloader_handle_ != nullptr || decoder_handle_ != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    stop_requested_.store(false);
    state_.store(MediaPlayerState::kStopped);
    {
        std::lock_guard<std::mutex> nlock(name_mutex_);
        current_name_.clear();
    }
    {
       
        std::lock_guard<std::mutex> clock(cache_mutex_);
        current_buffer_.reset();
    }
}

MediaBufferPtr MediaLibrary::GetOrCreateBuffer_Locked(const std::string& id,
                                                       const std::string& display_name,
                                                       bool& need_download) {
 
    for (auto it2 = cache_by_id_.begin(); it2 != cache_by_id_.end(); ) {
        if (!it2->second->download_done.load() || it2->second->download_error.load()) {
            ESP_LOGI(TAG, "Don buffer rac (chua tai xong/loi) cua '%s'",
                     it2->second->display_name.c_str());
            it2 = cache_by_id_.erase(it2);
        } else {
            ++it2;
        }
    }

    auto it = cache_by_id_.find(id);
    if (it != cache_by_id_.end()) {
        // Sau vong don rac o tren, moi entry con lai trong cache chac chan
        // download_done=true && !download_error - an toan dung lai luon.
        it->second->last_used_tick = esp_timer_get_time();
        need_download = false;
        ESP_LOGI(TAG, "Cache hit cho '%s', khong can tai lai", display_name.c_str());
        return it->second;
    }


    EnsureFreePsramForNewBuffer_Locked(id, kPerTrackCapacity);

    auto buf = std::make_shared<MediaBuffer>();
    buf->id = id;
    buf->display_name = display_name;
    buf->capacity = kPerTrackCapacity;
    buf->data = (uint8_t*)heap_caps_malloc(kPerTrackCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf->data) {
        ESP_LOGE(TAG, "Het PSRAM khi cap phat buffer cho '%s' (%u byte, con trong %u byte)",
                 display_name.c_str(), (unsigned)kPerTrackCapacity,
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
        buf->download_error.store(true);
        need_download = false;
        return buf;   // khong dua vao cache, tra ve buffer loi de Play() bao that bai
    }
    buf->last_used_tick = esp_timer_get_time();
    cache_by_id_[id] = buf;
    need_download = true;
    
    EvictIfNeeded_Locked(id);
    return buf;
}

void MediaLibrary::EnsureFreePsramForNewBuffer_Locked(const std::string& keep_id, size_t needed_bytes) {
  
    const size_t kSafetyMargin = 512 * 1024;
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    while (free_psram < needed_bytes + kSafetyMargin) {
        std::string oldest_id;
        int64_t oldest_tick = std::numeric_limits<int64_t>::max();
        for (auto& kv : cache_by_id_) {
            if (kv.first == keep_id) continue;
            if (current_buffer_ && kv.second == current_buffer_) continue;
            if (kv.second->last_used_tick < oldest_tick) {
                oldest_tick = kv.second->last_used_tick;
                oldest_id = kv.first;
            }
        }
        if (oldest_id.empty()) {
            ESP_LOGW(TAG, "Khong con cache nao de giai phong them PSRAM "
                     "(con trong %u byte, can %u byte)",
                     (unsigned)free_psram, (unsigned)(needed_bytes + kSafetyMargin));
            break;
        }
        ESP_LOGI(TAG, "Giai phong cache '%s' de lay cho PSRAM cho bai moi", oldest_id.c_str());
        cache_by_id_.erase(oldest_id);
        free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    }
}

void MediaLibrary::EvictIfNeeded_Locked(const std::string& keep_id) {
    auto is_protected = [&](const MediaBufferPtr& b, const std::string& bid) {
        if (bid == keep_id) return true;
        if (current_buffer_ && b == current_buffer_) return true;
        return false;
    };

    size_t total = 0;
    for (auto& kv : cache_by_id_) {
        if (is_protected(kv.second, kv.first)) continue;
        if (kv.second->download_done.load()) total += kv.second->capacity;
    }

    while (total > kCacheBudgetBytes) {
        std::string oldest_id;
        int64_t oldest_tick = std::numeric_limits<int64_t>::max();
        for (auto& kv : cache_by_id_) {
            if (is_protected(kv.second, kv.first)) continue;
            if (!kv.second->download_done.load()) continue;
            if (kv.second->last_used_tick < oldest_tick) {
                oldest_tick = kv.second->last_used_tick;
                oldest_id = kv.first;
            }
        }
        if (oldest_id.empty()) break;
        ESP_LOGI(TAG, "Cache day, xoa buffer LRU: %s", oldest_id.c_str());
        total -= cache_by_id_[oldest_id]->capacity;
        cache_by_id_.erase(oldest_id);
    }
}

bool MediaLibrary::Play(const StoryEntry& entry) {
   
    StopAndWait();

    std::string url = BuildUrl(entry);
    std::string id = entry.id;
    std::string display_name = entry.name;

    bool need_download = true;
    MediaBufferPtr buf;
    {
        std::lock_guard<std::mutex> lock(cache_mutex_);
        buf = GetOrCreateBuffer_Locked(id, display_name, need_download);
    }
    if (buf->download_error.load()) {
        return false;   // het PSRAM, da log o GetOrCreateBuffer_Locked
    }

    std::lock_guard<std::mutex> lock(playback_mutex_);
    {
        std::lock_guard<std::mutex> clock(cache_mutex_);
        current_buffer_ = buf;
    }
    {
        std::lock_guard<std::mutex> nlock(name_mutex_);
        current_name_ = display_name;
    }
    state_.store(MediaPlayerState::kBuffering);
    stop_requested_.store(false);
    
    if (is_active_session_checker_ && is_active_session_checker_()) {
        DuckForSpeech();
    }

    if (need_download) {
        struct DownloadArgs { MediaLibrary* self; MediaBufferPtr buf; std::string url; };
        auto* args = new DownloadArgs{this, buf, url};
        // Stack 8192: chi lam viec voi http client (mbedTLS handshake ~6-8KB),
        // khong con vong lap decode nen khong can stack lon nhu decoder-task.
        BaseType_t ok = xTaskCreatePinnedToCore(
            [](void* arg) {
                auto* a = static_cast<DownloadArgs*>(arg);
                a->self->DownloaderTask(a->buf, a->url);
                a->self->downloader_handle_ = nullptr;
                delete a;
                vTaskDelete(nullptr);
            }, "media_dl", 8192, args, 5, &downloader_handle_, 0);
        if (ok != pdPASS) {
            ESP_LOGE(TAG, "Khong tao duoc downloader task");
            delete args;
            state_.store(MediaPlayerState::kError);
            return false;
        }
    } else {
        downloader_handle_ = nullptr;   // du lieu da co san trong cache, khong can tai
    }

    struct DecodeArgs { MediaLibrary* self; MediaBufferPtr buf; };
    auto* dargs = new DecodeArgs{this, buf};
    // Stack 16384: giu nguyen muc cu (Helix decode + AdaptPcm).
    BaseType_t ok2 = xTaskCreatePinnedToCore(
        [](void* arg) {
            auto* a = static_cast<DecodeArgs*>(arg);
            a->self->DecoderTask(a->buf);
            a->self->decoder_handle_ = nullptr;
            delete a;
            vTaskDelete(nullptr);
        }, "media_play", 16384, dargs, 6, &decoder_handle_, 1);
    if (ok2 != pdPASS) {
        ESP_LOGE(TAG, "Khong tao duoc decoder task");
        delete dargs;
        state_.store(MediaPlayerState::kError);
       
        return false;
    }
    return true;
}

// ============================================================================
// Downloader-task: chi tai du lieu vao buf->data, KHONG decode
// ============================================================================

void MediaLibrary::DownloaderTask(MediaBufferPtr buf, std::string url) {
    const int kMaxRetries = 3;
    bool ok = false;

    for (int attempt = 1; attempt <= kMaxRetries && !stop_requested_.load(); attempt++) {
        buf->filled.store(0, std::memory_order_relaxed);

        auto http = Board::GetInstance().GetNetwork()->CreateHttp(5);
        if (!http->Open("GET", url)) {
            ESP_LOGW(TAG, "Mo URL that bai (lan %d): %s", attempt, url.c_str());
            vTaskDelay(pdMS_TO_TICKS(300 * attempt));
            continue;
        }
        if (http->GetStatusCode() != 200) {
            ESP_LOGW(TAG, "Status %d cho %s", http->GetStatusCode(), url.c_str());
            http->Close();
            vTaskDelay(pdMS_TO_TICKS(300 * attempt));
            continue;
        }

        bool stream_error = false;
        bool file_done = false;
        while (!stop_requested_.load()) {
            size_t filled = buf->filled.load(std::memory_order_relaxed);
            if (filled + kHttpReadChunkSize > buf->capacity) {
                ESP_LOGE(TAG, "File '%s' vuot qua gioi han %u byte, huy tai",
                         buf->display_name.c_str(), (unsigned)buf->capacity);
                stream_error = true;
                break;
            }
            int n = http->Read((char*)(buf->data + filled), kHttpReadChunkSize);
            if (n < 0) {
                ESP_LOGW(TAG, "Loi doc HTTP giua chung, se thu tai lai tu dau");
                stream_error = true;
                break;
            }
            if (n == 0) {
                file_done = true;
                break;
            }
           
            buf->filled.store(filled + (size_t)n, std::memory_order_release);
        }
        http->Close();

        if (stop_requested_.load()) {
            ok = false;
            break;
        }
        if (file_done && buf->filled.load() > 0) {
            ok = true;
            break;
        }
        if (stream_error) {
            vTaskDelay(pdMS_TO_TICKS(300 * attempt));
            continue;
        }
    }

    if (ok) {
        buf->download_done.store(true, std::memory_order_release);
        ESP_LOGI(TAG, "Da tai xong '%s': %u byte", buf->display_name.c_str(),
                 (unsigned)buf->filled.load());
    } else if (!stop_requested_.load()) {
        buf->download_error.store(true, std::memory_order_release);
        ESP_LOGE(TAG, "Tai that bai: %s", url.c_str());
    }
   
}

// ============================================================================
// Decoder-task: doc tu buf->data[0..filled), giai ma + phat, cho tai them
// khi bat kip toc do tai (neu chua download_done)
// ============================================================================

std::vector<int16_t> MediaLibrary::AdaptPcm(const int16_t* in, int in_frames,
                                             int in_channels, int in_rate,
                                             int out_channels, int out_rate) {
    std::vector<int16_t> ch_adjusted;
    ch_adjusted.reserve((size_t)in_frames * out_channels);

    if (in_channels == out_channels) {
        ch_adjusted.assign(in, in + (size_t)in_frames * in_channels);
    } else if (in_channels == 2 && out_channels == 1) {
        for (int i = 0; i < in_frames; i++) {
            int32_t l = in[i * 2];
            int32_t r = in[i * 2 + 1];
            ch_adjusted.push_back((int16_t)((l + r) / 2));
        }
    } else if (in_channels == 1 && out_channels == 2) {
        for (int i = 0; i < in_frames; i++) {
            ch_adjusted.push_back(in[i]);
            ch_adjusted.push_back(in[i]);
        }
    } else {
        int copy_channels = std::min(in_channels, out_channels);
        for (int i = 0; i < in_frames; i++) {
            for (int c = 0; c < out_channels; c++) {
                int16_t s = (c < copy_channels) ? in[i * in_channels + c] : 0;
                ch_adjusted.push_back(s);
            }
        }
    }

    if (in_rate == out_rate || in_frames <= 1) {
        return ch_adjusted;
    }

    int out_frames = (int)((int64_t)in_frames * out_rate / in_rate);
    std::vector<int16_t> out;
    out.reserve((size_t)out_frames * out_channels);

    double step = (double)in_rate / (double)out_rate;
    double pos = 0.0;
    for (int i = 0; i < out_frames; i++) {
        int idx0 = (int)pos;
        int idx1 = std::min(idx0 + 1, in_frames - 1);
        double frac = pos - idx0;
        for (int c = 0; c < out_channels; c++) {
            int16_t s0 = ch_adjusted[(size_t)idx0 * out_channels + c];
            int16_t s1 = ch_adjusted[(size_t)idx1 * out_channels + c];
            out.push_back((int16_t)(s0 + (s1 - s0) * frac));
        }
        pos += step;
    }
    return out;
}

void MediaLibrary::DecoderTask(MediaBufferPtr buf) {
  
    while (!stop_requested_.load()) {
        if (buf->download_error.load()) {
            state_.store(MediaPlayerState::kError);
            return;
        }
        size_t filled = buf->filled.load(std::memory_order_acquire);
        if (filled >= kPrebufferBytes || buf->download_done.load(std::memory_order_acquire)) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    if (stop_requested_.load()) {
        return;
    }


    if (speech_active_.load()) {
        state_.store(MediaPlayerState::kPaused);
        auto_paused_by_narration_.store(true);
    } else {
        state_.store(MediaPlayerState::kPlaying);
    }

    auto codec = Board::GetInstance().GetAudioCodec();
    HMP3Decoder decoder = MP3InitDecoder();
    if (!decoder) {
        ESP_LOGE(TAG, "Khong khoi tao duoc MP3 decoder");
        state_.store(MediaPlayerState::kError);
        return;
    }
    int16_t* pcm_buf = (int16_t*)heap_caps_malloc(
        1152 * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!pcm_buf) {
        pcm_buf = (int16_t*)heap_caps_malloc(1152 * 2 * sizeof(int16_t), MALLOC_CAP_8BIT);
    }
    if (!pcm_buf) {
        ESP_LOGE(TAG, "Khong cap phat duoc buffer PCM (het bo nho)");
        MP3FreeDecoder(decoder);
        state_.store(MediaPlayerState::kError);
        return;
    }


    size_t pos = 0;
    {
        size_t filled;
        while (true) {
            filled = buf->filled.load(std::memory_order_acquire);
            if (filled >= 10 || buf->download_done.load() || stop_requested_.load()) break;
            vTaskDelay(pdMS_TO_TICKS(20));
        }
        if (!stop_requested_.load() && filled >= 10 && buf->data[0] == 'I' &&
            buf->data[1] == 'D' && buf->data[2] == '3') {
            size_t tag_total = SkipId3v2Tag(buf->data, filled);
            while (tag_total > filled && !stop_requested_.load() && !buf->download_done.load()) {
                vTaskDelay(pdMS_TO_TICKS(20));
                filled = buf->filled.load(std::memory_order_acquire);
            }
            pos = std::min(tag_total, filled);
            if (pos > 0) {
                ESP_LOGI(TAG, "Bo qua ID3v2 tag dau file: %u byte", (unsigned)pos);
            }
        }
    }

    int consecutive_errors = 0;
    const int kMaxConsecutiveErrors = 200;

    while (!stop_requested_.load()) {
        if (state_.load() == MediaPlayerState::kPaused) {
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        size_t filled = buf->filled.load(std::memory_order_acquire);
        bool done = buf->download_done.load(std::memory_order_acquire);

        if (pos >= filled) {
            if (done) break;                              // het du lieu that su
            if (buf->download_error.load()) {
                state_.store(MediaPlayerState::kError);
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(20));                 // cho tai them
            continue;
        }

        int bytes_left = (int)(filled - pos);
        unsigned char* in_ptr = buf->data + pos;
        int offset = MP3FindSyncWord(in_ptr, bytes_left);
        if (offset < 0) {
            if (!done) {
                vTaskDelay(pdMS_TO_TICKS(20));
                continue;
            }
            break;   // da tai xong ma khong con sync word nao -> het that
        }
        in_ptr += offset;
        bytes_left -= offset;
        pos += offset;

        if (!done && bytes_left < 4) {
            // Chua du 4 byte de doc header, doi tai them.
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        if (!IsPlausibleMp3Header(in_ptr, bytes_left)) {
            pos += 1;
            if (++consecutive_errors > kMaxConsecutiveErrors) {
                ESP_LOGE(TAG, "Qua nhieu byte khong hop le lien tiep - huy phat");
                state_.store(MediaPlayerState::kError);
                break;
            }
            continue;
        }

        int err = MP3Decode(decoder, &in_ptr, &bytes_left, pcm_buf, 0);
        size_t consumed = (filled - pos) - bytes_left;
        pos += consumed;

        if (err != 0) {
            ESP_LOGW(TAG, "Loi giai ma MP3 frame: %d, bo qua", err);
            pos += 1;
            if (++consecutive_errors > kMaxConsecutiveErrors) {
                ESP_LOGE(TAG, "Qua nhieu loi giai ma lien tiep - huy phat");
                state_.store(MediaPlayerState::kError);
                break;
            }
            continue;
        }
        consecutive_errors = 0;

        MP3FrameInfo frame_info;
        MP3GetLastFrameInfo(decoder, &frame_info);
        int in_frames = frame_info.outputSamps / frame_info.nChans;
        std::vector<int16_t> pcm = AdaptPcm(
            pcm_buf, in_frames, frame_info.nChans, frame_info.samprate,
            codec->output_channels(), codec->output_sample_rate());
        codec->OutputData(pcm);
    }

    heap_caps_free(pcm_buf);
    MP3FreeDecoder(decoder);

    if (!stop_requested_.load() && state_.load() != MediaPlayerState::kError) {
        state_.store(MediaPlayerState::kStopped);
        ESP_LOGI(TAG, "Phat xong: %s", buf->display_name.c_str());
    }
}