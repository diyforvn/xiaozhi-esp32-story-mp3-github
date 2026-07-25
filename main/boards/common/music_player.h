#ifndef MUSIC_PLAYER_H
#define MUSIC_PLAYER_H

// ============================================================================
// MusicPlayer
// Bai hat gio la MP3 nam TRONG CUNG repo GitHub voi
// truyen ke (thu muc "songs/", entry co "category":"song" trong index.json
// dung chung voi StoryPlayer). Lop nay chi con la adapter mong: dang ky MCP
// tool self.music.* va goi vao MediaLibrary::GetInstance(), loc theo
// category == "song".
//
// Toan bo co che tai + cache + stream MP3 nam trong media_library.h/.cc.
// ============================================================================

class MusicPlayer {
public:
    static MusicPlayer& GetInstance();

    void Initialize();

private:
    MusicPlayer() = default;
    MusicPlayer(const MusicPlayer&) = delete;
    MusicPlayer& operator=(const MusicPlayer&) = delete;

    void RegisterMcpTools();
};

#endif  // MUSIC_PLAYER_H