#ifndef STORY_PLAYER_H
#define STORY_PLAYER_H



class StoryPlayer {
public:
    static StoryPlayer& GetInstance();

    void Initialize();

private:
    StoryPlayer() = default;
    StoryPlayer(const StoryPlayer&) = delete;
    StoryPlayer& operator=(const StoryPlayer&) = delete;

    void RegisterMcpTools();
};

#endif  // STORY_PLAYER_H