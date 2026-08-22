#include "friend.h"
#include "connection_manage.h"

void FriendHandler::registerhandlers() {
    auto& router = MessageRouter::instance();
    router.registerHandler(MSG_SEARCH_USER, handle_search_user);
    router.registerHandler(MSG_ADD_FRIEND, handle_add_friend);
    router.registerHandler(MSG_AGREE_FRIEND, handle_agree_friend);
    router.registerHandler(MSG_REFUSE_FRIEND, handle_refuse_friend);
    router.registerHandler(MSG_DEL_FRIEND, handle_del_friend);
    router.registerHandler(MSG_GET_FRIEND_LIST, handle_get_friend_list);
    router.registerHandler(MSG_GET_APPLY_LIST, handle_get_apply_list);
    router.registerHandler(MSG_SET_FRIEND_MUTE, handle_set_friend_mute);
    router.registerHandler(MSG_SET_FRIEND_REMARK, handle_set_friend_remark);
}

// 搜索用户
void FriendHandler::handle_search_user(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_SEARCH_USER, "请先登录");
        return;
    }

    auto data = json.value("data", nlohmann::json{});
    std::string keyword = data.value("keyword", "");
    if (keyword.empty()) {
        send_error(conn, ERR_PARAM, MSG_SEARCH_USER, "搜索关键词不能为空");
        return;
    }

    std::string sql = "SELECT id, username, email, phone FROM users WHERE username LIKE ? LIMIT 20";

    // 模糊匹配需要带%
    std::string like_keyword = "%" + keyword + "%";

    MysqlPool& pool = MysqlPool::GetInstance();
    QueryResult result = pool.Query(sql, {like_keyword});

    nlohmann::json user_list = nlohmann::json::array();
    RedisPool& redis = RedisPool::GetInstance();

    for (const auto& row : result.rows) {
        nlohmann::json user;
        user["user_id"] = std::stoull(row[0]);
        user["username"] = row[1];
        user["email"] = row[2];
        // 查询 Redis 在线状态
        user["online"] = redis.set_ismember("online_users", row[0]);
        user_list.push_back(user);
    }

    nlohmann::json resp;
    resp["users"] = user_list;
    send_ok(conn, MSG_SEARCH_USER, resp);
}

// 发起好友申请
void FriendHandler::handle_add_friend(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_ADD_FRIEND, "请先登录");
        return;
    }

    uint64_t apply_id = conn->getUserId();
    auto data = json.value("data", nlohmann::json{});
    uint64_t user_id = data.value("user_id", 0ull);
    std::string message = data.value("message", "");

    if (user_id == 0 || apply_id == user_id) {
        send_error(conn, ERR_PARAM, MSG_ADD_FRIEND, "无效的目标用户");
        return;
    }

    MysqlPool& pool = MysqlPool::GetInstance();
    std::string exist_sql = "SELECT id FROM users WHERE id = ?";
    int exists = pool.ExecuteExist(exist_sql, {std::to_string(user_id)});
    if (exists <= 0) {
        send_error(conn, ERR_ACCOUNT_NOT_EXIST, MSG_ADD_FRIEND, "用户不存在");
        return;
    }

    std::string sql = "SELECT * FROM friends WHERE user_id=? AND friend_id=?";
    QueryResult result =
        pool.Query(sql, {std::to_string(user_id), std::to_string(apply_id)});
    if (!result.rows.empty()) {
        send_error(conn, ERR_PARAM, MSG_ADD_FRIEND, "已经是好友了");
        return;
    }

    std::string sql_check =
        "SELECT 1 FROM friend_apply WHERE apply_id=? AND owner_id=? AND "
        "status=0";

    int applieds = pool.ExecuteExist(
        sql_check, {std::to_string(apply_id), std::to_string(user_id)});
    if (applieds > 0) {
        send_error(conn, ERR_PARAM, MSG_ADD_FRIEND, "您已经发送过好友申请，请等待对方处理");
        return;
    }

    // 先检查对方是否在线，决定初始推送状态（在线=1，离线=0）
    bool is_online = false;
    auto target_conn =
        ConnectionManager::GetInstance().GetConnByUserId(user_id);
    if (target_conn && target_conn->isLogin())
        is_online = true;

    // 插入申请记录
    std::string insert_sql =
        "INSERT INTO friend_apply (apply_id, owner_id, message, "
        "status,is_pushed) VALUES (?, ?, ?, 0,?)";
    uint64_t request_id = pool.ExecuteInsert(
        insert_sql, {std::to_string(apply_id), std::to_string(user_id), message,
                     is_online ? "1" : "0"});

    if (request_id == 0) {
        send_error(conn, ERR_SERVER_BUSY, MSG_ADD_FRIEND, "发送好友申请失败");
        return;
    }

    if (is_online) {
        // 在线：直接推，因为插入时 is_pushed 已经是 1 了，不需要再 UPDATE
        auto apply_user = pool.Query("SELECT username FROM users WHERE id = ?", {std::to_string(apply_id)});
        nlohmann::json notify;
        notify["type"] = "new_friend_request";
        notify["request_id"] = request_id;
        notify["apply_id"] = apply_id;
        notify["message"] = message;
        if (!apply_user.rows.empty()) {
            notify["apply_name"] = apply_user.rows[0][0];
        }
        send_ok(target_conn, PUSH_FRIEND_APPLY, notify);
        LOG(INFO) << "已实时推送申请给目标用户 " << user_id;
    } else {
        // 离线：什么也不用做，保持 is_pushed=0，等他登录时自动推
        LOG(INFO) << "目标用户 " << user_id << " 不在线，等待上线自动推送";
    }
    nlohmann::json resp;
    resp["request_id"] = request_id;
    send_ok(conn, MSG_ADD_FRIEND, resp);
}

// 同意好友申请
void FriendHandler::handle_agree_friend(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_AGREE_FRIEND, "请先登录");
        return;
    }

    uint64_t agree_id = conn->getUserId();
    auto data = json.value("data", nlohmann::json::object());
    uint64_t request_id = data.value("user_id", 0ull);

    if (request_id == 0) {
        send_error(conn, ERR_PARAM, MSG_AGREE_FRIEND, "无效的申请ID");
        return;
    }

    MysqlPool& pool = MysqlPool::GetInstance();
    MysqlConnGuard guard(3000);
    MYSQL* conn_mysql = guard.Get();
    if (!conn_mysql) {
        send_error(conn, ERR_SERVER_BUSY, MSG_AGREE_FRIEND, "服务器繁忙");
        return;
    }

    TransactionGuard tx(conn_mysql);  // 开启事务
    std::string que_sql =
        "SELECT apply_id FROM friend_apply WHERE id = ? AND owner_id = ? "
        "AND status = 0";
    QueryResult que_result =
        pool.Query(conn_mysql, que_sql,
                   {std::to_string(request_id), std::to_string(agree_id)});
    if (que_result.rows.empty()) {
        send_error(conn, ERR_PARAM, MSG_AGREE_FRIEND, "申请不存在或已处理");
        return;  // 离开作用域，TransactionGuard 自动 ROLLBACK
    }

    uint64_t apply_id = std::stoull(que_result.rows[0][0]);  // 获取申请人id

    std::string update_sql = "UPDATE friend_apply SET status = 1 WHERE id = ?";
    int answer =
        pool.ExecuteStmt(conn_mysql, update_sql, {std::to_string(request_id)});
    if (answer < 0) {
        send_error(conn, ERR_PARAM, MSG_AGREE_FRIEND, "更新申请状态失败");
        return;
    }

    std::string add_friend =
        "INSERT INTO friends (user_id, friend_id) VALUES (?, ?), (?, ?)";
    int add_answer =
        pool.ExecuteStmt(conn_mysql, add_friend,
                         {std::to_string(apply_id), std::to_string(agree_id),
                          std::to_string(agree_id), std::to_string(apply_id)});
   
    if (add_answer < 2) {
        send_error(conn, ERR_PARAM, MSG_AGREE_FRIEND, "添加朋友失败");
        return;
    }

    if (!tx.Commit()) {
        LOG(ERROR) << "事务提交失败";
        send_error(conn, ERR_PARAM, MSG_AGREE_FRIEND, "事务提交失败");
        return;
    }

    nlohmann::json notice_data;
    notice_data["type"] = "friend_agree";
    notice_data["user_id"] = agree_id;

    auto agree_user = pool.Query("SELECT username FROM users WHERE id = ?",
                                 {std::to_string(agree_id)});
    if (!agree_user.rows.empty()) {
        notice_data["user_name"] = agree_user.rows[0][0];
    }

    bool is_online = false;
    auto target_conn =
        ConnectionManager::GetInstance().GetConnByUserId(apply_id);
    if (target_conn && target_conn->isLogin())
        is_online = true;
    if (is_online) {
        // 在线，直接推送
        send_ok(target_conn, PUSH_FRIEND_AGREE, notice_data);
        // 标记推送完成，下次上线不再重复拉取
        pool.ExecuteStmt("UPDATE friend_apply SET is_pushed = 1 WHERE id = ?",
                         {std::to_string(request_id)});
        LOG(INFO) << "已在线推送并标记为已读";
    } else {
        LOG(INFO) << "申请人 " << apply_id << " 不在线，上线后自动拉取通知";
    }

    send_ok(conn, MSG_AGREE_FRIEND, {{"msg", "已同意好友申请"}});
}

// 拒绝好友申请
void FriendHandler::handle_refuse_friend(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_REFUSE_FRIEND, "请先登录");
        return;
    }

    uint64_t agree_id = conn->getUserId();
    auto data = json.value("data", nlohmann::json::object());
    uint64_t request_id = data.value("user_id", 0ull);

    if (request_id == 0) {
        send_error(conn, ERR_PARAM, MSG_REFUSE_FRIEND, "无效的申请ID");
        return;
    }

    MysqlPool& pool = MysqlPool::GetInstance();
    MysqlConnGuard guard(3000);
    MYSQL* conn_mysql = guard.Get();
    if (!conn_mysql) {
        send_error(conn, ERR_SERVER_BUSY, MSG_REFUSE_FRIEND, "服务器繁忙");
        return;
    }

    TransactionGuard tx(conn_mysql);  // 开启事务
    std::string que_sql =
        "SELECT apply_id FROM friend_apply WHERE id = ? AND owner_id = ? "
        "AND status = 0";
    QueryResult que_result =
        pool.Query(conn_mysql, que_sql,
                   {std::to_string(request_id), std::to_string(agree_id)});
    if (que_result.rows.empty()) {
        send_error(conn, ERR_PARAM, MSG_REFUSE_FRIEND, "申请不存在或已处理");
        return;  // 离开作用域，TransactionGuard 自动 ROLLBACK
    }

    uint64_t apply_id = std::stoull(que_result.rows[0][0]);  // 获取申请人id

    std::string update_sql = "UPDATE friend_apply SET status = 2 WHERE id = ?";
    int answer =
        pool.ExecuteStmt(conn_mysql, update_sql, {std::to_string(request_id)});
    if (answer < 0) {
        send_error(conn, ERR_PARAM, MSG_REFUSE_FRIEND, "更新申请状态失败");
        return;
    }

    if (!tx.Commit()) {
        send_error(conn, ERR_PARAM, MSG_REFUSE_FRIEND, "事务提交失败");
        return;
    }

    nlohmann::json notice_data;
    notice_data["type"] = "friend_disagree";
    notice_data["user_id"] = agree_id;

    auto disagree_user = pool.Query("SELECT username FROM users WHERE id = ?",
                                    {std::to_string(agree_id)});
    if (!disagree_user.rows.empty()) {
        notice_data["user_name"] = disagree_user.rows[0][0];
    }

    // 尝试获取申请人的在线连接
    bool is_online = false;
    auto target_conn =
        ConnectionManager::GetInstance().GetConnByUserId(apply_id);
    if (target_conn && target_conn->isLogin())
        is_online = true;
    if (is_online) {
        // 在线，直接推送
        send_ok(target_conn, PUSH_FRIEND_REFUSE, notice_data);
        // 标记推送完成，下次上线不再重复拉取
        pool.ExecuteStmt("UPDATE friend_apply SET is_pushed = 1 WHERE id = ?",
                         {std::to_string(request_id)});
        LOG(INFO) << "已在线推送并标记为已读";
    } else {
        LOG(INFO) << "申请人 " << apply_id << " 不在线，上线后自动拉取通知";
    }

    send_ok(conn, MSG_REFUSE_FRIEND, {{"msg", "已拒绝好友申请"}});
}

// 删除好友
void FriendHandler::handle_del_friend(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_DEL_FRIEND, "请先登录");
        return;
    }

    uint64_t user_id = conn->getUserId();
    auto data = json.value("data", nlohmann::json::object());
    uint64_t friend_id = data.value("user_id", 0ull);

    if (friend_id == 0) {
        send_error(conn, ERR_PARAM, MSG_DEL_FRIEND, "无效的用户ID");
        return;
    }

    MysqlPool& pool = MysqlPool::GetInstance();

    // 检查是不是朋友
    std::string que_sql =
        "SELECT 1 FROM friends WHERE user_id = ? AND friend_id = ? ";
    QueryResult que_result = pool.Query(
        que_sql, {std::to_string(user_id), std::to_string(friend_id)});
    if (que_result.rows.empty()) {
        send_error(conn, ERR_PARAM, MSG_DEL_FRIEND, "朋友不存在或已删除");
        return;
    }

    // 删除
    std::string delete_sql =
        "DELETE FROM friends WHERE (user_id=? AND friend_id=?) OR (user_id=? "
        "AND friend_id=?)";
    int add_answer = pool.ExecuteStmt(
        delete_sql, {std::to_string(user_id), std::to_string(friend_id),
                     std::to_string(friend_id), std::to_string(user_id)});
    if (add_answer < 0) {
        send_error(conn, ERR_PARAM, MSG_DEL_FRIEND, "删除朋友失败");
        return;
    }

    // 删除备注
    pool.ExecuteStmt(
        "DELETE FROM friend_remarks WHERE (user_id=? AND friend_id=?) OR "
        "(user_id=? AND friend_id=?)",
        {std::to_string(user_id), std::to_string(friend_id),
         std::to_string(friend_id), std::to_string(user_id)});

    pool.ExecuteStmt(
        "DELETE FROM friend_apply WHERE (apply_id=? AND owner_id=?) OR "
        "(apply_id=? AND owner_id=?)",
        {std::to_string(user_id), std::to_string(friend_id),
         std::to_string(friend_id), std::to_string(user_id)});
    nlohmann::json resp;
    resp["type"] = "friend_delete";
    resp["user_id"] = user_id;
    send_ok(conn, MSG_DEL_FRIEND, resp);
}

// 获取好友列表
void FriendHandler::handle_get_friend_list(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_GET_FRIEND_LIST, "请先登录");
        return;
    }

    uint64_t user_id = conn->getUserId();
    std::string sql =
        "SELECT u.id, u.username, fr.remark, f.status "
        "FROM friends f "
        "JOIN users u ON f.friend_id = u.id "
        "LEFT JOIN friend_remarks fr ON fr.user_id = f.user_id AND "
        "fr.friend_id = f.friend_id "
        "WHERE f.user_id = ?";

    MysqlPool& pool = MysqlPool::GetInstance();
    QueryResult result = pool.Query(sql, {std::to_string(user_id)});

    nlohmann::json user_list = nlohmann::json::array();
    RedisPool& redis = RedisPool::GetInstance();

    for (const auto& row : result.rows) {
        nlohmann::json user;
        user["user_id"] = std::stoull(row[0]);
        user["username"] = row[1];
        user["remark"] = row[2];
        user["blocked"] = (row[3] == "1");
        // 查询 Redis 在线状态
        user["online"] = redis.set_ismember("online_users", row[0]);
        user_list.push_back(user);
    }

    nlohmann::json resp;
    resp["users"] = user_list;
    send_ok(conn, MSG_GET_FRIEND_LIST, resp);
}

// 获取好友申请列表
void FriendHandler::handle_get_apply_list(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_GET_APPLY_LIST, "请先登录");
        return;
    }

    uint64_t user_id = conn->getUserId();
    std::string sql =
        "SELECT fa.id, fa.apply_id, u.username, fa.message, fa.created_at "
        "FROM friend_apply fa JOIN users u ON fa.apply_id = u.id "
        "WHERE fa.owner_id = ? AND fa.status = 0 ORDER BY fa.created_at DESC";

    MysqlPool& pool = MysqlPool::GetInstance();
    QueryResult result = pool.Query(sql, {std::to_string(user_id)});

    nlohmann::json apply_list = nlohmann::json::array();
    RedisPool& redis = RedisPool::GetInstance();

    for (const auto& row : result.rows) {
        nlohmann::json apply_item;
        apply_item["request_id"] = std::stoull(row[0]);  // fa.id (申请记录ID)
        apply_item["apply_id"] = std::stoull(row[1]);  // fa.apply_id (申请人ID)
        apply_item["username"] = row[2];  // u.username (申请人名字)
        apply_item["message"] = row[3];   // fa.message (附言)
        apply_item["created_at"] = row[4];
        // 查询 Redis 在线状态
        apply_item["online"] = redis.set_ismember("online_users", row[1]);
        apply_list.push_back(apply_item);
    }

    nlohmann::json resp;
    resp["applications"] = apply_list;
    send_ok(conn, MSG_GET_APPLY_LIST, resp);
}

// 屏蔽/取消屏蔽好友消息
void FriendHandler::handle_set_friend_mute(std::shared_ptr<TCPConnection> conn, const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_SET_FRIEND_MUTE, "请先登录");
        return;
    }

    uint64_t user_id = conn->getUserId();
    auto data = json.value("data", nlohmann::json::object());
    uint64_t friend_id = data.value("friend_id", 0ull);
    bool blocked = data.value("blocked", true);  // 默认不屏蔽
    int db_status = blocked ? 1 : 0;

    if (friend_id == 0) {
        send_error(conn, ERR_PARAM, MSG_SET_FRIEND_MUTE, "好友id错误");
        return;
    }

    MysqlPool& pool = MysqlPool::GetInstance();

    std::string que_sql =
        "SELECT 1 FROM friends WHERE user_id = ? AND friend_id = "
        "? ";
    QueryResult que_result = pool.Query(
        que_sql, {std::to_string(user_id), std::to_string(friend_id)});
    if (que_result.rows.empty()) {
        send_error(conn, ERR_PARAM, MSG_SET_FRIEND_MUTE, "朋友不存在或已删除");
        return;
    }

    std::string update_sql =
        "UPDATE friends SET status = ? WHERE user_id = ? AND friend_id = ?";
    int affected = pool.ExecuteStmt(
        update_sql, {std::to_string(db_status), std::to_string(user_id),
                     std::to_string(friend_id)});

    if (affected < 0) {
        send_error(conn, ERR_SERVER_BUSY, MSG_SET_FRIEND_MUTE,
                   "设置屏蔽失败，请稍后重试");
        return;
    }

    LOG(INFO) << "用户 " << user_id << " 已将好友 " << friend_id
              << " 的屏蔽状态设为: " << (blocked ? "屏蔽" : "正常");

    nlohmann::json notice_data;
    notice_data["friend_id"] = friend_id;
    notice_data["blocked"] = blocked;
    send_ok(conn, MSG_SET_FRIEND_MUTE, notice_data);
}

// 设置好友备注
void FriendHandler::handle_set_friend_remark(
    std::shared_ptr<TCPConnection> conn,
    const nlohmann::json& json) {
    if (!conn->isLogin()) {
        send_error(conn, ERR_PERMISSION, MSG_SET_FRIEND_REMARK, "请先登录");
        return;
    }

    uint64_t user_id = conn->getUserId();
    auto data = json.value("data", nlohmann::json::object());
    uint64_t friend_id = data.value("friend_id", 0ull);
    std::string nickname = data.value("remark", "");

    if (friend_id == 0) {
        send_error(conn, ERR_PARAM, MSG_SET_FRIEND_REMARK, "好友id错误");
        return;
    }

    MysqlPool& pool = MysqlPool::GetInstance();

    std::string que_sql =
        "SELECT 1 FROM friends WHERE user_id = ? AND friend_id = "
        "? ";
    QueryResult que_result = pool.Query(
        que_sql, {std::to_string(user_id), std::to_string(friend_id)});
    if (que_result.rows.empty()) {
        send_error(conn, ERR_PARAM, MSG_SET_FRIEND_REMARK,
                   "朋友不存在或已删除");
        return;
    }

    std::string update_sql =
        "INSERT INTO friend_remarks (user_id, friend_id, remark) VALUES (?, ?, "
        "?) "
        "ON DUPLICATE KEY UPDATE remark = ?";
    int affected = pool.ExecuteStmt(
        update_sql, {std::to_string(user_id), std::to_string(friend_id),
                     nickname, nickname});

    if (affected < 0) {
        send_error(conn, ERR_SERVER_BUSY, MSG_SET_FRIEND_REMARK,
                   "设置备注失败，请稍后重试");
        return;
    }

    LOG(INFO) << "用户 " << user_id << " 设置好友 " << friend_id
              << " 的备注为: " << nickname;

    nlohmann::json notice_data;
    notice_data["friend_id"] = friend_id;
    notice_data["nickname"] = nickname;
    send_ok(conn, MSG_SET_FRIEND_REMARK, notice_data);
}

void FriendHandler::pushOfflineFriendMsgs(std::shared_ptr<TCPConnection> conn, uint64_t user_id) {
    MysqlPool& pool = MysqlPool::GetInstance();

    // 1. 推送未读的好友申请（别人发给我的）
    QueryResult new_applies = pool.Query(
        "SELECT fa.id, fa.apply_id, u.username, fa.message "
        "FROM friend_apply fa "
        "JOIN users u ON fa.apply_id = u.id "
        "WHERE fa.owner_id = ? AND fa.status = 0 AND fa.is_pushed = 0",
        {std::to_string(user_id)});

    for (const auto& row : new_applies.rows) {
        uint64_t apply_id = std::stoull(row[0]);
        uint64_t from_id = std::stoull(row[1]);
        std::string username = row[2];
        std::string message = row[3];

        nlohmann::json push;
        push["type"] = "new_friend_request";
        push["apply_id"] = from_id;
        push["message"] = message;
        push["apply_name"] = username;
        send_ok(conn, PUSH_OFFLINE_NOTICE, push);

        pool.ExecuteStmt("UPDATE friend_apply SET is_pushed = 1 WHERE id = ?",
                         {std::to_string(apply_id)});
    }

    // 2. 推送好友申请结果（我发出的申请被同意/拒绝）
    QueryResult results = pool.Query(
        "SELECT fa.id, fa.owner_id, u.username, fa.status "
        "FROM friend_apply fa "
        "JOIN users u ON fa.owner_id = u.id "
        "WHERE fa.apply_id = ? AND fa.status IN (1, 2) AND fa.is_pushed = 0",
        {std::to_string(user_id)});

    for (const auto& row : results.rows) {
        uint64_t apply_id = std::stoull(row[0]);
        uint64_t owner_id = std::stoull(row[1]);
        std::string username = row[2];
        int status = std::stoi(row[3]);

        nlohmann::json push;
        push["type"] = status == 1 ? "friend_agree" : "friend_refuse";
        push["user_id"] = owner_id;
        push["user_name"] = username;
        send_ok(conn, PUSH_OFFLINE_NOTICE, push);

        // 设置为已推送
        pool.ExecuteStmt("UPDATE friend_apply SET is_pushed = 1 WHERE id = ?",
                         {std::to_string(apply_id)});
    }
}