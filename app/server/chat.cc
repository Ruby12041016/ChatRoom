#include "chat.h"

void HistoryHandler::registerhandlers() {
    auto& router = MessageRouter::instance();
    router.registerHandler(MSG_PRIVATE_CHAT, handle_private_chat);
    router.registerHandler(MSG_GROUP_CHAT, handle_group_chat);
    router.registerHandler(MSG_GET_HISTORY_MSG, handle_get_history);
    router.registerHandler(MSG_FILE_MSG, handle_file_msg);
    router.registerHandler(MSG_DOWNLOAD_FILE, handle_download_file);
}

// 发送私聊消息
void HistoryHandler::handle_private_chat(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_PRIVATE_CHAT, "请先登录");
        return;
    }

    uint64_t sender_id = conn->getUserId();
    auto data = json.value("data", nlohmann::json{});
    uint64_t receiver_id = data.value("receiver_id", 0ull);
    std::string message = data.value("message", "");
    int message_type = data.value("message_type", 0);

    if (receiver_id == 0) {
        send_error(conn, ERR_PARAM, MSG_PRIVATE_CHAT, "参数错误");
        return;
    }
    if (message.empty()) {
        return;
    }

    MysqlPool& pool = MysqlPool::GetInstance();

    QueryResult check_friend = pool.Query(
        "SELECT status FROM friends WHERE (user_id=? AND friend_id=?) OR "
        "(user_id=? AND friend_id=?)",
        {std::to_string(sender_id), std::to_string(receiver_id),
         std::to_string(receiver_id), std::to_string(sender_id)});
    if (check_friend.rows.empty()) {
        send_error(conn, ERR_FRIEND_NOT_EXIST, MSG_PRIVATE_CHAT, "对方不是好友");
        return;
    }

    QueryResult block_check = pool.Query(
        "SELECT status FROM friends WHERE user_id = ? AND friend_id = ?",
        {std::to_string(receiver_id), std::to_string(sender_id)});
    if (!block_check.rows.empty() && block_check.rows[0][0] == "1") {
        send_error(conn, ERR_BLACKED, MSG_PRIVATE_CHAT, "对方已屏蔽你的消息");
        return;
    }

    std::string insert_sql =
        "INSERT INTO messages (chat_type, from_id, to_id, content_type, "
        "content, status) VALUES (1, ?, ?, 1, ?, 0)";
    uint64_t message_id = pool.ExecuteInsert(
        insert_sql,
        {std::to_string(sender_id), std::to_string(receiver_id), message});
    if (message_id == 0) {
        send_error(conn, ERR_SERVER_BUSY, MSG_PRIVATE_CHAT, "发送消息失败");
        return;
    }

    nlohmann::json response;
    response["msg_id"] = MSG_PRIVATE_CHAT;
    response["data"] = {
        {"message_id", message_id}, {"from_id", sender_id},
        {"to_id", receiver_id},     {"content_type", message_type},
        {"content", message},       {"time", format_time_now()}};

    auto target_conn =
        ConnectionManager::GetInstance().GetConnByUserId(receiver_id);
    if (target_conn && target_conn->isLogin()) {
        send_ok(target_conn, PUSH_PRIVATE_MSG, response["data"]);
        // 更新消息状态为已读
        std::string update_sql = "UPDATE messages SET status=1 WHERE id=?";
        pool.ExecuteStmt(update_sql, {std::to_string(message_id)});
    }

    send_ok(conn, MSG_PRIVATE_CHAT, response["data"]);
}

// 发送群聊消息
void HistoryHandler::handle_group_chat(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_GROUP_CHAT, "请先登录");
        return;
    }

    uint64_t sender_id = conn->getUserId();
    auto data = json.value("data", nlohmann::json{});
    uint64_t group_id = data.value("group_id", 0ull);
    std::string message = data.value("message", "");
    int message_type = data.value("message_type", 0);

    if (group_id == 0) {
        send_error(conn, ERR_PARAM, MSG_GROUP_CHAT, "参数错误");
        return;
    }
    if (message.empty()) {
        return;
    }

    MysqlPool& pool = MysqlPool::GetInstance();

    std::string check_member_sql =
        "SELECT status FROM group_member WHERE group_id=? AND member_id=?";
    QueryResult check_member =
        pool.Query(check_member_sql,
                   {std::to_string(group_id), std::to_string(sender_id)});
    if (check_member.rows.empty()) {
        send_error(conn, ERR_NOT_IN_GROUP, MSG_GROUP_CHAT,
                   "你不在该群中，无法发送消息");
        return;
    }

    std::string insert_sql =
        "INSERT INTO messages (chat_type, from_id, to_id, content_type, "
        "content, status) VALUES (2, ?, ?, 1, ?, 0)";
    uint64_t message_id = pool.ExecuteInsert(
        insert_sql,
        {std::to_string(sender_id), std::to_string(group_id), message});
    if (message_id == 0) {
        send_error(conn, ERR_SERVER_BUSY, MSG_GROUP_CHAT, "发送消息失败");
        return;
    }

    nlohmann::json response;
    response["msg_id"] = MSG_GROUP_CHAT;
    response["data"] = {
        {"message_id", message_id}, {"from_id", sender_id},
        {"to_id", group_id},        {"content_type", message_type},
        {"content", message},       {"time", format_time_now()}};

    std::string get_members_sql =
        "SELECT member_id FROM group_member WHERE group_id=? AND member_id!=?";
    QueryResult members = pool.Query(
        get_members_sql, {std::to_string(group_id), std::to_string(sender_id)});
    ConnectionManager& conn_mgr = ConnectionManager::GetInstance();
    for (const auto& row : members.rows) {
        uint64_t member_id = std::stoull(row[0]);
        auto member_conn = conn_mgr.GetConnByUserId(member_id);
        if (member_conn && member_conn->isLogin()) {
            send_ok(member_conn, PUSH_GROUP_MSG, response["data"]);
        }
    }
    send_ok(conn, MSG_GROUP_CHAT, response["data"]);
}

// 查询历史消息
void HistoryHandler::handle_get_history(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_GET_HISTORY_MSG, "请先登录");
        return;
    }

    uint64_t user_id = conn->getUserId();
    auto data = json.value("data", nlohmann::json{});
    int chat_type = data.value("chat_type", 0);
    uint64_t target_id = data.value("target_id", 0ull);
    int limit = data.value("limit", 50);
    if (limit > MAX_HISTORY_LIMIT) {
        limit = MAX_HISTORY_LIMIT;
    }
    uint64_t before_msg_id = data.value("before_msg_id", 0ull);
    uint64_t after_msg_id = data.value("after_msg_id", 0ull);

    if (chat_type != 1 && chat_type != 2) {
        send_error(conn, ERR_PARAM, MSG_GET_HISTORY_MSG, "参数错误");
        return;
    }

    MysqlPool& pool = MysqlPool::GetInstance();

    if (chat_type == 1) {
        std::string check_friend_sql =
            "SELECT status FROM friends WHERE (user_id=? AND friend_id=?) OR "
            "(user_id=? AND friend_id=?)";
        QueryResult check_friend =
            pool.Query(check_friend_sql,
                       {std::to_string(user_id), std::to_string(target_id),
                        std::to_string(target_id), std::to_string(user_id)});
        if (check_friend.rows.empty()) {
            send_error(conn, ERR_FRIEND_NOT_EXIST, MSG_GET_HISTORY_MSG,
                       "对方不是好友");
            return;
        }
    } else if (chat_type == 2) {
        std::string check_member_sql =
            "SELECT status FROM group_member WHERE group_id=? AND member_id=?";
        QueryResult check_member =
            pool.Query(check_member_sql,
                       {std::to_string(target_id), std::to_string(user_id)});
        if (check_member.rows.empty()) {
            send_error(conn, ERR_NOT_IN_GROUP, MSG_GET_HISTORY_MSG,
                       "你不在该群中，无法获取消息");
            return;
        }
        // 更新已读ID
        pool.ExecuteStmt(
            "UPDATE group_member SET last_read_id = "
            "(SELECT COALESCE(MAX(id), last_read_id) FROM messages "
            " WHERE chat_type=2 AND to_id=?) "
            "WHERE group_id = ? AND member_id = ?",
            {std::to_string(target_id), std::to_string(target_id),
             std::to_string(user_id)});
    }

    std::string query_sql;
    std::vector<std::string> params;
    if (chat_type == 1) {
        // 私聊：双向
        query_sql =
            "SELECT id, from_id, to_id, content_type, content, status, "
            "created_at "
            "FROM messages WHERE chat_type=1 AND ((from_id=? AND to_id=?) OR "
            "(from_id=? AND to_id=?)) ";
        params = {std::to_string(user_id), std::to_string(target_id),
                  std::to_string(target_id), std::to_string(user_id)};
    } else {
        // 群聊：to_id 为群ID
        query_sql =
            "SELECT id, from_id, to_id, content_type, content, status, "
            "created_at "
            "FROM messages WHERE chat_type=2 AND to_id=? ";
        params = {std::to_string(target_id)};
    }

    if (before_msg_id > 0) {
        query_sql += " AND id < ? ";
        params.push_back(std::to_string(before_msg_id));
    }
    if (after_msg_id > 0) {
        query_sql += " AND id > ? ";
        params.push_back(std::to_string(after_msg_id));
    }
    if (after_msg_id > 0) {
        query_sql += " ORDER BY id ASC LIMIT ?";
    } else {
        query_sql += " ORDER BY id DESC LIMIT ?";
    }
    params.push_back(std::to_string(limit));

    QueryResult query_result = pool.Query(query_sql, params);

    nlohmann::json messages = nlohmann::json::array();
    for (auto it = query_result.rows.rbegin(); it != query_result.rows.rend();
         ++it) {
        nlohmann::json msg;
        msg["message_id"] = std::stoull((*it)[0]);
        msg["from_id"] = std::stoull((*it)[1]);
        msg["to_id"] = std::stoull((*it)[2]);
        msg["content_type"] = std::stoi((*it)[3]);
        msg["content"] = (*it)[4];
        msg["status"] = std::stoi((*it)[5]);
        msg["time"] = (*it)[6];
        messages.push_back(msg);
    }

    nlohmann::json response;
    response["msg_id"] = MSG_GET_HISTORY_MSG;
    response["data"] = messages;
    send_ok(conn, MSG_GET_HISTORY_MSG, response["data"]);
}

// 拉取离线私聊消息并推送
void HistoryHandler::pushOfflinePrivateMsgs(std::shared_ptr<TCPConnection> conn, uint64_t user_id) {
    MysqlPool& pool = MysqlPool::GetInstance();
    std::string query_sql =
        "SELECT id, from_id, to_id, content_type, content, status, created_at "
        "FROM messages WHERE chat_type=1 AND to_id=? AND status=0 ORDER BY id "
        "ASC";
    QueryResult query_result = pool.Query(query_sql, {std::to_string(user_id)});

    for (const auto& row : query_result.rows) {
        nlohmann::json msg;
        msg["message_id"] = std::stoull(row[0]);
        msg["from_id"] = std::stoull(row[1]);
        msg["to_id"] = std::stoull(row[2]);
        msg["content_type"] = std::stoi(row[3]);
        msg["content"] = row[4];
        msg["status"] = std::stoi(row[5]);
        msg["time"] = row[6];
        send_ok(conn, PUSH_PRIVATE_MSG, msg);
        pool.ExecuteStmt("UPDATE messages SET status = 1 WHERE id = ?",
                         {row[0]});
    }
}

// 拉取离线群聊通知并推送
void HistoryHandler::pushOfflineGroupMsgs(std::shared_ptr<TCPConnection> conn, uint64_t user_id) {
    MysqlPool& pool = MysqlPool::GetInstance();

    // 推送群聊通知（管理员）
    QueryResult notifs = pool.Query(
        "SELECT gn.id, "
        "gn.apply_id, "
        "gn.type, "
        "ga.group_id, "
        "ga.apply_id, "
        "ga.handle_id, "
        "ga.message, "
        "ga.status "
        "FROM group_notifications gn "
        "JOIN group_apply ga ON gn.apply_id = ga.id "
        "WHERE gn.receiver_id = ? "
        "AND gn.is_read = 0",
        {std::to_string(user_id)});

    for (const auto& row : notifs.rows) {
        uint64_t notif_id = std::stoull(row[0]);
        uint64_t apply_id = std::stoull(row[1]);
        int type = std::stoi(row[2]);
        uint64_t group_id = std::stoull(row[3]);
        uint64_t applicant_id = std::stoull(row[4]);

        nlohmann::json push;
        if (type == 1) {  // 新申请
            push["type"] = "new_group_apply";
            push["apply_id"] = apply_id;
            push["group_id"] = group_id;
            push["apply_user_id"] = applicant_id;
            push["message"] = row[6];
        } else if (type == 2) {  // 处理结果
            int apply_status = std::stoi(row[7]);
            push["type"] = "group_apply_result";
            push["apply_id"] = apply_id;
            push["group_id"] = group_id;
            if (!row[5].empty()) {
                push["handle_id"] = std::stoull(row[5]);
            }
            push["result"] = apply_status == 1 ? "agreed" : "refused";
        } else {
            LOG(WARNING) << "未知群通知类型: " << type
                         << ", notification_id=" << notif_id;

            continue;
        }
        send_ok(conn, PUSH_OFFLINE_NOTICE, push);
        pool.ExecuteStmt(
            "UPDATE group_notifications SET is_read = 1 WHERE id = ?",
            {std::to_string(notif_id)});
    }

    // 推送申请人的审批结果（is_pushed=0 的记录）
    QueryResult my_applies = pool.Query(
        "SELECT ga.id, ga.group_id, ga.status "
        "FROM group_apply ga "
        "WHERE ga.apply_id = ? AND ga.status IN (1, 2) AND ga.is_pushed = 0",
        {std::to_string(user_id)});

    for (const auto& row : my_applies.rows) {
        uint64_t apply_id = std::stoull(row[0]);
        uint64_t group_id = std::stoull(row[1]);
        int status = std::stoi(row[2]);

        nlohmann::json push;
        push["type"] = status == 1 ? "group_agree" : "group_refuse";
        push["group_id"] = group_id;
        send_ok(conn, PUSH_OFFLINE_NOTICE, push);

        pool.ExecuteStmt("UPDATE group_apply SET is_pushed = 1 WHERE id = ?",
                         {std::to_string(apply_id)});
    }
}

// 拉取用户所在群的最近消息（上线通知）
void HistoryHandler::pushRecentGroupMsgs(std::shared_ptr<TCPConnection> conn, uint64_t user_id) {
    MysqlPool& pool = MysqlPool::GetInstance();

    // 查询用户在每个群的 last_read_id
    std::string member_sql =
        "SELECT group_id, COALESCE(last_read_id, 0) FROM group_member "
        "WHERE member_id = ?";
    QueryResult members = pool.Query(member_sql, {std::to_string(user_id)});

    for (const auto& row : members.rows) {
        uint64_t gid = std::stoull(row[0]);
        uint64_t last_read = std::stoull(row[1]);

        // 只查 id > last_read 的消息（未读），不推自己发的
        std::string query_sql =
            "SELECT m.id, m.from_id, m.to_id, m.content_type, m.content, "
            "m.status, m.created_at "
            "FROM messages m "
            "WHERE m.chat_type=2 AND m.to_id=? AND m.from_id!=? "
            "AND m.id > ? "
            "ORDER BY m.id ASC LIMIT 50";
        QueryResult query_result =
            pool.Query(query_sql, {std::to_string(gid), std::to_string(user_id),
                                   std::to_string(last_read)});

        uint64_t max_msg_id = 0;
        for (const auto& row_ : query_result.rows) {
            uint64_t msg_id = std::stoull(row_[0]);
            nlohmann::json msg;
            msg["message_id"] = msg_id;
            msg["from_id"] = std::stoull(row_[1]);
            msg["to_id"] = std::stoull(row_[2]);
            msg["content_type"] = std::stoi(row_[3]);
            msg["content"] = row_[4];
            msg["status"] = std::stoi(row_[5]);
            msg["time"] = row_[6];
            send_ok(conn, PUSH_GROUP_MSG, msg);
            if (msg_id > max_msg_id)
                max_msg_id = msg_id;
        }

        // 更新 last_read_id
        if (max_msg_id > 0) {
            pool.ExecuteStmt(
                "UPDATE group_member SET last_read_id = ? "
                "WHERE group_id = ? AND member_id = ?",
                {std::to_string(max_msg_id), std::to_string(gid),
                 std::to_string(user_id)});
        }
    }
}

// 处理文件消息
void HistoryHandler::handle_file_msg(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_FILE_MSG, "请先登录");
        return;
    }

    uint64_t sender_id = conn->getUserId();
    auto data = json.value("data", nlohmann::json{});
    uint64_t receiver_id = data.value("receiver_id", 0ull);
    std::string file_name = data.value("file_name", "");
    uint64_t group_id = data.value("group_id", 0ull);
    uint64_t file_size = data.value("file_size", 0ull);

    if (file_name.empty() || (receiver_id == 0 && group_id == 0)) {
        send_error(conn, ERR_PARAM, MSG_FILE_MSG, "参数错误");
        return;
    }
    std::string file_path = "./files/" + file_name;

    MysqlPool& pool = MysqlPool::GetInstance();
    int chat_type = receiver_id == 0 ? 2 : 1;  // 文件消息类型，1: 私聊，2: 群聊
    uint64_t target_id = receiver_id == 0 ? group_id : receiver_id;

    if (chat_type == 1) {
        QueryResult check_friend = pool.Query(
            "SELECT status FROM friends WHERE (user_id=? AND friend_id=?) OR "
            "(user_id=? AND friend_id=?)",
            {std::to_string(sender_id), std::to_string(receiver_id),
             std::to_string(receiver_id), std::to_string(sender_id)});
        if (check_friend.rows.empty()) {
            send_error(conn, ERR_FRIEND_NOT_EXIST, MSG_FILE_MSG,
                       "对方不是好友");
            return;
        }

        QueryResult block_check = pool.Query(
            "SELECT status FROM friends WHERE user_id = ? AND friend_id = ?",
            {std::to_string(receiver_id), std::to_string(sender_id)});
        if (!block_check.rows.empty() && block_check.rows[0][0] == "1") {
            send_error(conn, ERR_BLACKED, MSG_FILE_MSG, "对方已屏蔽你的消息");
            return;
        }
    }
    std::string insert_sql =
        "INSERT INTO messages (chat_type, from_id, to_id, content_type, "
        "content, file_name, file_size, status) "
        "VALUES (?, ?, ?, 2, ?, ?, ?, 0)";
    uint64_t message_id = pool.ExecuteInsert(
        insert_sql, {std::to_string(chat_type), std::to_string(sender_id),
                     std::to_string(target_id), file_path, file_name,
                     std::to_string(file_size)});
    if (message_id == 0) {
        send_error(conn, ERR_SERVER_BUSY, MSG_FILE_MSG, "发送文件消息失败");
        return;
    }

    nlohmann::json response;
    response["msg_id"] = MSG_FILE_MSG;
    response["data"] = {
        {"message_id", message_id}, {"from_id", sender_id},
        {"to_id", target_id},       {"content_type", 2},
        {"content", file_path},     {"file_name", file_name},
        {"file_size", file_size},   {"time", format_time_now()}};

    ConnectionManager& conn_mgr = ConnectionManager::GetInstance();
    if (chat_type == 1) {
        auto target_conn = conn_mgr.GetConnByUserId(receiver_id);
        if (target_conn && target_conn->isLogin()) {
            send_ok(target_conn, PUSH_PRIVATE_MSG, response["data"]);
            // 更新消息状态为已读
            std::string update_sql = "UPDATE messages SET status=1 WHERE id=?";
            pool.ExecuteStmt(update_sql, {std::to_string(message_id)});
        }
    } else {
        std::string get_members_sql =
            "SELECT member_id FROM group_member WHERE group_id=? AND "
            "member_id!=?";
        QueryResult members =
            pool.Query(get_members_sql,
                       {std::to_string(group_id), std::to_string(sender_id)});
        for (const auto& row : members.rows) {
            uint64_t member_id = std::stoull(row[0]);
            auto member_conn = conn_mgr.GetConnByUserId(member_id);
            if (member_conn && member_conn->isLogin()) {
                send_ok(member_conn, PUSH_GROUP_MSG, response["data"]);
            }
        }
    }
    send_ok(conn, MSG_FILE_MSG,
            {{"msg_id", message_id}, {"status", "success"}});
}

// 处理文件下载请求
void HistoryHandler::handle_download_file(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_DOWNLOAD_FILE, "请先登录");
        return;
    }

    uint64_t user_id = conn->getUserId();
    auto data = json.value("data", nlohmann::json{});
    uint64_t message_id = data.value("message_id", 0ull);

    if (message_id == 0) {
        send_error(conn, ERR_PARAM, MSG_DOWNLOAD_FILE, "参数错误");
        return;
    }

    MysqlPool& pool = MysqlPool::GetInstance();
    std::string query_sql =
        "SELECT chat_type, from_id, to_id, content_type, content, file_name, "
        "file_size FROM messages "
        "WHERE id=?";
    QueryResult query_result = pool.Query(query_sql, {std::to_string(message_id)});

    if (query_result.rows.empty()) {
        send_error(conn, ERR_PARAM, MSG_DOWNLOAD_FILE, "消息不存在");
        return;
    }

    const auto& row = query_result.rows[0];
    int chat_type, content_type;
    uint64_t from_id, to_id;
    try {
        chat_type = std::stoi(row[0]);
        from_id = std::stoull(row[1]);
        to_id = std::stoull(row[2]);
        content_type = std::stoi(row[3]);
    } catch (const std::exception& e) {
        LOG(ERROR) << "字段解析失败: " << e.what();
        send_error(conn, ERR_SERVER_BUSY, MSG_DOWNLOAD_FILE, "数据异常");
        return;
    }
    std::string file_path = row[4];
    std::string file_name = row[5];

    if (content_type != 2) {
        send_error(conn, ERR_PARAM, MSG_DOWNLOAD_FILE, "该消息不是文件消息");
        return;
    }

    uint64_t file_size;
    try {
        file_size = std::stoull(row[6]);
    } catch (const std::exception& e) {
        LOG(ERROR) << "文件大小解析失败: " << e.what();
        send_error(conn, ERR_SERVER_BUSY, MSG_DOWNLOAD_FILE, "数据异常");
        return;
    }

    // 私聊仅双方可下载，群聊仅群成员可下载
    if (chat_type == 1 && to_id != user_id && from_id != user_id) {
        send_error(conn, ERR_PERMISSION, MSG_DOWNLOAD_FILE, "你没有权限下载该文件");
        return;
    } else if (chat_type == 2) {
        std::string check_member_sql = "SELECT status FROM group_member WHERE group_id=? AND member_id=?";
        QueryResult check_member = pool.Query(
            check_member_sql, {std::to_string(to_id), std::to_string(user_id)});
        if (check_member.rows.empty()) {
            send_error(conn, ERR_NOT_IN_GROUP, MSG_DOWNLOAD_FILE, "你不在该群中，无法下载文件");
            return;
        }
    }

    // 检查服务器上的文件是否存在
    if (!std::filesystem::exists(file_path)) {
        send_error(conn, ERR_SERVER_BUSY, MSG_DOWNLOAD_FILE, "文件不存在或已被删除");
        return;
    }

    // 返回文件信息
    nlohmann::json response;
    response["msg_id"] = MSG_DOWNLOAD_FILE;
    response["data"] = {{"message_id", message_id},
                        {"file_name", file_name},
                        {"file_size", file_size}};
    send_ok(conn, MSG_DOWNLOAD_FILE, response["data"]);
}