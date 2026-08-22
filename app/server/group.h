#ifndef GROUP_H
#define GROUP_H

#include <nlohmann/json.hpp>
#include "global.h"
#include "message.h"
#include "mysql_pool.h"
#include "redis_pool.h"
#include "router.h"
#include "server.h"

class GroupHandler {
   public:
    static void registerhandlers();

    // 创建群
    static void handle_create_group(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 解散群
    static void handle_dismiss_group(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 申请加入群
    static void handle_apply_join_group(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 查看入群申请（管理员/群主）
    static void handle_get_group_applies(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 审批入群申请（同意/拒绝）
    static void handle_review_join_apply(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 退出群
    static void handle_quit_group(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 查看我的群列表
    static void handle_get_my_groups(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 查看群成员列表
    static void handle_get_group_members(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 设置/取消管理员
    static void handle_set_admin(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 踢出成员
    static void handle_kick_member(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
    // 搜索群聊
    static void handle_search_group(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json);
};

#endif