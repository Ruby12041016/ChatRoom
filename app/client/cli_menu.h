#ifndef CLI_MENU
#define CLI_MENU

#include <set>

#include <termios.h>
#include "cli_account.h"
#include "cli_big_file.h"
#include "cli_chat.h"
#include "cli_friend.h"
#include "cli_group.h"
#include "cli_network.h"
#include "cli_session.h"
#include "global.h"

class Cli_Menu {
   public:
    Cli_Menu();
    void Run(int argc, char* argv[]);

   private:
    Cli_Network net_;
    Cli_Account account_;
    Cli_Chat chat_;
    Cli_Friend friend_;
    Cli_Group group_;
    Cli_BigFile big_file_;
    std::string server_ip_;

    // 通用操作同步等待标志
    bool op_done_ = false;
    bool op_success_ = true;  // 操作是否成功，默认true（错误时置false）

    // 推送显示优化
    std::string current_prompt_;            // 当前输入提示符
    bool push_buffered_ = false;            // 推送缓存总开关
    std::vector<std::string> push_buffer_;  // 推送消息缓存队列
    int push_count_ = 0;                    // 未缓冲模式下的推送计数
    int showed = 0;

    // 聊天会话隔离
    int chat_context_type_ = 0;       // 当前聊天类型：0=无, 1=私聊, 2=群聊
    uint64_t chat_context_id_ = 0;    // 当前聊天目标ID
    int other_msg_count_ = 0;         // 其他会话新消息计数
    uint64_t last_read_msg_id_ = 0;   // 当前聊天会话最后已读的消息ID
    uint64_t last_read_chat_id_ = 0;  // last_read_msg_id_对应的聊天ID
    uint64_t oldest_msg_id_ = 0;  // 当前会话已加载的最早消息ID（用于翻页加载更早消息）
    bool chat_show = false;

    // 内部工具函数
    void PrintPush(const std::string& content, bool is_show = true);
    void FlushPushBuffer();
    bool WaitForOperation(int timeout_sec = 5);  // 通用操作等待函数，超时返回false

    // 异步输入相关
    std::thread input_thread_;
    std::queue<std::string> input_queue_;
    std::mutex input_mutex_;
    std::condition_variable input_cv_;
    bool running_ = true;

    void InputThreadFunc();  // 输入线程主函数

    // 消息分发
    void ProcessMessages();

    // 菜单
    void ShowLoginMenu();
    void ShowMainMenu();

    // 工具函数
    std::string readline(const std::string& prompt);
    int read_int(const std::string& prompt);

    std::string read_pwd(const std::string& prompt);
    std::unordered_map<uint64_t, std::string> known_users_;   // 用户ID->用户名
    std::unordered_map<std::string, uint64_t> uname_to_id;    // 用户名->用户ID
    std::unordered_map<uint64_t, std::string> known_groups_;  // 群ID-> 群名
    std::unordered_map<std::string, uint64_t> gname_to_id;    // 群名->群ID

    std::set<uint64_t> friend_ids_;    // 当前用户的好友ID集合
    std::set<uint64_t> my_group_ids_;  // 当前用户所在群的ID集合

    void ChatMap(int chat_type, uint64_t target_id);
    std::vector<std::string> message_type{"未知", "文本", "文件"};
    std::vector<std::string> member_type{"群主", "管理员", "群员"};
};

#endif