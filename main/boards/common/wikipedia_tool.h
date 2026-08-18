#ifndef WIKIPEDIA_TOOL_H
#define WIKIPEDIA_TOOL_H

#include <string>

// TEST ONLY: module này gọi thẳng Wikipedia REST API từ thiết bị, không qua
// backend lọc/kiểm duyệt nội dung. Chỉ dùng để test kỹ thuật (đọc thử dữ liệu,
// đo độ trễ, kiểm tra pipeline HTTP trên ESP32).
// KHÔNG bật tool này trong bản dùng cho trẻ em / chế độ giáo dục không giám sát.
class WikipediaTool {
public:
    static WikipediaTool& GetInstance() {
        static WikipediaTool instance;
        return instance;
    }

    // Đăng ký tool "self.wikipedia.search" vào McpServer.
    // Gọi trong McpServer::AddCommonTools(), tương tự WledController/StoryPlayer.
    void Initialize();

private:
    WikipediaTool() = default;
    ~WikipediaTool() = default;
    WikipediaTool(const WikipediaTool&) = delete;
    WikipediaTool& operator=(const WikipediaTool&) = delete;

    // Gọi REST API tóm tắt của Wikipedia tiếng Việt cho một chủ đề.
    // Trả về chuỗi rỗng nếu lỗi mạng, không tìm thấy bài, hoặc vượt giới hạn kích thước.
    std::string FetchSummary(const std::string& title);

    static std::string UrlEncode(const std::string& input);
};

#endif // WIKIPEDIA_TOOL_H
