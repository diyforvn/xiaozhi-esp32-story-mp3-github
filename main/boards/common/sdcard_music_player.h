#ifndef SDCARD_MUSIC_PLAYER_H
#define SDCARD_MUSIC_PLAYER_H

#include <string>
#include <vector>
#include <atomic>
#include <mutex>
#include <functional>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ============================================================================
// SdCardMusicPlayer
//
// Phat nhac MP3 doc TRUC TIEP tu the nho SD (khong can WiFi/Internet, khong
// dung chung engine voi MediaLibrary vi khong co buoc "tai ve" qua HTTP).
//
// - Tu mount the SD qua SPI (neu board chua tu mount san o cung mount point).
// - Quet de quy toan bo the, tim moi file *.mp3, dat "id" tang dan.
// - Dang ky cac MCP tool self.sdmusic.* de Xiaozhi AI chon bai bang giong
//   noi: theo id chinh xac (self.sdmusic.debug_list_index) hoac theo query
//   (ten bai/tu khoa, tu dong fuzzy-match kieu tieng Viet khong dau).
// - Dung chung dinh dang MP3 + AudioCodec voi MediaLibrary nhung la mot
//   playback engine doc lap (tranh 2 nguon cung tranh phat 1 luc, xem
//   Play()/Stop() o day cung se dung MediaLibrary neu dang phat truyen/nhac
//   GitHub).
// ============================================================================

struct SdTrackEntry {
    std::string id;     // "0", "1", "2"... theo thu tu quet duoc
    std::string name;    // ten hien thi, rut tu ten file (bo duoi .mp3)
    std::string path;    // duong dan tuyet doi tren VFS, vd /sdcard/Nhac/a.mp3
};

enum class SdPlayerState {
    kIdle,
    kPlaying,
    kPaused,
    kStopped,
    kError,
};

class SdCardMusicPlayer {
public:
    static SdCardMusicPlayer& GetInstance();

    void Initialize();

    // Mount lai + quet lai toan bo the nho (goi lai khi vua cam/doi the).
    bool Rescan();

    const SdTrackEntry* FindById(const std::string& id) const;
    const SdTrackEntry* FindBestMatch(const std::string& query) const;
    std::vector<SdTrackEntry> ListAll() const;

    // Goi tu ben ngoai (Application) de SdCardMusicPlayer biet hien co dang
    // trong 1 phien hoi thoai hay khong (checker tra ve true = dang hoi
    // thoai). Dung cho RequestPlay() quyet dinh phat ngay hay hoan lai.
    void SetActiveSessionChecker(std::function<bool()> checker) {
        is_active_session_checker_ = std::move(checker);
    }

    
    bool RequestPlay(const SdTrackEntry& entry);

    bool Play(const SdTrackEntry& entry);
   
    bool PlayRelative(int offset);

    void Stop();
    void Pause();
    void Resume();

    
    void StopForConversation();
    
    void ResumeAfterConversation();

    SdPlayerState state() const { return state_.load(); }
    bool playback_queued() const {
        std::lock_guard<std::mutex> lock(resume_mutex_);
        return resume_pending_;
    }
    std::string current_name() const;
    bool is_sd_mounted() const { return sd_mounted_; }

private:
    SdCardMusicPlayer() = default;
    ~SdCardMusicPlayer();
    SdCardMusicPlayer(const SdCardMusicPlayer&) = delete;
    SdCardMusicPlayer& operator=(const SdCardMusicPlayer&) = delete;

    void RegisterMcpTools();
    bool MountSdCardIfNeeded();
    void ScanDirectory(const std::string& dir, int depth);

    void StopAndWait();
    static void PlayerTaskEntry(void* arg);
    void PlayerTask(SdTrackEntry entry);

    std::vector<SdTrackEntry> tracks_;
    mutable std::mutex tracks_mutex_;
    int current_index_ = -1;   // vi tri trong tracks_ cua bai dang phat/vua phat

    std::atomic<SdPlayerState> state_{SdPlayerState::kIdle};
    std::atomic<bool> stop_requested_{false};
    std::string current_name_;
    mutable std::mutex name_mutex_;

    TaskHandle_t player_handle_ = nullptr;
    std::mutex playback_mutex_;   // chi 1 phien Play()/Stop() tai 1 thoi diem

  
    mutable std::mutex resume_mutex_;
    bool resume_pending_ = false;
    SdTrackEntry pending_resume_entry_;

    bool sd_mounted_ = false;
    bool mount_attempted_ = false;

    std::function<bool()> is_active_session_checker_;
};

#endif  // SDCARD_MUSIC_PLAYER_H