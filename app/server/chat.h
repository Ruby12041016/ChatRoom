#ifndef CHAT_H
#define CHAT_H

#include <fstream>
#include <nlohmann/json.hpp>
#include "global.h"
#include "message.h"
#include "mysql_pool.h"
#include "redis_pool.h"
#include "router.h"
#include "server.h"
#include "utils.h"

class HistoryHandler {
   public:
    static void registerhandlers();

    // 发送私聊消息
    static void handle_private_chat(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 发送群聊消息
    static void handle_group_chat(std::shared_ptr<TCPConnection> conn,  const nlohmann::json& json);
    // 查询历史消息
    static void handle_get_history(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 拉取离线私聊消息并推送
    static void pushOfflinePrivateMsgs(std::shared_ptr<TCPConnection> conn, uint64_t user_id);
    // 拉取离线群聊通知并推送
    static void pushOfflineGroupMsgs(std::shared_ptr<TCPConnection> conn, uint64_t user_id);
    // 拉取用户所在群的最近消息（上线通知）
    static void pushRecentGroupMsgs(std::shared_ptr<TCPConnection> conn, uint64_t user_id);
    // 处理文件消息
    static void handle_file_msg(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 处理文件下载请求
    static void handle_download_file(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);

    static std::string format_time_now() {
        auto now = std::chrono::system_clock::now();
        time_t t = std::chrono::system_clock::to_time_t(now);
        std::tm* tm = std::localtime(&t);
        std::ostringstream oss;
        oss << std::put_time(tm, "%Y-%m-%d %H:%M:%S");
        return oss.str();
    }
};

#endif