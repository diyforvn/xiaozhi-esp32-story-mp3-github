#ifndef MEDIA_LIBRARY_H
#define MEDIA_LIBRARY_H

#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <functional>
#include <unordered_map>

#include <cJSON.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ============================================================================
// MediaLibrary
//
// Nguon du lieu DUY NHAT cho ca "story" (ke chuyen) va "music" (bai hat):
// 1 repo GitHub, 1 file index.json, 2 nhanh thu muc con:
//   - stories/...   (truyen ke, category != "song")
//   - songs/...     (bai hat, category == "song")
// StoryPlayer va MusicPlayer chi la 2 lop MONG dang ky MCP tool rieng
// (self.story.* / self.music.*), nhung ca hai cung goi vao 1 engine nay -
// dung chung index.json, chung co che tai + cache + giai ma.
//
// ============================================================================

struct StoryEntry {
    std::string id;
    std::string name;
    std::string path;                  // vd: "stories/three_pigs.mp3" hoac "songs/x.mp3"
    std::vector<std::string> tags;
    std::string category;              // "song" = bai hat, con lai coi la truyen
    std::string age;
    int duration_sec = 0;
};

enum class MediaPlayerState {
    kIdle,
    kBuffering,     // dang cho du prebuffer truoc khi bat dau phat
    kPlaying,
    kPaused,
    kStopped,
    kError,
};

struct MediaBuffer {
    uint8_t* data = nullptr;
    size_t capacity = 0;
    std::atomic<size_t> filled{0};          // so byte da tai VA da cong bo cho reader
    std::atomic<bool> download_done{false};
    std::atomic<bool> download_error{false};
    std::string id;
    std::string display_name;
    int64_t last_used_tick = 0;             // chi truy cap duoi cache_mutex_ (dung cho LRU)

    ~MediaBuffer();
};
using MediaBufferPtr = std::shared_ptr<MediaBuffer>;

class MediaLibrary {
public:
    static MediaLibrary& GetInstance();

    void EnsureIndexLoaded();
    bool RefreshIndex();

    void SetBaseUrl(const std::string& url);
    std::string base_url() const { return base_url_; }

    const StoryEntry* FindById(const std::string& id) const;    const StoryEntry* FindBestMatch(const std::string& query,
                                     const std::string& age,
                                     const std::function<bool(const StoryEntry&)>& filter) const;

    std::vector<StoryEntry> DebugListAll(std::string* out_base_url = nullptr) const;

    bool Play(const StoryEntry& entry);
    void Stop();
    void Pause();
    void Resume();

    void DuckForSpeech();
    void UnduckAfterSpeech();
    void SetActiveSessionChecker(std::function<bool()> checker) {
        is_active_session_checker_ = std::move(checker);
    }

    MediaPlayerState state() const { return state_.load(); }
    std::string current_name() const;

private:
    MediaLibrary();
    ~MediaLibrary();
    MediaLibrary(const MediaLibrary&) = delete;
    MediaLibrary& operator=(const MediaLibrary&) = delete;

    bool DownloadIndexJson(std::string& out_json);
    bool ParseIndexJson(const std::string& json_text);
    std::string BuildUrl(const StoryEntry& entry) const;

    void StopAndWait();
    MediaBufferPtr GetOrCreateBuffer_Locked(const std::string& id,
                                             const std::string& display_name,
                                             bool& need_download);
    void EnsureFreePsramForNewBuffer_Locked(const std::string& keep_id, size_t needed_bytes);
    void EvictIfNeeded_Locked(const std::string& keep_id);

    static void DownloaderTaskEntry(void* arg);
    void DownloaderTask(MediaBufferPtr buf, std::string url);

    static void DecoderTaskEntry(void* arg);
    void DecoderTask(MediaBufferPtr buf);

    static std::vector<int16_t> AdaptPcm(const int16_t* in, int in_frames,
                                          int in_channels, int in_rate,
                                          int out_channels, int out_rate);

    std::vector<StoryEntry> stories_;
    mutable std::mutex stories_mutex_;

    std::string base_url_;
    std::string index_path_ = "index.json";

    std::unordered_map<std::string, MediaBufferPtr> cache_by_id_;
    mutable std::mutex cache_mutex_;

    MediaBufferPtr current_buffer_;   // buffer cua phien phat hien tai
    std::atomic<MediaPlayerState> state_{MediaPlayerState::kIdle};
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> auto_paused_by_narration_{false};
  
    std::atomic<bool> speech_active_{false};
    std::function<bool()> is_active_session_checker_;
    std::string current_name_;
    mutable std::mutex name_mutex_;

    TaskHandle_t downloader_handle_ = nullptr;
    TaskHandle_t decoder_handle_ = nullptr;
    std::mutex playback_mutex_;   // chi 1 phien Play()/Stop() tai 1 thoi diem

    // ---- Cac thong so co the chinh theo RAM thuc te cua board ----
    // 4MB/track: du cho hau het truyen/bai hat (~35 phut @16kbps mono).
    // Board ESP32-S3 N16R8 co 8MB PSRAM, nhung con phai chia se cho
    // framebuffer man hinh/audio codec/... nen KHONG dat qua cao.
    static constexpr size_t kPerTrackCapacity = 4 * 1024 * 1024;
    // Bat dau giai ma ngay khi co tung nay byte (khong can cho tai xong het).
    static constexpr size_t kPrebufferBytes = 32 * 1024;
    static constexpr size_t kHttpReadChunkSize = 4 * 1024;
    // Cache them toi da ~1 track ngoai track dang phat (khong tinh buffer
    // dang phat vao ngan sach nay).
    static constexpr size_t kCacheBudgetBytes = 4 * 1024 * 1024;
};

#endif  // MEDIA_LIBRARY_H