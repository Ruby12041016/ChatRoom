#ifndef FRIEND_H
#define FRIEND_H

#include <nlohmann/json.hpp>
#include "global.h"
#include "message.h"
#include "mysql_pool.h"
#include "redis_pool.h"
#include "router.h"
#include "server.h"

class FriendHandler {
   public:
    static void registerhandlers();
    // 搜索用户
    static void handle_search_user(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 发起好友申请
    static void handle_add_friend(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 同意好友申请
    static void handle_agree_friend(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 拒绝好友申请
    static void handle_refuse_friend(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 删除好友
    static void handle_del_friend(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 获取好友列表
    static void handle_get_friend_list(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 获取好友申请列表
    static void handle_get_apply_list(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 屏蔽/取消屏蔽好友消息
    static void handle_set_friend_mute(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 设置好友备注
    static void handle_set_friend_remark(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 推送离线好友申请
    static void pushOfflineFriendMsgs(std::shared_ptr<TCPConnection> conn, uint64_t user_id);
};

#endif