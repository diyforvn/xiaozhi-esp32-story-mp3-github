#include "sdcard_music_player.h"
#include "mcp_server.h"
#include "board.h"
#include "audio_codec.h"

#include <esp_log.h>
#include <esp_heap_caps.h>
#include <esp_vfs_fat.h>
#include <driver/sdspi_host.h>
#include <driver/spi_common.h>
#include <sdmmc_cmd.h>

#include <dirent.h>
#include <sys/stat.h>
#include <cstdio>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <stdexcept>

extern "C" {
#include "mp3dec.h"   // MP3InitDecoder / MP3Decode / MP3GetLastFrameInfo / MP3FindSyncWord
}

#if CONFIG_ENABLE_STORY_PLAYER || CONFIG_ENABLE_MUSIC_PLAYER
#include "media_library.h"
#endif

#define TAG "SdMusicPlayer"

// ----------------------------------------------------------------------------
// Cau hinh chan SPI cho the nho SD. Dieu chinh trong menuconfig
// (Smart Home IoT -> SD card music player) cho dung phan cung cua ban.
// ----------------------------------------------------------------------------
#ifndef CONFIG_SDMUSIC_MOUNT_POINT
#define CONFIG_SDMUSIC_MOUNT_POINT "/sdcard"
#endif

namespace {

// Toi da so file quet duoc, tranh tran RAM neu the co qua nhieu file rac.
constexpr size_t kMaxTracks = 500;
constexpr int kMaxScanDepth = 6;

bool HasMp3Extension(const std::string& filename) {
    if (filename.size() < 4) return false;
    std::string ext = filename.substr(filename.size() - 4);
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
    return ext == ".mp3";
}

std::string StripExtension(const std::string& filename) {
    auto pos = filename.find_last_of('.');
    return (pos == std::string::npos) ? filename : filename.substr(0, pos);
}

// ---- Fuzzy match tieng Viet khong dau (rut gon tu media_library.cc) ----

struct DiacriticMap { const char* utf8; char ascii; };
const DiacriticMap kViMap[] = {
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
                for (auto& m : kViMap) {
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
            if (c >= 0xC0) {
                // Ky tu UTF-8 nhieu byte KHONG nam trong bang tren - thuong
                // la dau thanh tieng Viet dang to hop roi (combining
                // diacritical mark, Unicode NFD - hay gap khi file duoc
                // chep tu may Mac) thay vi 1 ky tu duy nhat (NFC) nhu bang
                // kViMap dang gia dinh. Bo qua NGUYEN CA chuoi byte cua no
                // (khong chi 1 byte) de KHONG lam lech nhip doc UTF-8 cho
                // phan con lai cua chuoi phia sau - chu am goc (a/e/o/...)
                // di truoc dau thanh da duoc ghi nhan roi nen bo qua dau la
                // an toan, ket qua van la ban khong dau dung.
                size_t seq_len = (c >= 0xF0) ? 4 : (c >= 0xE0) ? 3 : 2;
                i += std::min(seq_len, s.size() - i);
            } else {
                // Thay dau gach ngang / gach duoi bang khoang trang de tach
                // tu (ten file thuong dung "_" hoac "-" thay cho dau cach).
                char lower = (char)std::tolower(c);
                result.push_back((lower == '_' || lower == '-') ? ' ' : lower);
                i++;
            }
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

std::string RemoveSpaces(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        if (c != ' ') result.push_back(c);
    }
    return result;
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

size_t SkipId3v2Tag(const uint8_t* buf, size_t size) {
    if (size < 10 || buf[0] != 'I' || buf[1] != 'D' || buf[2] != '3') {
        return 0;
    }
    uint32_t tag_size = ((buf[6] & 0x7F) << 21) | ((buf[7] & 0x7F) << 14) |
                         ((buf[8] & 0x7F) << 7)  |  (buf[9] & 0x7F);
    size_t total = 10 + tag_size;
    if (buf[5] & 0x10) total += 10;
    return total;
}

}  // namespace

SdCardMusicPlayer& SdCardMusicPlayer::GetInstance() {
    static SdCardMusicPlayer instance;
    return instance;
}

SdCardMusicPlayer::~SdCardMusicPlayer() {
    Stop();
}

void SdCardMusicPlayer::Initialize() {
    MountSdCardIfNeeded();
    if (sd_mounted_) {
        Rescan();
    }
    RegisterMcpTools();
}

// ============================================================================
// Mount the SD qua SPI. Neu board cua ban da tu mount san CONFIG_SDMUSIC_MOUNT_POINT
// (vd trong Board::Initialize()), buoc nay se tu bo qua (mo thu duoc thu muc
// goc la coi nhu da mount).
// ============================================================================
bool SdCardMusicPlayer::MountSdCardIfNeeded() {
    if (mount_attempted_) {
        return sd_mounted_;
    }
    mount_attempted_ = true;

    // Da co ai mount san mount point nay chua (vd code rieng cua board)?
    DIR* probe = opendir(CONFIG_SDMUSIC_MOUNT_POINT);
    if (probe) {
        closedir(probe);
        ESP_LOGI(TAG, "The SD da duoc mount san tai %s", CONFIG_SDMUSIC_MOUNT_POINT);
        sd_mounted_ = true;
        return true;
    }

#if CONFIG_ENABLE_SDCARD_MUSIC_PLAYER
    spi_bus_config_t bus_cfg = {};
    bus_cfg.mosi_io_num = (gpio_num_t)CONFIG_SDMUSIC_SPI_MOSI_PIN;
    bus_cfg.miso_io_num = (gpio_num_t)CONFIG_SDMUSIC_SPI_MISO_PIN;
    bus_cfg.sclk_io_num = (gpio_num_t)CONFIG_SDMUSIC_SPI_CLK_PIN;
    bus_cfg.quadwp_io_num = -1;
    bus_cfg.quadhd_io_num = -1;
    bus_cfg.max_transfer_sz = 4000;

    esp_err_t err = spi_bus_initialize((spi_host_device_t)CONFIG_SDMUSIC_SPI_HOST,
                                        &bus_cfg, SPI_DMA_CH_AUTO);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE /* da init tu truoc */) {
        ESP_LOGE(TAG, "spi_bus_initialize that bai: %s", esp_err_to_name(err));
        return false;
    }

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = CONFIG_SDMUSIC_SPI_HOST;

    sdspi_device_config_t slot_cfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slot_cfg.gpio_cs = (gpio_num_t)CONFIG_SDMUSIC_SPI_CS_PIN;
    slot_cfg.host_id = (spi_host_device_t)CONFIG_SDMUSIC_SPI_HOST;

    esp_vfs_fat_sdmmc_mount_config_t mount_cfg = {};
    mount_cfg.format_if_mount_failed = false;
    mount_cfg.max_files = 5;
    mount_cfg.allocation_unit_size = 16 * 1024;

    sdmmc_card_t* card = nullptr;
    err = esp_vfs_fat_sdspi_mount(CONFIG_SDMUSIC_MOUNT_POINT, &host, &slot_cfg,
                                   &mount_cfg, &card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Mount the SD that bai (%s). Kiem tra day noi / dinh dang FAT32 / "
                      "chan SPI trong menuconfig.", esp_err_to_name(err));
        sd_mounted_ = false;
        return false;
    }

    ESP_LOGI(TAG, "Mount the SD thanh cong tai %s", CONFIG_SDMUSIC_MOUNT_POINT);
    sd_mounted_ = true;
    return true;
#else
    ESP_LOGW(TAG, "Khong tim thay the SD tai %s va tinh nang tu mount dang tat "
                  "(bat 'Enable SD card music player' trong menuconfig).",
             CONFIG_SDMUSIC_MOUNT_POINT);
    return false;
#endif
}

// ============================================================================
// Quet the SD tim file .mp3
// ============================================================================
void SdCardMusicPlayer::ScanDirectory(const std::string& dir, int depth) {
    if (depth > kMaxScanDepth) return;

    DIR* d = opendir(dir.c_str());
    if (!d) {
        ESP_LOGW(TAG, "Khong mo duoc thu muc %s", dir.c_str());
        return;
    }

    struct dirent* entry;
    while ((entry = readdir(d)) != nullptr) {
        if (tracks_.size() >= kMaxTracks) break;

        std::string name = entry->d_name;
        if (name == "." || name == ".." || name.empty()) continue;
        // Bo qua file/thu muc an va rac cua he dieu hanh khac (macOS, Windows...)
        if (name[0] == '.' || name[0] == '_') continue;

        std::string full_path = dir + "/" + name;

        struct stat st;
        if (stat(full_path.c_str(), &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            ScanDirectory(full_path, depth + 1);
        } else if (S_ISREG(st.st_mode) && HasMp3Extension(name)) {
            SdTrackEntry track;
            track.id = std::to_string(tracks_.size());
            track.name = StripExtension(name);
            track.path = full_path;
            tracks_.push_back(std::move(track));
        }
    }
    closedir(d);
}

bool SdCardMusicPlayer::Rescan() {
    if (!sd_mounted_ && !MountSdCardIfNeeded()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(tracks_mutex_);
    tracks_.clear();
    ScanDirectory(CONFIG_SDMUSIC_MOUNT_POINT, 0);
    ESP_LOGI(TAG, "Quet the SD xong: tim thay %d file MP3", (int)tracks_.size());
    return !tracks_.empty();
}

std::vector<SdTrackEntry> SdCardMusicPlayer::ListAll() const {
    std::lock_guard<std::mutex> lock(tracks_mutex_);
    return tracks_;
}

const SdTrackEntry* SdCardMusicPlayer::FindById(const std::string& id) const {
    std::lock_guard<std::mutex> lock(tracks_mutex_);
    for (auto& t : tracks_) {
        if (t.id == id) return &t;
    }
    return nullptr;
}

const SdTrackEntry* SdCardMusicPlayer::FindBestMatch(const std::string& query) const {
    std::lock_guard<std::mutex> lock(tracks_mutex_);
    std::string q = NormalizeVi(query);

    const SdTrackEntry* best = nullptr;
    int best_score = 0;
    for (auto& t : tracks_) {
        std::string name_norm = NormalizeVi(t.name);
        int score = 0;
        if (q.find(name_norm) != std::string::npos || name_norm.find(q) != std::string::npos) {
            score += 6;
        }
        score += WordOverlapScore(q, name_norm);
        int dist = EditDistance(q, name_norm);
        int max_len = (int)std::max(q.size(), name_norm.size());
        if (max_len > 0) {
            double ratio = 1.0 - (double)dist / (double)max_len;
            if (ratio > 0.4) score += (int)(ratio * 4);
        }

        // So khop rieng cho ten file khong co dau cach/gach noi (vd
        // "chayngaydi.mp3" dinh lien thanh 1 "tu" duy nhat). Bo het khoang
        // trang o ca 2 ben roi kiem tra dinh lien mach - cach nay van khop
        // dung ngay ca khi cau lenh co them tu dem o dau/cuoi
        // (vd "tim bai hat chay ngay di" -> "timbaihatchayngaydi" van chua
        // "chayngaydi" ben trong, lien mach).
        std::string q_nospace = RemoveSpaces(q);
        std::string name_nospace = RemoveSpaces(name_norm);
        if (name_nospace.size() >= 3 && q_nospace.find(name_nospace) != std::string::npos) {
            score += 6;
        } else if (q_nospace.size() >= 3 && name_nospace.find(q_nospace) != std::string::npos) {
            score += 6;
        } else {
            int dist2 = EditDistance(q_nospace, name_nospace);
            int max_len2 = (int)std::max(q_nospace.size(), name_nospace.size());
            if (max_len2 > 0) {
                double ratio2 = 1.0 - (double)dist2 / (double)max_len2;
                if (ratio2 > 0.5) score += (int)(ratio2 * 4);
            }
        }

        if (score > best_score) {
            best_score = score;
            best = &t;
        }
    }
    return (best_score >= 3) ? best : nullptr;
}

// ============================================================================
// Dieu khien phat / dung / tam dung
// ============================================================================
std::string SdCardMusicPlayer::current_name() const {
    std::lock_guard<std::mutex> lock(name_mutex_);
    return current_name_;
}

void SdCardMusicPlayer::Pause() {
    if (state_.load() == SdPlayerState::kPlaying) {
        state_.store(SdPlayerState::kPaused);
    }
}

void SdCardMusicPlayer::Resume() {
    if (state_.load() == SdPlayerState::kPaused) {
        state_.store(SdPlayerState::kPlaying);
    }
}

void SdCardMusicPlayer::Stop() {
    // Nguoi dung/AI chu dong yeu cau dung -> KHONG tu phat lai sau nay.
    {
        std::lock_guard<std::mutex> lock(resume_mutex_);
        resume_pending_ = false;
    }
    StopAndWait();
}

void SdCardMusicPlayer::StopForConversation() {
    // Chi nho lai bai dang phat/dang tam dung O LAN DAU TIEN bi ngat -
    // cac lan goi lien tiep sau do (vd Connecting -> Listening -> Speaking
    // trong cung 1 phien hoi thoai) se khong ghi de mat thong tin bai da nho.
    {
        std::lock_guard<std::mutex> rlock(resume_mutex_);
        if (!resume_pending_) {
            auto st = state_.load();
            if (st == SdPlayerState::kPlaying || st == SdPlayerState::kPaused) {
                std::lock_guard<std::mutex> tlock(tracks_mutex_);
                if (current_index_ >= 0 && current_index_ < (int)tracks_.size()) {
                    pending_resume_entry_ = tracks_[current_index_];
                    resume_pending_ = true;
                }
            }
        }
    }
    StopAndWait();
}

void SdCardMusicPlayer::ResumeAfterConversation() {
    SdTrackEntry entry;
    {
        std::lock_guard<std::mutex> lock(resume_mutex_);
        if (!resume_pending_) return;
        resume_pending_ = false;
        entry = pending_resume_entry_;
    }
    // Phat lai TU DAU bai da bi ngat (khong phai tiep tuc dung vi tri cu -
    // dung y "dung han" ma nguoi dung yeu cau, khac voi Pause()/Resume()
    // giu nguyen vi tri).
    Play(entry);
}

void SdCardMusicPlayer::StopAndWait() {
    std::lock_guard<std::mutex> lock(playback_mutex_);
    if (state_.load() == SdPlayerState::kIdle || state_.load() == SdPlayerState::kStopped) {
        return;
    }
    stop_requested_.store(true);
    while (player_handle_ != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    stop_requested_.store(false);
    state_.store(SdPlayerState::kStopped);
    {
        std::lock_guard<std::mutex> nlock(name_mutex_);
        current_name_.clear();
    }
}

bool SdCardMusicPlayer::RequestPlay(const SdTrackEntry& entry) {
    bool in_conversation = is_active_session_checker_ && is_active_session_checker_();
    if (!in_conversation) {
        // Dang Idle: phat ngay, khong can hoan lai.
        return Play(entry);
    }

    // Dang trong hoi thoai (Connecting/Listening/Speaking): KHONG bat am
    // thanh ngay de tranh chen vao giong TTS. Ghi nho bai nay (GHI DE bai
    // cu neu co - vd nguoi dung vua doi y doi bai khac giua chung) de
    // ResumeAfterConversation() tu phat that khi quay ve Idle.
    {
        std::lock_guard<std::mutex> lock(resume_mutex_);
        pending_resume_entry_ = entry;
        resume_pending_ = true;
    }
    // Neu dang co bai khac dang phat/tam dung du, dung han lai (khong de
    // no tiep tuc phat ngam trong luc cho).
    StopAndWait();

    // Cap nhat current_index_ luon o day de self.sdmusic action=next/
    // previous goi ngay sau do (van trong hoi thoai) tinh dung vi tri ke
    // tiep dua tren bai VUA duoc yeu cau, khong phai bai cu.
    {
        std::lock_guard<std::mutex> lock(tracks_mutex_);
        for (size_t i = 0; i < tracks_.size(); i++) {
            if (tracks_[i].id == entry.id) {
                current_index_ = (int)i;
                break;
            }
        }
    }
    return true;
}

bool SdCardMusicPlayer::Play(const SdTrackEntry& entry) {
    if (!sd_mounted_) {
        return false;
    }

#if CONFIG_ENABLE_STORY_PLAYER || CONFIG_ENABLE_MUSIC_PLAYER
    // Chi 1 nguon am thanh "nhac nen" duoc phat tai 1 thoi diem: dung
    // truyen/nhac GitHub (neu dang phat) truoc khi phat nhac tu the SD.
    MediaLibrary::GetInstance().Stop();
#endif

    StopAndWait();

    {
        std::lock_guard<std::mutex> lock(tracks_mutex_);
        for (size_t i = 0; i < tracks_.size(); i++) {
            if (tracks_[i].id == entry.id) {
                current_index_ = (int)i;
                break;
            }
        }
    }

    std::lock_guard<std::mutex> lock(playback_mutex_);
    {
        std::lock_guard<std::mutex> nlock(name_mutex_);
        current_name_ = entry.name;
    }
    state_.store(SdPlayerState::kPlaying);
    stop_requested_.store(false);

    struct PlayArgs { SdCardMusicPlayer* self; SdTrackEntry entry; };
    auto* args = new PlayArgs{this, entry};
    // Stack lon vi vong lap decode MP3 + doc file dieu chay tren cung 1 task.
    BaseType_t ok = xTaskCreatePinnedToCore(
        [](void* arg) {
            auto* a = static_cast<PlayArgs*>(arg);
            a->self->PlayerTask(a->entry);
            a->self->player_handle_ = nullptr;
            delete a;
            vTaskDelete(nullptr);
        }, "sdmusic_play", 8192, args, 5, &player_handle_, 0);

    if (ok != pdPASS) {
        ESP_LOGE(TAG, "Khong tao duoc player task");
        delete args;
        state_.store(SdPlayerState::kError);
        return false;
    }
    return true;
}

bool SdCardMusicPlayer::PlayRelative(int offset) {
    SdTrackEntry entry;
    {
        std::lock_guard<std::mutex> lock(tracks_mutex_);
        if (tracks_.empty() || current_index_ < 0) return false;
        int next_index = current_index_ + offset;
        if (next_index < 0 || next_index >= (int)tracks_.size()) return false;
        entry = tracks_[next_index];
    }
    // RequestPlay()/Play() se tu lay lai tracks_mutex_ ben trong - phai goi
    // sau khi da tra lock o tren de tranh deadlock. Dung RequestPlay() (khong
    // phai Play() truc tiep) de next/previous cung hoan lai dung luc neu
    // dang trong hoi thoai, giong het hanh vi cua action=play.
    return RequestPlay(entry);
}

// ============================================================================
// Player task: doc file tu the SD theo tung khoi (chunk), giai ma MP3, phat
// ra AudioCodec. Khong can task tai/downloader rieng nhu MediaLibrary vi
// doc the SD la dong bo (blocking) va du nhanh de doc-vua-giai-ma trong
// cung 1 vong lap.
// ============================================================================
void SdCardMusicPlayer::PlayerTask(SdTrackEntry entry) {
    FILE* f = fopen(entry.path.c_str(), "rb");
    if (!f) {
        ESP_LOGE(TAG, "Khong mo duoc file: %s", entry.path.c_str());
        state_.store(SdPlayerState::kError);
        return;
    }

    constexpr size_t kChunkSize = 8 * 1024;
    constexpr size_t kRingCapacity = 32 * 1024;   // du cho vai frame MP3

    uint8_t* ring = (uint8_t*)heap_caps_malloc(kRingCapacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!ring) {
        ring = (uint8_t*)heap_caps_malloc(kRingCapacity, MALLOC_CAP_8BIT);
    }
    if (!ring) {
        ESP_LOGE(TAG, "Khong cap phat duoc buffer doc file (het bo nho)");
        fclose(f);
        state_.store(SdPlayerState::kError);
        return;
    }

    auto codec = Board::GetInstance().GetAudioCodec();
    HMP3Decoder decoder = MP3InitDecoder();
    if (!decoder) {
        ESP_LOGE(TAG, "Khong khoi tao duoc MP3 decoder");
        heap_caps_free(ring);
        fclose(f);
        state_.store(SdPlayerState::kError);
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
        heap_caps_free(ring);
        fclose(f);
        state_.store(SdPlayerState::kError);
        return;
    }

    size_t filled = 0;     // so byte hop le hien co trong ring, tinh tu dau ring[0]
    bool eof = false;
    bool skipped_id3 = false;
    int consecutive_errors = 0;
    const int kMaxConsecutiveErrors = 200;

    while (!stop_requested_.load()) {
        if (state_.load() == SdPlayerState::kPaused) {
            vTaskDelay(pdMS_TO_TICKS(30));
            continue;
        }

        // Nap them du lieu tu file neu con cho trong ring va chua het file.
        if (!eof && filled < kRingCapacity) {
            size_t to_read = std::min(kChunkSize, kRingCapacity - filled);
            size_t n = fread(ring + filled, 1, to_read, f);
            if (n > 0) {
                filled += n;
            }
            if (n < to_read) {
                eof = true;   // het file (hoac loi doc, coi nhu het)
            }
        }

        if (!skipped_id3 && filled >= 10 && ring[0] == 'I' && ring[1] == 'D' && ring[2] == '3') {
            size_t tag_total = SkipId3v2Tag(ring, filled);
            size_t skip = std::min(tag_total, filled);
            if (skip > 0) {
                memmove(ring, ring + skip, filled - skip);
                filled -= skip;
            }
            skipped_id3 = true;
            continue;
        }
        skipped_id3 = true;

        if (filled == 0) {
            if (eof) break;   // het file that su, khong con gi de giai ma
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        unsigned char* in_ptr = ring;
        int bytes_left = (int)filled;
        int offset = MP3FindSyncWord(in_ptr, bytes_left);
        if (offset < 0) {
            if (!eof) {
                // Chua thay sync word trong khoi da doc, doc them roi thu lai.
                vTaskDelay(pdMS_TO_TICKS(5));
                continue;
            }
            break;  
        }
        in_ptr += offset;
        bytes_left -= offset;

        if (!eof && bytes_left < 4) {
            vTaskDelay(pdMS_TO_TICKS(5));
            continue;
        }

        if (!IsPlausibleMp3Header(in_ptr, bytes_left)) {
            // Bo 1 byte roi thu lai tu dau ring (memmove ben duoi se dich).
            size_t consumed = offset + 1;
            consumed = std::min(consumed, filled);
            memmove(ring, ring + consumed, filled - consumed);
            filled -= consumed;
            if (++consecutive_errors > kMaxConsecutiveErrors) {
                ESP_LOGE(TAG, "Qua nhieu byte khong hop le lien tiep - huy phat");
                state_.store(SdPlayerState::kError);
                break;
            }
            continue;
        }

        int err = MP3Decode(decoder, &in_ptr, &bytes_left, pcm_buf, 0);
        size_t consumed = (filled - offset) - bytes_left;
        consumed += offset;
        consumed = std::min(consumed, filled);
        memmove(ring, ring + consumed, filled - consumed);
        filled -= consumed;

        if (err != 0) {
            ESP_LOGW(TAG, "Loi giai ma MP3 frame: %d, bo qua", err);
            if (filled > 0) {
                memmove(ring, ring + 1, filled - 1);
                filled -= 1;
            }
            if (++consecutive_errors > kMaxConsecutiveErrors) {
                ESP_LOGE(TAG, "Qua nhieu loi giai ma lien tiep - huy phat");
                state_.store(SdPlayerState::kError);
                break;
            }
            continue;
        }
        consecutive_errors = 0;

        MP3FrameInfo frame_info;
        MP3GetLastFrameInfo(decoder, &frame_info);
        int in_frames = frame_info.outputSamps / frame_info.nChans;

        // Adapt kenh/tan so cho khop dau ra codec (giong media_library.cc).
        std::vector<int16_t> pcm;
        if (frame_info.nChans == codec->output_channels() &&
            frame_info.samprate == codec->output_sample_rate()) {
            pcm.assign(pcm_buf, pcm_buf + (size_t)in_frames * frame_info.nChans);
        } else if (frame_info.nChans == 2 && codec->output_channels() == 1 &&
                   frame_info.samprate == codec->output_sample_rate()) {
            pcm.reserve(in_frames);
            for (int i = 0; i < in_frames; i++) {
                int32_t l = pcm_buf[i * 2];
                int32_t r = pcm_buf[i * 2 + 1];
                pcm.push_back((int16_t)((l + r) / 2));
            }
        } else {
            // Truong hop tan so khac nhau: resample tuyen tinh don gian.
            int out_channels = codec->output_channels();
            std::vector<int16_t> ch_adjusted;
            if (frame_info.nChans == out_channels) {
                ch_adjusted.assign(pcm_buf, pcm_buf + (size_t)in_frames * frame_info.nChans);
            } else if (frame_info.nChans == 2 && out_channels == 1) {
                for (int i = 0; i < in_frames; i++) {
                    ch_adjusted.push_back((int16_t)((pcm_buf[i*2] + pcm_buf[i*2+1]) / 2));
                }
            } else {  // mono -> stereo
                for (int i = 0; i < in_frames; i++) {
                    ch_adjusted.push_back(pcm_buf[i]);
                    ch_adjusted.push_back(pcm_buf[i]);
                }
            }
            int out_rate = codec->output_sample_rate();
            if (frame_info.samprate == out_rate || in_frames <= 1) {
                pcm = std::move(ch_adjusted);
            } else {
                int out_frames = (int)((int64_t)in_frames * out_rate / frame_info.samprate);
                pcm.reserve((size_t)out_frames * out_channels);
                double step = (double)frame_info.samprate / (double)out_rate;
                double pos = 0.0;
                for (int i = 0; i < out_frames; i++) {
                    int idx0 = (int)pos;
                    int idx1 = std::min(idx0 + 1, in_frames - 1);
                    double frac = pos - idx0;
                    for (int c = 0; c < out_channels; c++) {
                        int16_t s0 = ch_adjusted[(size_t)idx0 * out_channels + c];
                        int16_t s1 = ch_adjusted[(size_t)idx1 * out_channels + c];
                        pcm.push_back((int16_t)(s0 + (s1 - s0) * frac));
                    }
                    pos += step;
                }
            }
        }
        codec->OutputData(pcm);
    }

    heap_caps_free(pcm_buf);
    heap_caps_free(ring);
    MP3FreeDecoder(decoder);
    fclose(f);

    if (!stop_requested_.load() && state_.load() != SdPlayerState::kError) {
        state_.store(SdPlayerState::kStopped);
        ESP_LOGI(TAG, "Phat xong: %s", entry.name.c_str());
    }
}

// ============================================================================
// MCP tools: self.sdmusic.*
// ============================================================================
void SdCardMusicPlayer::RegisterMcpTools() {
    auto& mcp = McpServer::GetInstance();

    
    mcp.AddTool("self.sdmusic",
        "Play or control an MP3 file stored locally on the device's SD card "
        "(works fully offline, no internet needed). `action` selects what to "
        "do: \"list\" (see every track_id + name currently indexed from the "
        "SD card - call this first if you are not sure what songs exist, or "
        "if \"play\" with a `query` fails to find a match, so you can look at "
        "the real names and retry with a better query or an exact track_id), "
        "\"play\" (need `track_id` or `query`), \"stop\", \"pause\", "
        "\"resume\", \"next\", \"previous\", \"rescan\" (re-read the SD card "
        "after inserting a new card or adding files), or \"status\" (just "
        "report current playback state). A play requested during a conversation is queued until standby. Every response includes "
        "`track_count` (how many MP3 files are currently indexed) so you "
        "always know whether the SD card actually has songs on it. For "
        "\"play\", pass `track_id` if known, otherwise pass `query` with the "
        "song title/filename keywords and the best match on the SD card will "
        "be found automatically (fuzzy match, tolerant of Vietnamese "
        "diacritics and small typos) - but the filenames on the card may not "
        "closely resemble spoken song titles, so if a `query` play fails, "
        "use \"list\" to see the actual names before giving up.",
        PropertyList({
            Property("action", kPropertyTypeString),
            Property("track_id", kPropertyTypeString, std::string("")),
            Property("query", kPropertyTypeString, std::string(""))
        }),
        [](const PropertyList& properties) -> ReturnValue {
            auto& player = SdCardMusicPlayer::GetInstance();
            auto action = properties["action"].value<std::string>();

            if (action == "list") {
               
            } else if (action == "play") {
                auto id = properties["track_id"].value<std::string>();
                auto query = properties["query"].value<std::string>();

                if (!player.is_sd_mounted()) {
                    throw std::runtime_error("Khong tim thay the nho SD (chua cam the hoac mount that bai)");
                }

                const SdTrackEntry* entry = nullptr;
                if (!id.empty()) {
                    entry = player.FindById(id);
                    if (!entry) {
                        throw std::runtime_error("Khong tim thay track_id=" + id);
                    }
                } else if (!query.empty()) {
                    entry = player.FindBestMatch(query);
                    if (!entry) {
                        throw std::runtime_error("Khong tim thay bai nao tren the SD phu hop voi: " + query +
                            ". Dung action=list de xem danh sach ten file that tren the SD roi thu lai.");
                    }
                } else {
                    throw std::runtime_error("Can truyen track_id hoac query khi action=play");
                }

                if (!player.RequestPlay(*entry)) {
                    throw std::runtime_error("Khong phat duoc: " + entry->name);
                }
            } else if (action == "stop") {
                player.Stop();
            } else if (action == "pause") {
                player.Pause();
            } else if (action == "resume") {
                player.Resume();
            } else if (action == "next") {
                if (!player.PlayRelative(1)) {
                    throw std::runtime_error("Khong con bai tiep theo");
                }
            } else if (action == "previous") {
                if (!player.PlayRelative(-1)) {
                    throw std::runtime_error("Khong co bai truoc do");
                }
            } else if (action == "rescan") {
                player.Rescan();
            } else if (action != "status") {
                throw std::runtime_error("action khong hop le: " + action);
            }

            cJSON* json = cJSON_CreateObject();
            const bool queued = player.playback_queued();
            const char* state_str = queued ? "queued" : "idle";
            if (!queued) switch (player.state()) {
                case SdPlayerState::kPlaying: state_str = "playing"; break;
                case SdPlayerState::kPaused:  state_str = "paused"; break;
                case SdPlayerState::kStopped: state_str = "stopped"; break;
                case SdPlayerState::kError:   state_str = "error"; break;
                default: break;
            }
            cJSON_AddStringToObject(json, "state", state_str);
            cJSON_AddBoolToObject(json, "queued", queued);
            cJSON_AddStringToObject(json, "current_name", player.current_name().c_str());
            cJSON_AddBoolToObject(json, "sd_mounted", player.is_sd_mounted());

            auto tracks = player.ListAll();
            cJSON_AddNumberToObject(json, "track_count", (int)tracks.size());
            if (action == "list") {
                // Gioi han so luong tra ve de tranh JSON qua to (context AI
                // co han) - 40 bai la du cho hau het the nho gia dinh.
                constexpr size_t kMaxListed = 40;
                cJSON* arr = cJSON_CreateArray();
                for (size_t i = 0; i < tracks.size() && i < kMaxListed; i++) {
                    cJSON* item = cJSON_CreateObject();
                    cJSON_AddStringToObject(item, "track_id", tracks[i].id.c_str());
                    cJSON_AddStringToObject(item, "name", tracks[i].name.c_str());
                    cJSON_AddItemToArray(arr, item);
                }
                cJSON_AddItemToObject(json, "tracks", arr);
                if (tracks.size() > kMaxListed) {
                    cJSON_AddBoolToObject(json, "truncated", true);
                }
            }
            return json;
        });

    mcp.AddUserOnlyTool("self.sdmusic.debug_list_index",
        "Debug: list every MP3 file currently indexed from the SD card, with "
        "its track_id and name.",
        PropertyList(),
        [](const PropertyList&) -> ReturnValue {
            auto& player = SdCardMusicPlayer::GetInstance();
            auto tracks = player.ListAll();

            cJSON* arr = cJSON_CreateArray();
            for (auto& t : tracks) {
                cJSON* item = cJSON_CreateObject();
                cJSON_AddStringToObject(item, "id", t.id.c_str());
                cJSON_AddStringToObject(item, "name", t.name.c_str());
                cJSON_AddStringToObject(item, "path", t.path.c_str());
                cJSON_AddItemToArray(arr, item);
            }
            cJSON* root = cJSON_CreateObject();
            cJSON_AddNumberToObject(root, "count", (int)tracks.size());
            cJSON_AddBoolToObject(root, "sd_mounted", player.is_sd_mounted());
            cJSON_AddItemToObject(root, "tracks", arr);
            return root;
        });
}